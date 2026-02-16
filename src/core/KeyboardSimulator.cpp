#include "KeyboardSimulator.h"
#include "../util/Logger.h"
#include <iostream>

namespace Core
{

    // Helper to cache scan codes and reduce system calls
    static UINT GetCachedScanCode(int vk)
    {
        static UINT codes[256] = {0};
        static bool valid[256] = {false};

        if (vk < 0 || vk > 255)
            return MapVirtualKey(vk, MAPVK_VK_TO_VSC);

        if (!valid[vk])
        {
            codes[vk] = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
            valid[vk] = true;
        }
        return codes[vk];
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
#ifdef _WIN32
        // Optimization: Use stack array to avoid heap allocation
        INPUT inputs[8] = {};
        int count = 0;

        // Helper lambda to fill INPUT structure
        auto add_input = [&](int vk, bool up)
        {
            if (count >= 8)
                return;
            INPUT &input = inputs[count++];

            input.type = INPUT_KEYBOARD;
            input.ki.wVk = static_cast<WORD>(vk);

            // Try to map to scan code for better compatibility
            UINT scanCode = GetCachedScanCode(vk);
            if (scanCode > 0)
            {
                input.ki.wScan = static_cast<WORD>(scanCode);
                input.ki.dwFlags = KEYEVENTF_SCANCODE;
            }

            // Handle extended keys
            if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE)
            {
                input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
            }

            if (up)
            {
                input.ki.dwFlags |= KEYEVENTF_KEYUP;
            }
        };

        if (!key_up)
        {
            // Key Down Sequence

            // 1. Press Modifiers
            if (modifier == 1)
                add_input(VK_SHIFT, false);
            if (modifier == 2)
                add_input(VK_CONTROL, false);

            // 2. Press Main Key
            add_input(vk_code, false);

            // 3. Release Modifiers (Transient Strategy)
            if (modifier == 1)
                add_input(VK_SHIFT, true);
            if (modifier == 2)
                add_input(VK_CONTROL, true);
        }
        else
        {
            // Key Up Sequence
            // Just release main key
            add_input(vk_code, true);

            // Also release modifiers if they were pressed (safety measure)
            if (modifier == 1)
                add_input(VK_SHIFT, true);
            if (modifier == 2)
                add_input(VK_CONTROL, true);
        }

        if (count > 0)
        {
            SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
        }
#endif
    }

    void KeyboardSimulator::send_key_down(int vk_code, int modifier, void *hwnd)
    {
        LOG_DEBUG("按键按下: VK=0x" << std::hex << vk_code << std::dec
                                    << ", 修饰符=" << modifier
                                    << ", 窗口=" << hwnd);

#ifdef _WIN32
        if (hwnd)
        {
            HWND h = static_cast<HWND>(hwnd);

            auto send_msg = [&](int vk, bool up)
            {
                UINT scanCode = GetCachedScanCode(vk);
                LPARAM lParam = 1; // Repeat count 1
                lParam |= (scanCode << 16);

                bool extended = ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE);
                if (extended)
                    lParam |= ((LPARAM)1 << 24);

                if (up)
                {
                    lParam |= ((LPARAM)1 << 30); // Previous key state
                    lParam |= ((LPARAM)1 << 31); // Transition state
                    PostMessage(h, WM_KEYUP, vk, lParam);
                }
                else
                {
                    PostMessage(h, WM_KEYDOWN, vk, lParam);
                }
            };

            // 1. Press Modifiers
            if (modifier == 1)
                send_msg(VK_SHIFT, false);
            if (modifier == 2)
                send_msg(VK_CONTROL, false);

            // 2. Press Main Key
            send_msg(vk_code, false);

            // 3. Release Modifiers (Transient)
            if (modifier == 1)
                send_msg(VK_SHIFT, true);
            if (modifier == 2)
                send_msg(VK_CONTROL, true);
        }
        else
        {
            send_input(vk_code, modifier, false);
        }
#endif
    }

    void KeyboardSimulator::send_key_up(int vk_code, int modifier, void *hwnd)
    {
        LOG_DEBUG("按键释放: VK=0x" << std::hex << vk_code << std::dec
                                    << ", 修饰符=" << modifier
                                    << ", 窗口=" << hwnd);

#ifdef _WIN32
        if (hwnd)
        {
            HWND h = static_cast<HWND>(hwnd);

            UINT scanCode = GetCachedScanCode(vk_code);
            LPARAM lParam = 1;
            lParam |= (scanCode << 16);

            if ((vk_code >= VK_PRIOR && vk_code <= VK_DOWN) || vk_code == VK_INSERT || vk_code == VK_DELETE)
            {
                lParam |= ((LPARAM)1 << 24);
            }

            lParam |= ((LPARAM)1 << 30);
            lParam |= ((LPARAM)1 << 31);

            PostMessage(h, WM_KEYUP, vk_code, lParam);
        }
        else
        {
            send_input(vk_code, modifier, true);
        }
#endif
    }

    void KeyboardSimulator::send_key_press(int vk_code, int modifier, void *hwnd)
    {
        send_key_down(vk_code, modifier, hwnd);
        send_key_up(vk_code, modifier, hwnd);
    }

#ifdef _WIN32
#include <psapi.h>

    // Helper: Convert wide string to UTF-8
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
            // Use Unicode version to get window title
            wchar_t titleW[256];
            int len = GetWindowTextW(hwnd, titleW, sizeof(titleW) / sizeof(wchar_t));

            // Only add windows with non-empty titles
            if (len > 0)
            {
                std::string title = WideToUtf8(titleW);

                // Skip if conversion failed or title is empty/whitespace only
                if (title.empty())
                    return TRUE;

                // Check if title contains only whitespace
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

                // Get Process Name
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
#endif

    std::vector<KeyboardSimulator::WindowInfo> KeyboardSimulator::GetWindowList()
    {
        LOG_DEBUG("[KeyboardSimulator] 获取窗口列表");

        std::vector<WindowInfo> windows;
#ifdef _WIN32
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
#endif

        LOG_DEBUG("找到 " << windows.size() << " 个可见窗口");
        return windows;
    }

}
