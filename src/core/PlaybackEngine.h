#pragma once

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <set>
#include <memory>
#include <unordered_set>

#include "../midi/MidiParser.h"
#include "KeyboardSimulator.h"
#include "../util/KeyManager.h"
#include "../util/MemoryPool.h"

namespace Core {

    // 哈希函数，用于 ActiveKeySet
    struct ActiveKeyHash {
        size_t operator()(const std::pair<int, void*>& p) const {
            // 使用简单位运算，避免 std::hash 依赖
            size_t h1 = static_cast<size_t>(p.first);
            size_t h2 = reinterpret_cast<size_t>(p.second);
            return h1 ^ (h2 << 16) ^ (h2 >> 16);
        }
    };

    using ActiveKeySet = std::unordered_set<std::pair<int, void*>, ActiveKeyHash>;

    class PlaybackEngine {
    public:
        PlaybackEngine();
        ~PlaybackEngine();

        void load_midi(const Midi::MidiFile& midi_file);
        void play();
        void pause();
        void stop();
        void seek(double time_s);
        
        // Configuration
        void set_speed(double speed);
        // Global transpose removed, use per-channel transpose
        void set_channel_transpose(int channel, int semitones);
        void set_channel_enable(int channel, bool enabled);
        void set_channel_window(int channel, void* hwnd);
        void set_channel_track(int channel, int track_index); // -1 for all tracks
        void set_pitch_range(int min_pitch, int max_pitch);
        void set_decompose(bool decompose);
        void notify_keymap_changed();
        
        bool is_playing() const { return m_playing; }
        bool is_paused() const { return m_paused; }
        double get_current_time() const { return m_current_time; }

        Util::KeyManager& get_key_manager() { return m_key_manager; }

    private:
        struct ProcessedEvent {
            double time;
            bool is_note_on;
            int vk_code;
            int modifier; // Added modifier
            void* window_handle;

            bool operator<(const ProcessedEvent& other) const {
                if (std::abs(time - other.time) > 1e-6)
                    return time < other.time;
                // Note Off (false) before Note On (true) to ensure clean transition
                return is_note_on < other.is_note_on; 
            }
        };

        struct TempNote {
            double start;
            double end;
            int vk;
            int modifier;
            void* hwnd;
            int pitch;
            int track;
        };

        // Channel settings (0-15)
        struct ChannelSettings {
            std::atomic<int> transpose{0};
            std::atomic<bool> enabled{true};
            std::atomic<void*> window_handle{nullptr};
            std::atomic<int> track_index{-1}; // -1 means all tracks
        };

        // 优化：用于 rebuild_events 的配置快照
        struct ValidConfig {
            ChannelSettings* settings;
            bool is_specific_track;
            int target_track;
            bool is_smart_transpose;
        };



        void playback_thread();
        void rebuild_events();
        void release_all_active_keys();

        // 核心数据：持久化持有
        Util::PreallocVector<Midi::RawNote> m_all_notes;
        Util::PreallocVector<ProcessedEvent> m_events;
        
        // Optimization: Cache pitch statistics per track (index = track_idx)
        // Using float for duration-weighted histogram
        std::vector<std::vector<float>> m_track_pitch_histograms;
        
        std::atomic<int> m_config_version{0};   // To trigger rebuild
        int m_built_version{-1};                // Last built version
        bool m_seek_triggered{false};           // Flag to indicate seek occurred

        std::thread m_thread;
        std::atomic<bool> m_running; // Thread running
        std::atomic<bool> m_playing; // Logical playing state
        std::atomic<bool> m_paused;
        std::atomic<double> m_current_time;
        std::atomic<double> m_playback_speed;
        std::atomic<bool> m_decompose{false};
        
        // Audio/MIDI settings
        std::atomic<int> m_min_pitch{48};
        std::atomic<int> m_max_pitch{84};
        
        std::vector<std::unique_ptr<ChannelSettings>> m_channels;
        
        // Active keys for stuck note prevention
        // Pair of <vk_code, window_handle>
        // 使用 unordered_set 实现 O(1) 查找/插入/删除
        ActiveKeySet m_active_keys;
        
        // Synchronization
        std::mutex m_mutex;
        std::condition_variable m_cv;

        KeyboardSimulator m_simulator;
        Util::KeyManager m_key_manager;
        
        double m_total_duration = 0.0;
    };

}