#include "PlaybackEngine.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <cmath>
#include <iostream>
#include <windows.h>
#include <mmsystem.h>
#include "../util/Logger.h"

// 辅助宏：记录函数入口
#define LOG_ENTRY() LOG_DEBUG("[" << __func__ << "] 进入")
#define LOG_EXIT() LOG_DEBUG("[" << __func__ << "] 退出")

namespace Core
{
    /// 用于 rebuild_events 的配置快照（局部定义）
    struct ValidConfig {
        ChannelSettings* settings;
        bool is_specific_track;
        int target_track;
        bool is_smart_transpose;
    };

    PlaybackEngine::PlaybackEngine()
        : m_running(false), m_playing(false), m_paused(false),
          m_current_time(0.0), m_playback_speed(1.0)
    {
        LOG_ENTRY();

        // Initialize 16 channels
        for (int i = 0; i < 16; ++i)
        {
            m_channels.push_back(std::make_unique<ChannelSettings>());
        }

        m_running = true;
        m_thread = std::thread(&PlaybackEngine::playback_thread, this);

        LOG_INFO("PlaybackEngine 初始化完成，播放线程已启动");
    }

    PlaybackEngine::~PlaybackEngine()
    {
        LOG_ENTRY();

        stop();
        m_running = false;
        m_cv.notify_all();
        if (m_thread.joinable())
        {
            m_thread.join();
        }

        LOG_INFO("PlaybackEngine 已销毁");
    }

    void PlaybackEngine::load_midi(const Midi::MidiFile &midi_file)
    {
        LOG_ENTRY();

        // Stop playback and clear state before loading new file
        stop();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_playing = false;
        m_paused = false;
        m_current_time = 0.0;
        m_total_duration = midi_file.length;

        m_all_notes.clear();

        // 预估大小以减少重分配
        size_t totalNotes = 0;
        for (const auto &track_notes : midi_file.raw_notes_by_track)
        {
            totalNotes += track_notes.size();
        }

        // 内存管理：如果当前容量远大于新文件所需，释放多余内存
        if (m_all_notes.capacity() > totalNotes * 4) {
            m_all_notes.shrink_to_fit();
        }
        m_all_notes.reserve(totalNotes);

        for (const auto &track_notes : midi_file.raw_notes_by_track)
        {
            m_all_notes.insert(m_all_notes.end(),
                               track_notes.begin(), track_notes.end());
        }

        // Sort raw notes by start time globally
        std::sort(m_all_notes.begin(), m_all_notes.end(),
                  [](const Midi::RawNote &a, const Midi::RawNote &b)
                  {
                      return a.start_s < b.start_s;
                  });

        // Optimization: Build pitch histograms with duration and velocity weighting
        m_track_pitch_histograms.clear();
        m_track_pitch_histograms.resize(midi_file.raw_notes_by_track.size(), std::vector<float>(128, 0.0f));
        m_global_histogram.clear();
        m_global_histogram.resize(128, 0.0f);

        for (const auto &raw : m_all_notes)
        {
            if (raw.pitch >= 0 && raw.pitch < 128)
            {
                if (raw.track_index >= 0 && raw.track_index < m_track_pitch_histograms.size())
                {
                    // sqrt(duration) * velocity: compress extreme duration values while preserving relative importance
                    m_track_pitch_histograms[raw.track_index][raw.pitch] += std::sqrt(raw.duration) * raw.velocity;
                }
                // 全局直方图：排除打击乐（channel 10）和 Bass 乐器（program 33-40）
                if (raw.channel != 10 && (raw.program < 33 || raw.program > 40))
                {
                    m_global_histogram[raw.pitch] += std::sqrt(raw.duration) * raw.velocity;
                }
            }
        }

        // Apply highest pitch weighting to protect melody high points from being transposed out of range
        for (size_t t = 0; t < m_track_pitch_histograms.size(); ++t)
        {
            auto &hist = m_track_pitch_histograms[t];

            // Find highest pitch (topmost non-zero weight)
            int highest_pitch = 60;  // Default to middle C
            for (int p = 127; p >= 0; --p)
            {
                if (hist[p] > 0.0f)
                {
                    highest_pitch = p;
                    break;
                }
            }

            // Apply highest pitch boost: protect melody high points from being transposed out of range
            int start_pitch = highest_pitch - 3;
            if (start_pitch < 0) start_pitch = 0;
            for (int p = start_pitch; p <= highest_pitch; ++p)
            {
                if (hist[p] > 0.0f)
                {
                    float distance = static_cast<float>(highest_pitch - p);
                    float boost = 1.2f - 0.1f * distance;
                    hist[p] *= boost;
                }
            }
        }

        // Apply highest pitch weighting to global histogram
        {
            int highest_pitch = 60;
            for (int p = 127; p >= 0; --p)
            {
                if (m_global_histogram[p] > 0.0f)
                {
                    highest_pitch = p;
                    break;
                }
            }

            int start_pitch = highest_pitch - 3;
            if (start_pitch < 0) start_pitch = 0;
            for (int p = start_pitch; p <= highest_pitch; ++p)
            {
                if (m_global_histogram[p] > 0.0f)
                {
                    float distance = static_cast<float>(highest_pitch - p);
                    float boost = 1.2f - 0.1f * distance;
                    m_global_histogram[p] *= boost;
                }
            }
        }

        LOG_INFO("MIDI 文件已加载: 音符数=" << m_all_notes.size()
                                            << ", 时长=" << m_total_duration << "s"
                                            << ", 音轨数=" << midi_file.raw_notes_by_track.size());

        m_config_version++; // Trigger rebuild
        m_cv.notify_all();
    }

