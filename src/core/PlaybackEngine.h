#pragma once

// 标准库
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <set>
#include <memory>
#include <unordered_set>

// 项目头文件
#include "../midi/MidiParser.h"
#include "KeyboardSimulator.h"
#include "../util/KeyManager.h"
#include "../util/MemoryPool.h"

namespace Core {

    /// 哈希函数，用于 ActiveKeySet
    struct ActiveKeyHash {
        size_t operator()(const std::pair<int, void*>& p) const {
            // 采用 boost::hash_combine 风格的混合，减少冲突
            size_t h1 = static_cast<size_t>(p.first);
            size_t h2 = reinterpret_cast<size_t>(p.second);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    /// 按键事件结构（用于播放线程）
    struct KeyEvent {
        bool is_note_on;
        int vk_code;
        int modifier;
        void* window_handle;
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
        
        /// 配置接口
        void set_speed(double speed);
        void set_channel_transpose(int channel, int semitones);
        void set_channel_enable(int channel, bool enabled);
        void set_channel_window(int channel, void* hwnd);
        void set_channel_track(int channel, int track_index);  ///< -1 表示所有轨道
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
            int modifier;
            void* window_handle;

            bool operator<(const ProcessedEvent& other) const {
                if (std::abs(time - other.time) > 1e-6)
                    return time < other.time;
                // Note Off 在 Note On 之前，确保平滑过渡
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

        /// 通道设置 (0-15)
        struct ChannelSettings {
            std::atomic<int> transpose{0};
            std::atomic<bool> enabled{true};
            std::atomic<void*> window_handle{nullptr};
            std::atomic<int> track_index{-1};  ///< -1 表示所有轨道
        };

        /// 用于 rebuild_events 的配置快照
        struct ValidConfig {
            ChannelSettings* settings;
            bool is_specific_track;
            int target_track;
            bool is_smart_transpose;
        };

        void playback_thread();
        void rebuild_events();
        void release_all_active_keys();

        /// 核心数据：持久化持有
        Util::PreallocVector<Midi::RawNote> m_all_notes;
        Util::PreallocVector<ProcessedEvent> m_events;
        
        /// 每轨道的音高统计缓存（用于智能移调）
        std::vector<std::vector<float>> m_track_pitch_histograms;
        
        std::atomic<int> m_config_version{0};   ///< 触发重建的版本号
        int m_built_version{-1};                ///< 最后构建的版本
        bool m_seek_triggered{false};           ///< 跳转触发标志

        std::thread m_thread;
        std::atomic<bool> m_running;            ///< 线程运行状态
        std::atomic<bool> m_playing;            ///< 逻辑播放状态
        std::atomic<bool> m_paused;
        std::atomic<double> m_current_time;
        std::atomic<double> m_playback_speed;
        std::atomic<bool> m_decompose{false};
        
        /// 音频/MIDI 设置
        std::atomic<int> m_min_pitch{48};
        std::atomic<int> m_max_pitch{84};
        
        std::vector<std::unique_ptr<ChannelSettings>> m_channels;
        
        /// 活跃按键集合（防止卡键）
        /// 使用 unordered_set 实现 O(1) 查找/插入/删除
        ActiveKeySet m_active_keys;
        
        /// 事件缓冲区（复用避免重复分配）
        std::vector<KeyEvent> m_key_event_buffer;
        
        /// 同步原语
        std::mutex m_mutex;
        std::condition_variable m_cv;

        KeyboardSimulator m_simulator;
        Util::KeyManager m_key_manager;
        
        double m_total_duration{0.0};
    };

}