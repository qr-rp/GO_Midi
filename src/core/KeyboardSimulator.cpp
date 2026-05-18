#include "KeyboardSimulator.h"
#include "../util/Logger.h"
#include <psapi.h>
#include <iostream>
#include <unordered_map>

namespace Core
{

    // 获取按键扫描码（无缓存，线程安全）
    static UINT GetScanCode(int vk)
    {
        if (vk < 0 || vk > 255)
            return MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        return MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    }

    KeyboardSimulator::KeyboardSimulator()
    {
        LOG_DEBUG("[KeyboardSimulator] 初始化");
    }

    KeyboardSimulator::~KeyboardSimulator()
    {
        LOG_DEBUG("[KeyboardSimulator] 销毁");
    }

    void KeyboardSimulator::send_input(int vk_code, int modifier, bool key_up)
    {
        // 优化：使用栈数组避免堆分配
        INPUT inputs[8] = {};
        int count = 0;

        // 辅助 lambda 填充 INPUT 结构
        auto add_input = [&](int vk, bool up)
        {
            if (count >= 8)
                return;
            INPUT &input = inputs[count++];

            input.type = INPUT_KEYBOARD;
            input.ki.wVk = static_cast<WORD>(vk);
            input.ki.dwFlags = 0;

            // 使用 vk 码而非扫描码，避免 KEYEVENTF_SCANCODE + KEYEVENTF_EXTENDEDKEY 冲突
            // 同时省去扫描码查找开销，天然线程安全

            if (up)
            {
                input.ki.dwFlags |= KEYEVENTF_KEYUP;
            }
        };

        if (!key_up)
        {
            // 按键按下序列

            // 1. 按下修饰键
            if (modifier == 1)
                add_input(VK_SHIFT, false);
            if (modifier == 2)
                add_input(VK_CONTROL, false);

            // 2. 按下主键
            add_input(vk_code, false);

            // 3. 释放修饰键（瞬时策略）
            if (modifier == 1)
                add_input(VK_SHIFT, true);
            if (modifier == 2)
                add_input(VK_CONTROL, true);
        }
        else
        {
            // 按键释放序列
            // 只释放主键
            add_input(vk_code, true);

            // 同时释放修饰键（安全措施）
            if (modifier == 1)
                add_input(VK_SHIFT, true);
            if (modifier == 2)
                add_input(VK_CONTROL, true);
        }

        if (count > 0)
        {
            SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
        }
    }