    void PlaybackEngine::play()
    {
        LOG_ENTRY();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_playing = true;
            m_paused = false;
        }
        m_cv.notify_all();
        LOG_INFO("播放开始");
    }

    void PlaybackEngine::pause()
    {
        LOG_ENTRY();

        std::vector<std::pair<int, void *>> keys_to_release;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_paused = true;
            // 将活动按键转移到临时变量，准备释放
            keys_to_release.assign(m_active_keys.begin(), m_active_keys.end());
            m_active_keys.clear();
        }

        // 在锁外释放按键，避免长时间持有锁
        for (const auto &key : keys_to_release)
        {
            m_simulator.send_key_up(key.first, 0, key.second);
        }

        LOG_INFO("播放暂停，当前时间=" << m_current_time << "s");
    }

    void PlaybackEngine::stop()
    {
        LOG_ENTRY();

        std::vector<std::pair<int, void *>> keys_to_release;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_playing = false;
            m_paused = false;
            m_current_time = 0.0;

            // 将活动按键转移到临时变量，准备释放
            keys_to_release.assign(m_active_keys.begin(), m_active_keys.end());
            m_active_keys.clear();
        }

        // 在锁外释放按键，避免阻塞其他线程
        for (const auto &key : keys_to_release)
        {
            m_simulator.send_key_up(key.first, 0, key.second);
        }

        // 额外的安全释放：释放常见修饰键以防卡住
        HWND foreground = GetForegroundWindow();
        if (foreground)
        {
            INPUT safety_inputs[4] = {};
            safety_inputs[0].type = INPUT_KEYBOARD;
            safety_inputs[0].ki.wVk = VK_SHIFT;
            safety_inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;

            safety_inputs[1].type = INPUT_KEYBOARD;
            safety_inputs[1].ki.wVk = VK_CONTROL;
            safety_inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

            safety_inputs[2].type = INPUT_KEYBOARD;
            safety_inputs[2].ki.wVk = VK_MENU;
            safety_inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

            safety_inputs[3].type = INPUT_KEYBOARD;
            safety_inputs[3].ki.wVk = VK_LWIN;
            safety_inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(4, safety_inputs, sizeof(INPUT));
        }

        LOG_INFO("播放停止");
    }

    void PlaybackEngine::seek(double time_s)
    {
        LOG_DEBUG("跳转播放位置: " << time_s << "s");

        std::vector<std::pair<int, void *>> keys_to_release;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_current_time = time_s;
            m_seek_triggered = true;
            if (m_current_time < 0)
                m_current_time = 0;
            if (m_current_time > m_total_duration)
                m_current_time = m_total_duration;

            // Clear active keys on seek - 先复制到临时变量，减少锁持有时间
            keys_to_release.assign(m_active_keys.begin(), m_active_keys.end());
            m_active_keys.clear();
        }

        // 在锁外释放按键，避免长时间持有锁
        for (const auto &key : keys_to_release)
        {
            m_simulator.send_key_up(key.first, 0, key.second);
        }

        LOG_DEBUG("跳转完成，当前位置=" << m_current_time << "s");
    }

    void PlaybackEngine::set_speed(double speed)
    {
        LOG_DEBUG("设置播放速度: " << speed << "x");
        m_playback_speed = speed;
    }

    void PlaybackEngine::set_channel_transpose(int channel, int semitones)
    {
        LOG_DEBUG("设置通道 " << channel << " 移调: " << semitones << " 半音");

        std::lock_guard<std::mutex> lock(m_mutex);
        if (channel >= 0 && channel < 16 && channel < m_channels.size())
        {
            if (m_channels[channel]->transpose != semitones)
            {
                m_channels[channel]->transpose = semitones;
                m_config_version++;
                m_cv.notify_all();
            }
        }
        else
        {
            LOG_WARN("无效的通道编号: " << channel);
        }
    }

    void PlaybackEngine::set_channel_enable(int channel, bool enabled)
    {
        LOG_DEBUG("设置通道 " << channel << " 启用状态: " << (enabled ? "启用" : "禁用"));

        std::lock_guard<std::mutex> lock(m_mutex);
        if (channel >= 0 && channel < 16 && channel < m_channels.size())
        {
            if (m_channels[channel]->enabled != enabled)
            {
                m_channels[channel]->enabled = enabled;
                m_config_version++;
                m_cv.notify_all();
            }
        }
        else
        {
            LOG_WARN("无效的通道编号: " << channel);
        }
    }

    void PlaybackEngine::set_channel_window(int channel, void *hwnd)
    {
        LOG_DEBUG("设置通道 " << channel << " 目标窗口: " << hwnd);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (channel >= 0 && channel < 16 && channel < m_channels.size())
        {
            if (m_channels[channel]->window_handle != hwnd)
            {
                m_channels[channel]->window_handle = hwnd;
                m_config_version++;
                m_cv.notify_all();
            }
        }
        else
        {
            LOG_WARN("无效的通道编号: " << channel);
        }
    }

    void PlaybackEngine::set_channel_track(int channel, int track_index)
    {
        LOG_DEBUG("设置通道 " << channel << " 目标音轨: " << track_index);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (channel >= 0 && channel < 16 && channel < m_channels.size())
        {
            if (m_channels[channel]->track_index != track_index)
            {
                m_channels[channel]->track_index = track_index;
                m_config_version++;
                m_cv.notify_all();
            }
        }
        else
        {
            LOG_WARN("无效的通道编号: " << channel);
        }
    }

    void PlaybackEngine::set_pitch_range(int min_pitch, int max_pitch)
    {
        LOG_DEBUG("设置音域范围: " << min_pitch << " - " << max_pitch);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_min_pitch != min_pitch || m_max_pitch != max_pitch)
        {
            m_min_pitch = min_pitch;
            m_max_pitch = max_pitch;
            m_config_version++;
            m_cv.notify_all();
        }
    }

    void PlaybackEngine::set_decompose(bool decompose)
    {
        LOG_DEBUG("设置分解和弦模式: " << (decompose ? "启用" : "禁用"));

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_decompose != decompose)
        {
            m_decompose = decompose;
            m_config_version++;
            m_cv.notify_all();
        }
    }

    void PlaybackEngine::notify_keymap_changed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config_version++;
        m_cv.notify_all();
    }

    // Helper structures and functions
    // TempNote struct moved to header

    static int clamp_pitch(int pitch, int min_p, int max_p, bool smart)
    {
        int current = pitch;
        if (smart)
        {
            while (current < min_p)
                current += 12;
            while (current > max_p)
                current -= 12;
            if (current < min_p)
                current = min_p;
            if (current > max_p)
                current = max_p;
        }
        else
        {
            // Manual transpose: Do not clamp.
            // If out of range, it will be dropped by key mapping check.
        }
        return current;
    }

    void PlaybackEngine::rebuild_events()
    {
        LOG_DEBUG("重建事件列表");

        m_events.clear();

        // 内存管理：如果容量远大于可能需要的最大值，释放多余内存
        size_t max_events = m_all_notes.size() * 2;
        if (m_events.capacity() > max_events * 4) {
            m_events.shrink_to_fit();
        }

        if (m_all_notes.empty())
        {
            LOG_DEBUG("音符列表为空，跳过重建");
            return;
        }

        // 即用即走：临时音符列表作为局部变量
        std::vector<TempNote> temp_notes;
        temp_notes.reserve(m_all_notes.size());
        auto &notes = temp_notes;

        // 1. Filter and Map
        int total_added = 0;

        std::vector<ChannelSettings *> active_configs;
        for (const auto &ch : m_channels)
        {
            if (ch->enabled)
                active_configs.push_back(ch.get());
        }

        // Fallback: If no channels enabled, use default global config (match Python behavior)
        ChannelSettings default_global;
        if (active_configs.empty())
        {
            default_global.enabled = true;
            default_global.track_index = -1;
            default_global.transpose = 0;
            default_global.window_handle = nullptr;
            active_configs.push_back(&default_global);
        }

        // 即用即走：配置快照作为局部变量
        std::vector<ValidConfig> valid_configs;

        for (auto *ch_config : active_configs)
        {
            if (!ch_config->enabled)
                continue;

            // Fix: In playback mode with multiple channels, require explicit configuration
            if (m_playing && active_configs.size() > 1)
            {
                if (ch_config->window_handle == nullptr && ch_config->track_index == -1)
                {
                    continue;
                }
            }

            ValidConfig vc;
            vc.settings = ch_config;
            vc.target_track = ch_config->track_index;
            vc.is_specific_track = (vc.target_track != -1);
            vc.is_smart_transpose = (ch_config->transpose == 0);
            valid_configs.push_back(vc);
        }

        // 优化：使用栈数组替代 vector，消除堆分配
        // 使用 float 支持时值加权直方图
        // 八度移调（模12），保持和弦性质不变
        auto compute_best_shift = [&](const std::vector<float> &hist)
        {
            float prefix[129] = {};
            for (int p = 0; p < 128; ++p)
            {
                prefix[p + 1] = prefix[p] + hist[p];
            }
            
            // 尝试 -4 到 +4 八度的移调（保持和弦性质）
            float scores[9] = {};
            for (int oct = -4; oct <= 4; ++oct)
            {
                int shift = oct * 12;
                int low = m_min_pitch - shift;
                int high = m_max_pitch - shift;
                if (low < 0)
                    low = 0;
                if (high > 127)
                    high = 127;
                if (low <= high)
                {
                    scores[oct + 4] += prefix[high + 1] - prefix[low];
                }
            }
            
            float best_score = -1.0f;
            int best_oct_idx = 4; // 默认不移调
            for (int i = 0; i < 9; ++i)
            {
                if (scores[i] > best_score)
                {
                    best_score = scores[i];
                    best_oct_idx = i;
                }
                else if (scores[i] == best_score)
                {
                    // 相同分数时选择绝对值较小的移调
                    if (std::abs(i - 4) < std::abs(best_oct_idx - 4))
                    {
                        best_oct_idx = i;
                    }
                }
            }
            return (best_oct_idx - 4) * 12;
        };

        // 计算移调值：支持混合模式
        // - 特定音轨配置：使用该音轨独立计算的移调值
        // - 全部音轨配置：使用全局直方图计算的统一移调值（相对移调，保持音轨音高关系）
        std::vector<int> track_best_shifts(m_track_pitch_histograms.size(), 0);
        int global_shift = 0;

        // 先检查配置类型，按需计算
        bool has_global_config = false;
        bool has_specific_track_config = false;
        for (const auto &vc : valid_configs)
        {
            if (vc.is_specific_track)
                has_specific_track_config = true;
            else
                has_global_config = true;
        }

        // 仅在有特定音轨配置时才计算独立移调值
        if (has_specific_track_config)
        {
            for (size_t i = 0; i < m_track_pitch_histograms.size(); ++i)
            {
                track_best_shifts[i] = compute_best_shift(m_track_pitch_histograms[i]);
            }
        }

        // 全局配置：使用预计算的全局直方图计算统一移调值
        if (has_global_config)
        {
            global_shift = compute_best_shift(m_global_histogram);
        }

        // Optimization: Iterate m_all_notes ONCE (Cache Locality)
        // m_all_notes is already sorted by start time, so we iterate in time order.
        for (const auto &raw : m_all_notes)
        {
            for (const auto &vc : valid_configs)
            {
                // Track Filter logic
                if (vc.is_specific_track)
                {
                    if (raw.track_index != vc.target_track)
                        continue;
                }
                else
                {
                    // Global config: Skip percussion (Channel 10)
                    if (raw.channel == 10)
                        continue;
                }

                int transpose = vc.settings->transpose;

                // 智能移调：特定音轨用独立移调，全部音轨用统一移调
                if (vc.is_smart_transpose)
                {
                    if (vc.is_specific_track)
                    {
                        // 特定音轨：使用该音轨独立计算的移调值
                        if (raw.track_index >= 0 && raw.track_index < static_cast<int>(track_best_shifts.size()))
                        {
                            transpose += track_best_shifts[raw.track_index];
                        }
                    }
                    else
                    {
                        // 全部音轨：使用全局统一移调值（相对移调，保持音轨音高关系）
                        transpose += global_shift;
                    }
                }

                int raw_pitch = raw.pitch + transpose;

                int current_pitch = clamp_pitch(raw_pitch, m_min_pitch, m_max_pitch, vc.is_smart_transpose);

                // Moved mapping to later stage

                notes.push_back({raw.start_s,
                                 raw.start_s + raw.duration,
                                 0, // vk placeholder
                                 0, // modifier placeholder
                                 vc.settings->window_handle,
                                 current_pitch,
                                 raw.track_index});
                total_added++;
            }
        }

        if (notes.empty())
            return;

        // Pre-allocate for On + Off events
        m_events.reserve(notes.size() * 2);
        {
            // Removed MIN_GAP to allow perfect legato (NoteOff at t, NoteOn at t)
            // Sorting ensures NoteOff comes before NoteOn at same timestamp.

            // Helpers for conflict resolution
            auto resolve = [&](TempNote *prev, TempNote *curr)
            {
                if (prev->pitch != curr->pitch)
                    return; // Only resolve same pitch

                // 0. Exact overlap check
                if (std::abs(prev->start - curr->start) < 1e-5 &&
                    std::abs(prev->end - prev->start - (curr->end - curr->start)) < 1e-5)
                {
                    curr->end = curr->start - 1.0; // Mark invalid
                    return;
                }

                // 1. If start times are too close (basically simultaneous), ensure order
                if (curr->start < prev->start)
                {
                    curr->start = prev->start; // Should be handled by sort, but safety
                }

                // 2. Process containment
                if (prev->end > curr->end)
                {
                    curr->end = prev->end;
                }

                // 3. Truncate previous note to avoid overlap
                if (prev->end > curr->start)
                {
                    prev->end = curr->start;
                }
            };

            // 即用即走：使用局部 unordered_map，O(1) 查找
            struct PairHash {
                size_t operator()(const std::pair<void*, int>& p) const {
                    return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(p.first)) ^
                           (std::hash<int>()(p.second) << 16);
                }
            };
            std::unordered_map<std::pair<void*, int>, TempNote*, PairHash> active_notes_map;

            for (auto &curr : notes)
            {
                auto key = std::make_pair(curr.hwnd, curr.pitch);
                auto it = active_notes_map.find(key);

                if (it != active_notes_map.end())
                {
                    resolve(it->second, &curr);
                }

                if (curr.end > curr.start)
                {
                    active_notes_map[key] = &curr;
                }
            }
        }

        // 4. Decompose Logic (Run last if enabled)
        if (m_decompose)
        {
            const double CHORD_THRESHOLD = 0.03;
            const double STAGGER = 0.015;

            std::unordered_map<void *, std::vector<TempNote>> grouped;
            for (const auto &n : notes)
            {
                if (n.end > n.start)
                {
                    grouped[n.hwnd].push_back(n);
                }
            }

            std::vector<TempNote> mono_notes;
            mono_notes.reserve(notes.size());

            for (auto &group : grouped)
            {
                auto &g = group.second;
                std::sort(g.begin(), g.end(), [](const TempNote &a, const TempNote &b)
                          { return a.start < b.start; });

                size_t i = 0;
                while (i < g.size())
                {
                    size_t j = i + 1;
                    while (j < g.size() && (g[j].start - g[i].start) < CHORD_THRESHOLD)
                    {
                        j++;
                    }

                    if (j - i > 1)
                    {
                        std::sort(g.begin() + i, g.begin() + j, [](const TempNote &a, const TempNote &b)
                                  { return a.pitch < b.pitch; });

                        for (size_t k = 1; k < j - i; ++k)
                        {
                            double shift = k * STAGGER;
                            g[i + k].start += shift;
                            g[i + k].end += shift;
                        }
                    }

                    i = j;
                }

                std::sort(g.begin(), g.end(), [](const TempNote &a, const TempNote &b)
                          { return a.start < b.start; });

                for (size_t k = 0; k + 1 < g.size(); ++k)
                {
                    // Enforce gap between notes in monophonic mode
                    // Removed MONO_GAP to allow perfect legato
                    double max_end = g[k + 1].start;
                    if (g[k].end > max_end)
                    {
                        g[k].end = max_end;
                    }
                }

                for (const auto &n : g)
                {
                    if (n.end > n.start)
                    {
                        mono_notes.push_back(n);
                    }
                }
            }

            notes = std::move(mono_notes);
        }

        // 5. Key Mapping (Moved to end)
        int dropped_mapping_late = 0;
        for (auto &note : notes)
        {
            if (note.end <= note.start)
                continue; // Skip invalid notes

            auto mapping = m_key_manager.get_mapping(note.pitch);
            if (mapping.vk_code == 0)
            {
                dropped_mapping_late++;
                note.end = note.start; // Mark as invalid
                continue;
            }
            note.vk = mapping.vk_code;
            note.modifier = mapping.modifier;
        }

        if (dropped_mapping_late > 0)
        {
            LOG_WARN("键位映射丢弃统计: 丢弃数量=" << dropped_mapping_late);
        }

        // 6. Generate Events
        m_events.reserve(notes.size() * 2);
        for (const auto &note : notes)
        {
            if (note.end > note.start)
            {
                // Note On 事件
                m_events.push_back({note.start, true, note.vk, note.modifier, note.hwnd});
                // Note Off 事件
                m_events.push_back({note.end, false, note.vk, note.modifier, note.hwnd});
            }
        }

        // 7. Final Sort
        std::sort(m_events.begin(), m_events.end());

        LOG_INFO("事件重建完成: 原始音符=" << m_all_notes.size()
                                           << ", 过滤后音符=" << notes.size()
                                           << ", 事件数=" << m_events.size());
    }

    void PlaybackEngine::playback_thread()
    {
        // 提高定时器精度以获得准确的 sleep
        timeBeginPeriod(1);
        // 提高线程优先级
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        // 设置线程亲和性到最后一个逻辑处理器，避免与游戏争抢 CPU
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        int numProcessors = sysInfo.dwNumberOfProcessors;
        if (numProcessors > 0)
        {
            // 限制在 64 位（或 32 位系统的 32 位）以匹配 DWORD_PTR 大小
            int maxBits = sizeof(DWORD_PTR) * 8;
            int cpuIdx = numProcessors - 1;
            if (cpuIdx >= maxBits)
                cpuIdx = maxBits - 1;

            DWORD_PTR mask = (DWORD_PTR)1 << cpuIdx;
            DWORD_PTR result = SetThreadAffinityMask(GetCurrentThread(), mask);
            if (result == 0)
            {
                LOG_WARN("设置线程亲和性失败，错误码: " << GetLastError());
            }
            else
            {
                LOG_DEBUG("播放线程亲和性设置为逻辑处理器 " << cpuIdx);
            }
        }
        size_t next_event_idx = 0;
        auto last_loop_time = std::chrono::high_resolution_clock::now();

        while (m_running)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            // Check for config changes or new file
            if (m_config_version != m_built_version)
            {
                // Save current playback position relative to events?
                // Actually if we rebuild, the indices change.
                // We should re-find the index based on time.
                rebuild_events();
                m_built_version = m_config_version.load();

                // Reset index based on current time (Binary search for efficiency)
                auto it = std::lower_bound(m_events.begin(), m_events.end(), m_current_time.load(),
                                           [](const ProcessedEvent &evt, double time)
                                           {
                                               return evt.time < time;
                                           });
                next_event_idx = std::distance(m_events.begin(), it);
            }

            // Wait if paused or not playing
            while (m_running && (!m_playing || m_paused))
            {
                m_cv.wait(lock);
                last_loop_time = std::chrono::high_resolution_clock::now();

                // If seeked/unpaused, update index
                if (m_config_version != m_built_version)
                {
                    rebuild_events();
                    m_built_version = m_config_version.load();
                }

                // Re-sync index
                next_event_idx = 0;
                // Optimization: Binary search
                auto it = std::lower_bound(m_events.begin(), m_events.end(), m_current_time.load(),
                                           [](const ProcessedEvent &evt, double time)
                                           {
                                               return evt.time < time;
                                           });
                next_event_idx = std::distance(m_events.begin(), it);
            }

            if (!m_running)
                break;

            if (m_seek_triggered)
            {
                m_seek_triggered = false;
                auto it = std::lower_bound(m_events.begin(), m_events.end(), m_current_time.load(),
                                           [](const ProcessedEvent &evt, double time)
                                           {
                                               return evt.time < time;
                                           });
                next_event_idx = std::distance(m_events.begin(), it);
            }

            // Lock is still held here

            // Playback Logic
            auto now = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> dt = now - last_loop_time;
            last_loop_time = now;

            m_current_time = m_current_time.load() + (dt.count() * m_playback_speed);

            // 使用成员变量缓冲区避免重复分配
            m_key_event_buffer.clear();

            while (next_event_idx < m_events.size())
            {
                const auto &evt = m_events[next_event_idx];

                if (evt.time > m_current_time)
                    break;

                // 收集事件
                m_key_event_buffer.push_back({evt.is_note_on, evt.vk_code, evt.modifier, evt.window_handle});

                // 更新 active_keys：使用 unordered_set O(1) 操作
                if (evt.is_note_on)
                {
                    m_active_keys.insert(std::make_pair(evt.vk_code, evt.window_handle));
                }
                else
                {
                    m_active_keys.erase(std::make_pair(evt.vk_code, evt.window_handle));
                }

                next_event_idx++;
            }

            // 在锁外执行按键发送，减少锁持有时间
            lock.unlock();

            for (const auto& evt : m_key_event_buffer)
            {
                if (evt.is_note_on)
                {
                    m_simulator.send_key_down(evt.vk_code, evt.modifier, evt.window_handle);
                }
                else
                {
                    m_simulator.send_key_up(evt.vk_code, evt.modifier, evt.window_handle);
                }
            }

            // Calculate dynamic sleep time
            double sleep_ms = 15.0; // Default max sleep for UI responsiveness

            if (next_event_idx < m_events.size())
            {
                double time_to_next = m_events[next_event_idx].time - m_current_time;
                if (time_to_next > 0)
                {
                    // Convert to wall time
                    double wall_ms = (time_to_next / m_playback_speed) * 1000.0;
                    if (wall_ms < sleep_ms)
                    {
                        sleep_ms = wall_ms;
                    }
                }
                else
                {
                    sleep_ms = 0; // Catch up immediately
                }
            }

            // Ensure minimum sleep to yield CPU, but allow 0 if we are behind
            // if (sleep_ms < 1.0 && sleep_ms > 0.001) sleep_ms = 1.0;

            // 注意：锁已经在上面的事件处理部分释放了

            if (sleep_ms >= 1.0)
            {
                // 使用 sleep_for，Windows 在 timeBeginPeriod(1) 后精度约 1ms
                // 减去 0.5ms 作为安全边界，避免过睡
                double actual_sleep = sleep_ms - 0.5;
                if (actual_sleep > 0)
                {
                    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(actual_sleep));
                }
            }
            else if (sleep_ms > 0)
            {
                // 极短等待（< 1ms）：使用 sleep_for 而非 busy-wait
                // 虽然可能略有过睡，但避免 CPU 空转
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            else
            {
                // 无需等待：yield 让出 CPU
                std::this_thread::yield();
            }
        }

        timeEndPeriod(1);
    }

}
