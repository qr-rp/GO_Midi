#pragma once

#ifdef _WIN32
#include <windows.h>
#else
// Define dummy types for non-Windows (though this project is Windows only)
typedef unsigned short WORD;
typedef void* HWND;
#endif

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

        // Helper to map MIDI note to VK code if needed, but UI might handle mapping
        // static int map_note_to_vk(int note);

        // Window Management
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
