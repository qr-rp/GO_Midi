#include "KeyboardSimulator.h"
#include "../util/Logger.h"
#include "../util/KeyManager.h"
#include <psapi.h>
#include <iostream>
#include <unordered_map>

namespace Core
{

    struct ModEvent
    {
        int vk{0};
        UINT downMessage{0};
        UINT upMessage{0};
        DWORD downFlag{0};
        DWORD upFlag{0};
    };

    static ModEvent ResolveMod(int modbit)
    {
        switch (modbit)
        {
            case Util::kModShift:
                return {VK_SHIFT, 0, 0, 0, 0};
            case Util::kModCtrl:
                return {VK_CONTROL, 0, 0, 0, 0};
            case Util::kModAlt:
                return {VK_MENU, 0, 0, 0, 0};
            case Util::kModMouseL:
                return {0, WM_LBUTTONDOWN, WM_LBUTTONUP,
                        MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP};
            case Util::kModMouseM:
                return {0, WM_MBUTTONDOWN, WM_MBUTTONUP,
                        MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP};
            case Util::kModMouseR:
                return {0, WM_RBUTTONDOWN, WM_RBUTTONUP,
                        MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP};
        }
        return {};
    }

    static const int kAllModBits[6] = {
        Util::kModShift, Util::kModCtrl, Util::kModAlt,
        Util::kModMouseL, Util::kModMouseM, Util::kModMouseR
    };

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

