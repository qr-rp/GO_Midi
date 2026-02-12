#pragma once

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
    };

    class MidiTrack {
    public:
        std::string name;
        int note_count = 0;

        MidiTrack(std::string name = "") : name(std::move(name)) {}
    };

    class MidiFile {
    public:
#ifdef _WIN32
        MidiFile(const std::wstring& filepath);
#endif
        MidiFile(const std::string& filepath);
        
        std::vector<MidiTrack> tracks;
        float length = 0.0f;
        int division = 480;
        int format = 1;
        
        std::vector<std::vector<RawNote>> raw_notes_by_track;

        double get_initial_bpm() const;
        std::pair<int, int> get_initial_time_signature() const;

    private:
        std::vector<uint8_t> m_data;
        std::vector<std::pair<int, int>> m_tempo_events; // tick, tempo_us
        std::vector<std::pair<int, std::pair<int, int>>> m_time_sig_events; // tick, (nn, dd)
        
        // Tempo map related
        std::vector<int> m_tempo_ticks;
        std::vector<double> m_tempo_seconds;
        std::vector<int> m_tempo_values; // us per beat
        double m_smpte_ticks_per_second = 0.0;

        void parse();
        std::pair<uint32_t, size_t> readVarLen(size_t offset);
        double tick_to_seconds(int tick);
        void init_tempo_map(const std::vector<std::pair<int, int>>& tempo_events);
        
        // Helper to parse a single track
        struct TrackParseResult {
            MidiTrack track;
            std::vector<std::tuple<int, int, int, int>> notes; // start, end, pitch, ch
            std::vector<std::pair<int, int>> tempo_events;
            std::vector<std::pair<int, std::pair<int, int>>> time_sig_events;
            int last_tick;
        };
        
        TrackParseResult parse_track(size_t start, size_t len, int track_index);
        
        uint16_t readU16(size_t offset);
        uint32_t readU32(size_t offset);
        std::string decodeText(size_t start, size_t len);

        // Optimization: Cache last tempo index for O(1) sequential access
        mutable size_t m_last_tempo_idx{0};
    };

}
