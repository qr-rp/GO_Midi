#pragma once

// 标准库
#include <map>
#include <mutex>
#include <cstring>
#include <vector>
#include <string>

namespace Util {

    // 修饰键位掩码（可组合: Shift+Ctrl / Shift+Alt / 鼠标键+主键 ...）
    enum : int {
        kModNone    = 0,
        kModShift   = 1,  ///< Shift
        kModCtrl    = 2,  ///< Ctrl
        kModAlt     = 4,  ///< Alt
        kModMouseL  = 8,  ///< 鼠标左键（修饰键, 配合主键使用）
        kModMouseM  = 16, ///< 鼠标中键
        kModMouseR  = 32, ///< 鼠标右键
    };

    /// 修饰位中是否含鼠标键
    inline bool has_mouse_mod(int modifier) {
        return (modifier & (kModMouseL | kModMouseM | kModMouseR)) != 0;
    }

    struct KeyMapping {
        int vk_code;
        int modifier;  ///< 位掩码: 1=Shift, 2=Ctrl, 4=Alt, 8=鼠标左, 16=鼠标中, 32=鼠标右 (0=无)
    };

    class KeyManager {
    public:
        KeyManager();
        
        KeyMapping get_mapping(int note);
        
        /// 加载键位配置（宽字符路径）
        bool load_config(const std::wstring& path);
        
        /// 保存键位配置（宽字符路径）
        bool save_config(const std::wstring& path) const;

        void set_map(const std::map<int, KeyMapping>& map);
        /// 返回 map 拷贝（内部加锁，避免并发读写竞争）
        std::map<int, KeyMapping> get_map() const;
        void reset_to_default();
        void load_yysls_preset();

    private:
        std::map<int, KeyMapping> m_note_map;
        /// 优化：O(1) 查找缓存数组（MIDI 音符范围 0-127）
        KeyMapping m_lookup_cache[128]{};
        bool m_lookup_valid[128]{};

        /// 线程安全：播放线程在 rebuild_events 时读 get_mapping，UI 线程在编辑键位时写，
        /// 需互斥保护（缓存数组 + std::map 并发读写是 UB）
        mutable std::mutex m_mutex;
        
        void rebuild_lookup_cache();
        void init_default_map();
        void init_yysls_map();
        std::string format_key_string(int vk, int modifier) const;
        bool parse_key_string(const std::string& key_str, int& vk, int& modifier) const;
        std::string get_note_name(int midi_pitch) const;
        bool get_pitch_from_name(const std::string& name, int& pitch) const;
    };

}