    void KeyboardSimulator::send_key_events(const std::vector<KeyInputEvent>& events)
    {
        if (events.empty())
            return;

        // A2: 无目标窗口的按键合并为单次 SendInput。
        // 栈数组缓存 INPUT，避免重复分配（同 release_keys 的 256 上限）。
        INPUT inputs[256] = {};
        int input_count = 0;

        auto flush = [&]() {
            if (input_count > 0) {
                SendInput(static_cast<UINT>(input_count), inputs, sizeof(INPUT));
                input_count = 0;
            }
        };

        // 追加键盘 INPUT 项，缓冲区满时先 flush
        auto add_key_input = [&](int vk, bool up) {
            if (input_count >= 256)
                flush();
            INPUT& input = inputs[input_count++];
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = static_cast<WORD>(vk);
            input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
        };

        // 追加鼠标 INPUT 项（当前光标位置）
        auto add_mouse_input = [&](DWORD flags) {
            if (input_count >= 256)
                flush();
            INPUT& input = inputs[input_count++];
            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = flags;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
        };

        // 有目标窗口：键盘按键 PostMessage（scanCode / extended / up 标志）
        auto post_key_msg = [&](HWND h, int vk, bool up) {
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

        // 有目标窗口：鼠标消息 PostMessage（客户区中心; wParam 带键盘修饰状态, 无 Alt 标志位）
        auto post_mouse_msg = [&](HWND h, UINT msg, int mods) {
            WPARAM wp = 0;
            if (mods & Util::kModShift) wp |= MK_SHIFT;
            if (mods & Util::kModCtrl)  wp |= MK_CONTROL;
            RECT rc{};
            if (GetClientRect(h, &rc))
            {
                LPARAM lp = MAKELPARAM((rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2);
                PostMessage(h, msg, wp, lp);
            }
        };

        // 发送单个修饰位（按目标路由: nullptr → SendInput 批, hwnd → PostMessage）
        auto emit_mod = [&](void* target, int modbit, bool down) {
            const ModEvent mod = ResolveMod(modbit);
            int cur_mods = 0;
            auto it = m_mod_state.find(target);
            if (it != m_mod_state.end())
                cur_mods = it->second.held;

            if (target)
            {
                HWND h = static_cast<HWND>(target);
                if (mod.vk)
                    post_key_msg(h, mod.vk, !down);
                else if (mod.downMessage)
                    post_mouse_msg(h, down ? mod.downMessage : mod.upMessage, cur_mods);
            }
            else
            {
                if (mod.vk)
                    add_key_input(mod.vk, !down);
                else if (mod.downFlag)
                    add_mouse_input(down ? mod.downFlag : mod.upFlag);
            }
        };

        // 发送主键（按目标路由）: down=true 表示按下
        // (post_key_msg/add_key_input 的参数都是 up 语义, 这里统一转换)
        auto emit_key = [&](void* target, int vk, bool down) {
            if (target)
                post_key_msg(static_cast<HWND>(target), vk, !down);
            else
                add_key_input(vk, !down);
        };

        for (const auto& evt : events)
        {
            void* target = evt.window_handle;
            ModState& st = m_mod_state[target];

            // 按下/抬起本音符用到的全部修饰位（位掩码: Shift/Ctrl/Alt/鼠标左中右）
            auto emit_mods = [&](bool down) {
                for (int bit : kAllModBits)
                {
                    if (!(evt.modifier & bit))
                        continue;
                    if (down)
                        st.held |= bit;
                    else
                        st.held &= ~bit;
                    emit_mod(target, bit, down);
                }
            };

            if (evt.is_note_on)
            {
                // 瞬时策略：① 按下修饰 → ② 主键 down → ③ 立即释放修饰
                emit_mods(true);

                // (重叠同主键音符已由 PlaybackEngine 冲突模块按 vk 截断为 legato,
                //   这里不会收到同键重叠事件, 直接瞬时按下)
                emit_key(target, evt.vk_code, true);

                emit_mods(false);
            }
            else
            {
                // 主键 up；同时释放修饰（安全措施，重复释放无害）
                emit_key(target, evt.vk_code, false);

                emit_mods(false);
            }
        }

        flush();
    }

    void KeyboardSimulator::release_all_mods()
    {
        if (m_mod_state.empty())
            return;

        LOG_DEBUG("[KeyboardSimulator] 释放全部保持中的修饰键");

        INPUT inputs[16] = {};
        int input_count = 0;

        auto flush = [&]() {
            if (input_count > 0) {
                SendInput(static_cast<UINT>(input_count), inputs, sizeof(INPUT));
                input_count = 0;
            }
        };

        for (auto& [target, st] : m_mod_state)
        {
            if (!st.held)
                continue;

            if (target)
            {
                // PostMessage: 键盘 → WM_KEYUP; 鼠标 → WM_*BUTTONUP（客户区中心）
                HWND h = static_cast<HWND>(target);
                int cur_mods = st.held;
                for (int bit : kAllModBits)
                {
                    if (!(st.held & bit))
                        continue;
                    const ModEvent mod = ResolveMod(bit);
                    if (mod.vk)
                    {
                        UINT scanCode = GetScanCode(mod.vk);
                        LPARAM lParam = 1 | (scanCode << 16)
                                      | ((LPARAM)1 << 30)
                                      | ((LPARAM)1 << 31);
                        if ((mod.vk >= VK_PRIOR && mod.vk <= VK_DOWN) ||
                            mod.vk == VK_INSERT || mod.vk == VK_DELETE)
                            lParam |= ((LPARAM)1 << 24);
                        PostMessage(h, WM_KEYUP, mod.vk, lParam);
                    }
                    else
                    {
                        WPARAM wp = 0;
                        if (cur_mods & Util::kModShift) wp |= MK_SHIFT;
                        if (cur_mods & Util::kModCtrl)  wp |= MK_CONTROL;
                        RECT rc{};
                        if (GetClientRect(h, &rc))
                        {
                            LPARAM lp = MAKELPARAM((rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2);
                            PostMessage(h, mod.upMessage, wp, lp);
                        }
                    }
                }
            }
            else
            {
                // SendInput 批: 键盘 → KEYUP; 鼠标 → MOUSEEVENTF_*UP
                for (int bit : kAllModBits)
                {
                    if (!(st.held & bit))
                        continue;
                    const ModEvent mod = ResolveMod(bit);
                    if (mod.vk)
                    {
                        if (input_count >= 16) flush();
                        inputs[input_count].type = INPUT_KEYBOARD;
                        inputs[input_count].ki.wVk = static_cast<WORD>(mod.vk);
                        inputs[input_count].ki.dwFlags = KEYEVENTF_KEYUP;
                        input_count++;
                    }
                    else
                    {
                        if (input_count >= 16) flush();
                        inputs[input_count].type = INPUT_MOUSE;
                        inputs[input_count].mi.dx = 0;
                        inputs[input_count].mi.dy = 0;
                        inputs[input_count].mi.mouseData = 0;
                        inputs[input_count].mi.dwFlags = mod.upFlag;
                        inputs[input_count].mi.time = 0;
                        inputs[input_count].mi.dwExtraInfo = 0;
                        input_count++;
                    }
                }
            }
        }

        flush();
        m_mod_state.clear();
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
