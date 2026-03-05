#pragma once

// 标准库
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <fstream>
#include <map>
#include <algorithm>

namespace Midi {

    struct RawNote {
        float start_s;
        int pitch;
        float duration;
        int track_index;
        int channel;
        int velocity;  ///< MIDI 力度 (0-127)
    };

    class MidiTrack {
    public:
        std::string name;
        int note_count{0};

        MidiTrack(std::string name = "") : name(std::move(name)) {}
    };

    class MidiFile {
    public:
#ifdef _WIN32
        MidiFile(const std::wstring& filepath);
#endif
        MidiFile(const std::string& filepath);
        
        std::vector<MidiTrack> tracks;
        float length{0.0f};
        int division{480};
        int format{1};
        
        std::vector<std::vector<RawNote>> raw_notes_by_track;

        double get_initial_bpm() const;
        std::pair<int, int> get_initial_time_signature() const;

    private:
        std::vector<uint8_t> m_data;
        std::vector<std::pair<int, int>> m_tempo_events;  ///< tick, tempo_us
        std::vector<std::pair<int, std::pair<int, int>>> m_time_sig_events;  ///< tick, (nn, dd)
        
        /// 节拍图相关
        std::vector<int> m_tempo_ticks;
        std::vector<double> m_tempo_seconds;
        std::vector<int> m_tempo_values;  ///< 每拍微秒数
        double m_smpte_ticks_per_second{0.0};

        void parse();
        std::pair<uint32_t, size_t> readVarLen(size_t offset);
        double tick_to_seconds(int tick);
        void init_tempo_map(const std::vector<std::pair<int, int>>& tempo_events);
        
        /// 解析单个轨道的辅助结构
        struct TrackParseResult {
            MidiTrack track;
            std::vector<std::tuple<int, int, int, int, int>> notes;  ///< start, end, pitch, ch, velocity
            std::vector<std::pair<int, int>> tempo_events;
            std::vector<std::pair<int, std::pair<int, int>>> time_sig_events;
            int last_tick;
        };
        
        TrackParseResult parse_track(size_t start, size_t len, int track_index);
        
        uint16_t readU16(size_t offset);
        uint32_t readU32(size_t offset);
        std::string decodeText(size_t start, size_t len);

        /// 优化：缓存最后的节拍索引，用于 O(1) 顺序访问
        mutable size_t m_last_tempo_idx{0};
    };

}
