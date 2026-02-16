#include "PlaybackEngine.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <cmath>
#include <iostream>
#include "../util/Logger.h"

// 辅助宏：记录函数入口
#define LOG_ENTRY() LOG_DEBUG("[" << __func__ << "] 进入")
#define LOG_EXIT() LOG_DEBUG("[" << __func__ << "] 退出")

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

namespace Core
{

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

        // 优化：使用 PreallocVector 减少分配
        m_all_notes.Clear();

        // 预估大小以减少重分配
        size_t totalNotes = 0;
        for (const auto &track_notes : midi_file.raw_notes_by_track)
        {
            totalNotes += track_notes.size();
        }
        m_all_notes.GetVector().reserve(totalNotes);

        for (const auto &track_notes : midi_file.raw_notes_by_track)
        {
            m_all_notes.GetVector().insert(m_all_notes.GetVector().end(),
                                           track_notes.begin(), track_notes.end());
        }

        // Sort raw notes by start time globally
        std::sort(m_all_notes.GetVector().begin(), m_all_notes.GetVector().end(),
                  [](const Midi::RawNote &a, const Midi::RawNote &b)
                  {
                      return a.start_s < b.start_s;
                  });

        // Optimization: Build pitch histograms
        m_track_pitch_histograms.clear();
        m_track_pitch_histograms.resize(midi_file.raw_notes_by_track.size(), std::vector<int>(128, 0));

        for (const auto &raw : m_all_notes.GetVector())
        {
            if (raw.pitch >= 0 && raw.pitch < 128)
            {
                if (raw.track_index >= 0 && raw.track_index < m_track_pitch_histograms.size())
                {
                    m_track_pitch_histograms[raw.track_index][raw.pitch]++;
                }
            }
        }

        LOG_INFO("MIDI 文件已加载: 音符数=" << m_all_notes.Size()
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
        std::lock_guard<std::mutex> lock(m_mutex);
        m_paused = true;
        release_all_active_keys();
        m_active_keys.clear();
        LOG_INFO("播放暂停，当前时间=" << m_current_time << "s");
    }

    void PlaybackEngine::stop()
    {
        LOG_ENTRY();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_playing = false;
        m_paused = false;
        m_current_time = 0.0;

        // Release all active keys with enhanced cleanup
        release_all_active_keys();

        // Clear active keys list
        m_active_keys.clear();
        m_active_keys.shrink_to_fit(); // Free memory

        // 优化：停止后缩容释放多余内存
        m_all_notes.ShrinkIfNeeded();
        m_events.ShrinkIfNeeded();
        m_temp_notes.ShrinkIfNeeded();

        LOG_INFO("播放停止");
    }

    // Helper method to release all active keys
    void PlaybackEngine::release_all_active_keys()
    {
        // Copy keys to release to minimize lock holding time
        std::vector<std::pair<int, void *>> keys_to_release;
        keys_to_release.swap(m_active_keys);

        // Release keys without holding lock
        for (const auto &key : keys_to_release)
        {
            m_simulator.send_key_up(key.first, 0, key.second);
        }

        // Second pass: safety release common modifier keys in case they got stuck
        // This handles edge cases where modifiers might have been left pressed
        HWND foreground = GetForegroundWindow();
        if (foreground)
        {
            // Release common modifier keys as safety measure
            INPUT safety_inputs[4] = {};
            safety_inputs[0].type = INPUT_KEYBOARD;
            safety_inputs[0].ki.wVk = VK_SHIFT;
            safety_inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;

            safety_inputs[1].type = INPUT_KEYBOARD;
            safety_inputs[1].ki.wVk = VK_CONTROL;
            safety_inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

            safety_inputs[2].type = INPUT_KEYBOARD;
            safety_inputs[2].ki.wVk = VK_MENU; // Alt key
            safety_inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

            safety_inputs[3].type = INPUT_KEYBOARD;
            safety_inputs[3].ki.wVk = VK_LWIN; // Windows key
            safety_inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(4, safety_inputs, sizeof(INPUT));
        }
    }
    void PlaybackEngine::seek(double time_s)
    {
        LOG_DEBUG("跳转播放位置: " << time_s << "s");

        std::lock_guard<std::mutex> lock(m_mutex);
        m_current_time = time_s;
        m_seek_triggered = true;
        if (m_current_time < 0)
            m_current_time = 0;
        if (m_current_time > m_total_duration)
            m_current_time = m_total_duration;

        // Clear active keys on seek
        for (const auto &key : m_active_keys)
        {
            m_simulator.send_key_up(key.first, 0, key.second);
        }
        m_active_keys.clear();

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

        m_events.Clear();
        if (m_all_notes.Empty())
        {
            LOG_DEBUG("音符列表为空，跳过重建");
            return;
        }

        m_temp_notes.Clear();
        m_temp_notes.GetVector().reserve(m_all_notes.Size());
        auto &notes = m_temp_notes.GetVector();

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

        // Pre-filter valid configs to avoid repeated checks in the hot loop
        // 优化：复用成员缓冲区，减少堆分配
        m_valid_configs.clear();

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
            m_valid_configs.push_back(vc);
        }

