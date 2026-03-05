#pragma once

#include <windows.h>

// 标准库
#include <vector>
#include <string>
#include <utility>

namespace Core {

    class KeyboardSimulator {
    public:
        KeyboardSimulator();
        ~KeyboardSimulator();

        void send_key_down(int vk_code, int modifier = 0, void* hwnd = nullptr);
        void send_key_up(int vk_code, int modifier = 0, void* hwnd = nullptr);
        void send_key_press(int vk_code, int modifier = 0, void* hwnd = nullptr);

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
