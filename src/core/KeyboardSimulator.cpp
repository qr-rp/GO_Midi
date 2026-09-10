#include "KeyboardSimulator.h"
#include "../util/Logger.h"
#include "../util/KeyManager.h"
#include <psapi.h>
#include <iostream>
#include <unordered_map>

namespace Core
{

    // 鼠标修饰键按下/释放 flag（配合 Util::kModMouseL/M/R 使用）
    static DWORD MouseDownFlag(int modbit)
    {
        switch (modbit)
        {
            case Util::kModMouseL: return MOUSEEVENTF_LEFTDOWN;
            case Util::kModMouseM: return MOUSEEVENTF_MIDDLEDOWN;
            case Util::kModMouseR: return MOUSEEVENTF_RIGHTDOWN;
        }
        return 0;
    }

    static DWORD MouseUpFlag(int modbit)
    {
        switch (modbit)
        {
            case Util::kModMouseL: return MOUSEEVENTF_LEFTUP;
            case Util::kModMouseM: return MOUSEEVENTF_MIDDLEUP;
            case Util::kModMouseR: return MOUSEEVENTF_RIGHTUP;
        }
        return 0;
    }

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
        // 容量: 6 修饰键 down + 主键 + 6 修饰键 up = 13, 取 16 富余
        INPUT inputs[16] = {};
        int count = 0;