        // 优化：使用栈数组替代 vector，消除堆分配
        auto compute_best_shift = [&](const std::vector<int> &hist)
        {
            int prefix[129] = {};
            for (int p = 0; p < 128; ++p)
            {
                prefix[p + 1] = prefix[p] + hist[p];
            }
            int scores[9] = {};
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
            int best_score = -1;
            int best_oct_idx = 4;
            for (int i = 0; i < 9; ++i)
            {
                if (scores[i] > best_score)
                {
                    best_score = scores[i];
                    best_oct_idx = i;
                }
                else if (scores[i] == best_score)
                {
                    if (std::abs(i - 4) < std::abs(best_oct_idx - 4))
                    {
                        best_oct_idx = i;
                    }
                }
            }
            return (best_oct_idx - 4) * 12;
        };

        // 优化：复用成员缓冲区
        m_track_best_shifts.assign(m_track_pitch_histograms.size(), 0);
        for (size_t i = 0; i < m_track_pitch_histograms.size(); ++i)
        {
            m_track_best_shifts[i] = compute_best_shift(m_track_pitch_histograms[i]);
        }

        // Optimization: Iterate m_all_notes ONCE (Cache Locality)
        // m_all_notes is already sorted by start time, so we iterate in time order.
        for (const auto &raw : m_all_notes.GetVector())
        {
            for (const auto &vc : m_valid_configs)
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

                // improved smart transpose logic
                if (vc.is_smart_transpose)
                {
                    if (raw.track_index >= 0 && raw.track_index < static_cast<int>(m_track_best_shifts.size()))
                    {
                        transpose += m_track_best_shifts[raw.track_index];
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
        m_events.GetVector().reserve(notes.size() * 2);
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

            // 优化：使用 unordered_map 替代 map，O(1) 查找
            m_active_notes_map.clear();

            for (auto &curr : notes)
            {
                auto key = std::make_pair(curr.hwnd, curr.pitch);
                auto it = m_active_notes_map.find(key);

                if (it != m_active_notes_map.end())
                {
                    resolve(it->second, &curr);
                }

                if (curr.end > curr.start)
                {
                    m_active_notes_map[key] = &curr;
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
        m_events.GetVector().reserve(notes.size() * 2);
        for (const auto &note : notes)
        {
            if (note.end > note.start)
            {
                // Note On 事件
                m_events.GetVector().push_back({note.start, true, note.vk, note.modifier, note.hwnd});
                // Note Off 事件
                m_events.GetVector().push_back({note.end, false, note.vk, note.modifier, note.hwnd});
            }
        }

        // 7. Final Sort
        std::sort(m_events.GetVector().begin(), m_events.GetVector().end());

        LOG_INFO("事件重建完成: 原始音符=" << m_all_notes.Size()
                                           << ", 过滤后音符=" << notes.size()
                                           << ", 事件数=" << m_events.Size());

        // 清理临时缓冲区以释放内存（如果太大）
        m_temp_notes.ShrinkIfNeeded();
        m_events.ShrinkIfNeeded();
    }

    void PlaybackEngine::playback_thread()
    {
#ifdef _WIN32
        // Increase timer resolution for accurate sleep
        timeBeginPeriod(1);
        // Increase thread priority
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        // Set Thread Affinity to the last logical processor to avoid game contention
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        int numProcessors = sysInfo.dwNumberOfProcessors;
        if (numProcessors > 0)
        {
            // Cap at 64 (or 32 on 32-bit systems) to match DWORD_PTR size
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
#endif
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

            while (next_event_idx < m_events.Size())
            {
                const auto &evt = m_events[next_event_idx];

                if (evt.time > m_current_time)
                    break;

                // Fire event
                if (evt.is_note_on)
                {
                    m_simulator.send_key_down(evt.vk_code, evt.modifier, evt.window_handle);

                    // Track active keys properly - include modifier in tracking
                    auto pair = std::make_pair(evt.vk_code, evt.window_handle);

                    // Always add to active keys (don't check for duplicates)
                    // This ensures every NoteOn has a corresponding cleanup
                    m_active_keys.push_back(pair);
                }
                else
                {
                    m_simulator.send_key_up(evt.vk_code, evt.modifier, evt.window_handle);

                    // 优化：从后向前搜索（LIFO），swap-and-pop O(1) 删除
                    auto pair = std::make_pair(evt.vk_code, evt.window_handle);
                    for (int k = static_cast<int>(m_active_keys.size()) - 1; k >= 0; --k)
                    {
                        if (m_active_keys[k] == pair)
                        {
                            if (k != static_cast<int>(m_active_keys.size()) - 1)
                            {
                                m_active_keys[k] = m_active_keys.back();
                            }
                            m_active_keys.pop_back();
                            break;
                        }
                    }
                }

                next_event_idx++;
            }

            // Calculate dynamic sleep time
            double sleep_ms = 15.0; // Default max sleep for UI responsiveness

            if (next_event_idx < m_events.Size())
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

            lock.unlock(); // Unlock before sleep

            if (sleep_ms >= 2.0)
            {
                // Hybrid: Sleep for most of the time, keeping 1ms buffer for precision
                // Windows sleep resolution is ~1-2ms with timeBeginPeriod(1)
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(sleep_ms - 1.5));
            }
            else if (sleep_ms > 0)
            {
                // Busy-wait / Yield for short durations (< 2ms) to avoid oversleeping
                // This ensures high timing accuracy for dense MIDI sections
                auto start_spin = std::chrono::high_resolution_clock::now();
                while (true)
                {
                    auto now = std::chrono::high_resolution_clock::now();
                    std::chrono::duration<double, std::milli> elapsed = now - start_spin;
                    if (elapsed.count() >= sleep_ms)
                        break;
                    std::this_thread::yield();
                }
            }
            else
            {
                std::this_thread::yield();
            }
        }

#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }

}