    void KeyboardSimulator::send_key_down(int vk_code, int modifier, void *hwnd)
    {
        LOG_DEBUG("按键按下: VK=0x" << std::hex << vk_code << std::dec
                                    << ", 修饰符=" << modifier
                                    << ", 窗口=" << hwnd);

        if (hwnd)
        {
            HWND h = static_cast<HWND>(hwnd);

            auto send_msg = [&](int vk, bool up)
            {
                UINT scanCode = GetScanCode(vk);
                LPARAM lParam = 1; // 重复计数 1
                lParam |= (scanCode << 16);

                bool extended = ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE);
                if (extended)
                    lParam |= ((LPARAM)1 << 24);

                if (up)
                {
                    lParam |= ((LPARAM)1 << 30); // 前一按键状态
                    lParam |= ((LPARAM)1 << 31); // 转换状态
                    PostMessage(h, WM_KEYUP, vk, lParam);
                }
                else
                {
                    PostMessage(h, WM_KEYDOWN, vk, lParam);
                }
            };

            // 1. 按下修饰键
            if (modifier == 1)
                send_msg(VK_SHIFT, false);
            if (modifier == 2)
                send_msg(VK_CONTROL, false);

            // 2. 按下主键
            send_msg(vk_code, false);

            // 3. 释放修饰键（瞬时）
            if (modifier == 1)
                send_msg(VK_SHIFT, true);
            if (modifier == 2)
                send_msg(VK_CONTROL, true);
        }
        else
        {
            send_input(vk_code, modifier, false);
        }
    }

    void KeyboardSimulator::send_key_up(int vk_code, int modifier, void *hwnd)
    {
        LOG_DEBUG("按键释放: VK=0x" << std::hex << vk_code << std::dec
                                    << ", 修饰符=" << modifier
                                    << ", 窗口=" << hwnd);

        if (hwnd)
        {
            HWND h = static_cast<HWND>(hwnd);

            auto send_msg = [&](int vk)
            {
                UINT scanCode = GetScanCode(vk);
                LPARAM lParam = 1;
                lParam |= (scanCode << 16);

                if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE)
                {
                    lParam |= ((LPARAM)1 << 24);
                }

                lParam |= ((LPARAM)1 << 30);
                lParam |= ((LPARAM)1 << 31);

                PostMessage(h, WM_KEYUP, vk, lParam);
            };

            // 释放主键
            send_msg(vk_code);

            // 同时释放修饰键（安全措施）
            if (modifier == 1)
                send_msg(VK_SHIFT);
            if (modifier == 2)
                send_msg(VK_CONTROL);
        }
        else
        {
            send_input(vk_code, modifier, true);
        }
    }

    void KeyboardSimulator::release_keys(const std::vector<std::pair<int, void*>>& keys)
    {
        // 按窗口句柄分组，对每个窗口批量 PostMessage，无窗口的合并 SendInput
        std::unordered_map<void*, std::vector<int>> groups;
        for (const auto& [vk, hwnd] : keys)
            groups[hwnd].push_back(vk);

        // 栈数组缓存 INPUT，避免每次分配
        INPUT inputs[256] = {};
        int input_count = 0;

        for (const auto& [hwnd, vk_list] : groups)
        {
            if (hwnd)
            {
                // 有目标窗口 → PostMessage 批量释放，不等待
                HWND h = static_cast<HWND>(hwnd);
                for (int vk : vk_list)
                {
                    UINT scanCode = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
                    LPARAM lParam = 1 | (scanCode << 16)
                                  | ((LPARAM)1 << 30)
                                  | ((LPARAM)1 << 31);
                    if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE)
                        lParam |= ((LPARAM)1 << 24);
                    PostMessage(h, WM_KEYUP, static_cast<WPARAM>(vk), lParam);
                }
            }
            else
            {
                // 无目标窗口 → 合并到 SendInput 批处理
                for (int vk : vk_list)
                {
                    if (input_count + 1 > 256) {
                        // 缓冲区满，先发一次
                        SendInput(static_cast<UINT>(input_count), inputs, sizeof(INPUT));
                        input_count = 0;
                    }
                    inputs[input_count].type = INPUT_KEYBOARD;
                    inputs[input_count].ki.wVk = static_cast<WORD>(vk);
                    inputs[input_count].ki.dwFlags = KEYEVENTF_KEYUP;
                    input_count++;
                }
            }
        }

        // 发送余下的 SendInput
        if (input_count > 0)
            SendInput(static_cast<UINT>(input_count), inputs, sizeof(INPUT));
    }

    // 辅助函数：宽字符串转 UTF-8
    static std::string WideToUtf8(const wchar_t *wideStr)
    {
        if (!wideStr)
            return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
        if (size <= 0)
            return "";
        std::vector<char> buffer(size);
        WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, buffer.data(), size, nullptr, nullptr);
        return std::string(buffer.data());
    }

    BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
    {
        auto *windows = reinterpret_cast<std::vector<KeyboardSimulator::WindowInfo> *>(lParam);

        if (IsWindowVisible(hwnd))
        {
            // 使用 Unicode 版本获取窗口标题
            wchar_t titleW[256];
            int len = GetWindowTextW(hwnd, titleW, sizeof(titleW) / sizeof(wchar_t));

            // 只添加有非空标题的窗口
            if (len > 0)
            {
                std::string title = WideToUtf8(titleW);

                // 跳过转换失败或标题为空/只有空白的窗口
                if (title.empty())
                    return TRUE;

                // 检查标题是否只包含空白
                bool hasNonWhitespace = false;
                for (char c : title)
                {
                    if (!isspace(static_cast<unsigned char>(c)))
                    {
                        hasNonWhitespace = true;
                        break;
                    }
                }
                if (!hasNonWhitespace)
                    return TRUE;

                // 获取进程名
                DWORD pid;
                GetWindowThreadProcessId(hwnd, &pid);
                std::string processName = "Unknown";

                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                if (hProcess)
                {
                    wchar_t bufferW[MAX_PATH];
                    if (GetModuleBaseNameW(hProcess, NULL, bufferW, MAX_PATH))
                    {
                        processName = WideToUtf8(bufferW);
                    }
                    CloseHandle(hProcess);
                }

                windows->push_back({hwnd, title, processName, pid});
            }
        }
        return TRUE;
    }

    std::vector<KeyboardSimulator::WindowInfo> KeyboardSimulator::GetWindowList()
    {
        LOG_DEBUG("[KeyboardSimulator] 获取窗口列表");

        std::vector<WindowInfo> windows;
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));

        LOG_DEBUG("找到 " << windows.size() << " 个可见窗口");
        return windows;
    }

}