#pragma once

#include <windows.h>

// 标准库
#include <vector>
#include <string>
#include <utility>
#include <unordered_map>

namespace Core {

    /// 单个按键事件（A2 批处理用，字段与旧 PlaybackEngine::KeyEvent 一致）
    struct KeyInputEvent {
        bool is_note_on;
        int vk_code;
        int modifier;
        void* window_handle;
    };

    class KeyboardSimulator {
    public:
        KeyboardSimulator();
        ~KeyboardSimulator();

        void send_key_down(int vk_code, int modifier = 0, void* hwnd = nullptr);
        void send_key_up(int vk_code, int modifier = 0, void* hwnd = nullptr);

        /// 批量发送按键（A2 优化）：
        /// - 无目标窗口的按键合并为单次 SendInput（减少系统调用，消除同帧按键错位）
        /// - 有目标窗口的按键保持逐键 PostMessage 语义
        /// - 修饰键与主键同生命周期：Note On 按下修饰保持，Note Off 随引用计数归零释放；
        ///   中途出现不同修饰（含无修饰）的 Note On 时先释放旧修饰再按新修饰，避免干扰
        void send_key_events(const std::vector<KeyInputEvent>& events);

        /// 批量释放按键（按窗口分组，比逐个 send_key_up 更高效）
        void release_keys(const std::vector<std::pair<int, void*>>& keys);

        /// 释放所有保持中的修饰键（stop/seek/pause/播放结束兜底，防粘键）
        void release_all_mods();

        /// 窗口信息结构
        struct WindowInfo {
            HWND hwnd;
            std::string title;
            std::string process_name;
            unsigned long pid;
        };
        static std::vector<WindowInfo> GetWindowList();

    private:
        void send_input(int vk_code, int modifier, bool key_up);

        /// 延迟释放修饰键状态（按目标窗口分组; nullptr 目标用 0 键）
        struct ModState {
            int held = 0;                          ///< 当前物理按住的修饰位
            int refs[6] = {0, 0, 0, 0, 0, 0};      ///< 每个修饰位的活跃音符引用计数
        };
        std::unordered_map<void*, ModState> m_mod_state;
    };

}
