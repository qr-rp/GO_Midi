#pragma once

#include <map>
#include <cstring>
#include <vector>
#include <string>

namespace Util {

    struct KeyMapping {
        int vk_code;
        int modifier; // 0: None, 1: Shift, 2: Ctrl
    };

    class KeyManager {
    public:
        KeyManager();
        
        KeyMapping get_mapping(int note);
        bool load_config(const std::string& path);
        bool save_config(const std::string& path) const;

        void set_map(const std::map<int, KeyMapping>& map);
        const std::map<int, KeyMapping>& get_map() const;
        void reset_to_default();

    private:
        std::map<int, KeyMapping> m_note_map;
        // 优化：O(1) 查找缓存数组（MIDI 音符范围 0-127）
        KeyMapping m_lookup_cache[128]{};
        bool m_lookup_valid[128]{};
        void rebuild_lookup_cache();
        void init_default_map();
        std::string format_key_string(int vk, int modifier) const;
        bool parse_key_string(const std::string& key_str, int& vk, int& modifier) const;
        std::string get_note_name(int midi_pitch) const;
        bool get_pitch_from_name(const std::string& name, int& pitch) const;
    };

}