        // 辅助 lambda 填充键盘 INPUT 结构
        auto add_key_input = [&](int vk, bool up)
        {
            if (count >= 16)
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

        // 辅助 lambda 填充鼠标 INPUT 结构（当前光标位置点击）
        auto add_mouse_input = [&](DWORD flags)
        {
            if (count >= 16)
                return;
            INPUT &input = inputs[count++];

            input.type = INPUT_MOUSE;
            input.mi.dx = 0;
            input.mi.dy = 0;
            input.mi.mouseData = 0;
            input.mi.dwFlags = flags;
            input.mi.time = 0;
            input.mi.dwExtraInfo = 0;
        };

        // 按下/释放修饰键（位掩码: Shift/Ctrl/Alt/鼠标左中右）
        auto mods = [&](bool up)
        {
            if (modifier & Util::kModShift)
                add_key_input(VK_SHIFT, up);
            if (modifier & Util::kModCtrl)
                add_key_input(VK_CONTROL, up);
            if (modifier & Util::kModAlt)
                add_key_input(VK_MENU, up);
            // 鼠标修饰键 → 鼠标事件（当前光标位置）
            if (modifier & Util::kModMouseL)
                add_mouse_input(up ? MouseUpFlag(Util::kModMouseL) : MouseDownFlag(Util::kModMouseL));
            if (modifier & Util::kModMouseM)
                add_mouse_input(up ? MouseUpFlag(Util::kModMouseM) : MouseDownFlag(Util::kModMouseM));
            if (modifier & Util::kModMouseR)
                add_mouse_input(up ? MouseUpFlag(Util::kModMouseR) : MouseDownFlag(Util::kModMouseR));
        };

        if (!key_up)
        {
            // 按键按下序列
            // 1. 按下修饰键
            mods(false);

            // 2. 按下主键（主键永远是键盘 VK）
            add_key_input(vk_code, false);

            // 3. 释放修饰键（瞬时策略）
            mods(true);
        }
        else
        {
            // 按键释放序列：只释放主键
            add_key_input(vk_code, true);

            // 同时释放修饰键（安全措施）
            mods(true);
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

            // 键盘按键 PostMessage（scanCode / extended / up 标志）
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

            // 鼠标消息 PostMessage（客户区中心; wParam 带键盘修饰状态, 无 Alt 标志位）
            auto post_mouse_msg = [&](UINT msg, int mods)
            {
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

            // 修饰键 PostMessage: 键盘 → WM_KEYDOWN/UP; 鼠标 → WM_*BUTTONDOWN/UP(客户区中心)
            auto send_mod = [&](int modbit, bool down)
            {
                switch (modbit)
                {
                    case Util::kModShift: send_msg(VK_SHIFT, !down); break;
                    case Util::kModCtrl: send_msg(VK_CONTROL, !down); break;
                    case Util::kModAlt: send_msg(VK_MENU, !down); break;
                    case Util::kModMouseL: post_mouse_msg(down ? WM_LBUTTONDOWN : WM_LBUTTONUP, modifier); break;
                    case Util::kModMouseM: post_mouse_msg(down ? WM_MBUTTONDOWN : WM_MBUTTONUP, modifier); break;
                    case Util::kModMouseR: post_mouse_msg(down ? WM_RBUTTONDOWN : WM_RBUTTONUP, modifier); break;
                }
            };

            // 1. 按下修饰键
            if (modifier & Util::kModShift) send_mod(Util::kModShift, true);
            if (modifier & Util::kModCtrl)  send_mod(Util::kModCtrl, true);
            if (modifier & Util::kModAlt)   send_mod(Util::kModAlt, true);
            if (modifier & Util::kModMouseL) send_mod(Util::kModMouseL, true);
            if (modifier & Util::kModMouseM) send_mod(Util::kModMouseM, true);
            if (modifier & Util::kModMouseR) send_mod(Util::kModMouseR, true);

            // 2. 按下主键（主键永远是键盘 VK）
            send_msg(vk_code, false);

            // 3. 释放修饰键（瞬时）
            if (modifier & Util::kModShift) send_mod(Util::kModShift, false);
            if (modifier & Util::kModCtrl)  send_mod(Util::kModCtrl, false);
            if (modifier & Util::kModAlt)   send_mod(Util::kModAlt, false);
            if (modifier & Util::kModMouseL) send_mod(Util::kModMouseL, false);
            if (modifier & Util::kModMouseM) send_mod(Util::kModMouseM, false);
            if (modifier & Util::kModMouseR) send_mod(Util::kModMouseR, false);
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

            // 键盘按键 PostMessage（UP）
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

            // 鼠标消息 PostMessage（客户区中心; wParam 带键盘修饰状态）
            auto post_mouse_msg = [&](UINT msg, int mods)
            {
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

            // 修饰键 PostMessage 释放
            auto send_mod_up = [&](int modbit)
            {
                switch (modbit)
                {
                    case Util::kModShift: send_msg(VK_SHIFT); break;
                    case Util::kModCtrl: send_msg(VK_CONTROL); break;
                    case Util::kModAlt: send_msg(VK_MENU); break;
                    case Util::kModMouseL: post_mouse_msg(WM_LBUTTONUP, modifier); break;
                    case Util::kModMouseM: post_mouse_msg(WM_MBUTTONUP, modifier); break;
                    case Util::kModMouseR: post_mouse_msg(WM_RBUTTONUP, modifier); break;
                }
            };

            // 释放主键
            send_msg(vk_code);

            // 同时释放修饰键（安全措施）
            if (modifier & Util::kModShift) send_mod_up(Util::kModShift);
            if (modifier & Util::kModCtrl)  send_mod_up(Util::kModCtrl);
            if (modifier & Util::kModAlt)   send_mod_up(Util::kModAlt);
            if (modifier & Util::kModMouseL) send_mod_up(Util::kModMouseL);
            if (modifier & Util::kModMouseM) send_mod_up(Util::kModMouseM);
            if (modifier & Util::kModMouseR) send_mod_up(Util::kModMouseR);
        }
        else
        {
            send_input(vk_code, modifier, true);
        }
    }

    // ---- 修饰位工具（键盘修饰 → VK; 鼠标修饰 → 鼠标 flag）----
    // 返回键盘 VK（Shift/Ctrl/Alt），鼠标修饰返回 0
    static int ModToVk(int modbit)
    {
        switch (modbit)
        {
            case Util::kModShift: return VK_SHIFT;
            case Util::kModCtrl:  return VK_CONTROL;
            case Util::kModAlt:   return VK_MENU;
        }
        return 0;
    }

    // 返回鼠标事件 flag（左/中/右），键盘修饰返回 0
    static DWORD ModToMouseFlag(int modbit, bool down)
    {
        switch (modbit)
        {
            case Util::kModMouseL: return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            case Util::kModMouseM: return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            case Util::kModMouseR: return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        }
        return 0;
    }

    // 修饰位 → 引用计数索引（0..5）
    static int ModBitIndex(int modbit)
    {
        switch (modbit)
        {
            case Util::kModShift:   return 0;
            case Util::kModCtrl:    return 1;
            case Util::kModAlt:     return 2;
            case Util::kModMouseL:  return 3;
            case Util::kModMouseM:  return 4;
            case Util::kModMouseR:  return 5;
        }
        return 0;
    }

    // 全部修饰位（枚举顺序与 ModBitIndex 对应）
    static const int kAllModBits[6] = {
        Util::kModShift, Util::kModCtrl, Util::kModAlt,
        Util::kModMouseL, Util::kModMouseM, Util::kModMouseR
    };

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
            int cur_mods = 0;
            auto it = m_mod_state.find(target);
            if (it != m_mod_state.end())
                cur_mods = it->second.held;

            if (target)
            {
                HWND h = static_cast<HWND>(target);
                int vk = ModToVk(modbit);
                if (vk)
                    post_key_msg(h, vk, !down);
                else
                {
                    switch (modbit)
                    {
                        case Util::kModMouseL: post_mouse_msg(h, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, cur_mods); break;
                        case Util::kModMouseM: post_mouse_msg(h, down ? WM_MBUTTONDOWN : WM_MBUTTONUP, cur_mods); break;
                        case Util::kModMouseR: post_mouse_msg(h, down ? WM_RBUTTONDOWN : WM_RBUTTONUP, cur_mods); break;
                    }
                }
            }
            else
            {
                int vk = ModToVk(modbit);
                if (vk)
                    add_key_input(vk, !down);
                else
                    add_mouse_input(ModToMouseFlag(modbit, down));
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

            if (evt.is_note_on)
            {
                // ① 切换修饰：释放 held 中不在 modifier 的位，按下 modifier 中不在 held 的位
                int release = st.held & ~evt.modifier;
                int press   = evt.modifier & ~st.held;
                for (int bit : kAllModBits)
                {
                    if (release & bit) emit_mod(target, bit, false);  // up
                    if (press   & bit) emit_mod(target, bit, true);   // down
                }
                st.held = evt.modifier;

                // ② 主键 down
                // (重叠同主键音符已由 PlaybackEngine 冲突模块按 vk 截断为 legato,
                //   这里不会收到同键重叠事件, 直接瞬时按下)
                emit_key(target, evt.vk_code, true);

                // ③ 修饰引用计数 +1
                for (int bit : kAllModBits)
                    if (evt.modifier & bit)
                        st.refs[ModBitIndex(bit)]++;
            }
            else
            {
                // ① 主键 up
                emit_key(target, evt.vk_code, false);

                // ② 修饰引用计数 -1，归零位物理释放（重复释放无害）
                for (int bit : kAllModBits)
                {
                    if (!(evt.modifier & bit))
                        continue;
                    int& ref = st.refs[ModBitIndex(bit)];
                    if (ref > 0)
                        ref--;
                    if (ref == 0 && (st.held & bit))
                    {
                        emit_mod(target, bit, false);  // up
                        st.held &= ~bit;
                    }
                }
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
                    int vk = ModToVk(bit);
                    if (vk)
                    {
                        UINT scanCode = GetScanCode(vk);
                        LPARAM lParam = 1 | (scanCode << 16)
                                      | ((LPARAM)1 << 30)
                                      | ((LPARAM)1 << 31);
                        if ((vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE)
                            lParam |= ((LPARAM)1 << 24);
                        PostMessage(h, WM_KEYUP, vk, lParam);
                    }
                    else
                    {
                        UINT msg = 0;
                        switch (bit)
                        {
                            case Util::kModMouseL: msg = WM_LBUTTONUP; break;
                            case Util::kModMouseM: msg = WM_MBUTTONUP; break;
                            case Util::kModMouseR: msg = WM_RBUTTONUP; break;
                        }
                        WPARAM wp = 0;
                        if (cur_mods & Util::kModShift) wp |= MK_SHIFT;
                        if (cur_mods & Util::kModCtrl)  wp |= MK_CONTROL;
                        RECT rc{};
                        if (GetClientRect(h, &rc))
                        {
                            LPARAM lp = MAKELPARAM((rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2);
                            PostMessage(h, msg, wp, lp);
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
                    int vk = ModToVk(bit);
                    if (vk)
                    {
                        if (input_count >= 16) flush();
                        inputs[input_count].type = INPUT_KEYBOARD;
                        inputs[input_count].ki.wVk = static_cast<WORD>(vk);
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
                        inputs[input_count].mi.dwFlags = ModToMouseFlag(bit, false);
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