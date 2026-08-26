#pragma once

#include <windows.h>

// 标准库
#include <vector>
#include <string>
#include <utility>

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
        void send_key_events(const std::vector<KeyInputEvent>& events);

        /// 批量释放按键（按窗口分组，比逐个 send_key_up 更高效）
        void release_keys(const std::vector<std::pair<int, void*>>& keys);

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
    };

}
