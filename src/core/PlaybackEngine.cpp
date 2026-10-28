#include "PlaybackEngine.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <cmath>
#include <iostream>
#include <ranges>
#include <windows.h>
#include <mmsystem.h>
#ifdef max
#undef max  // 消除 Windows SDK min/max 宏，避免与 std::max 冲突
#endif
#ifdef min
#undef min
#endif
#include "../util/Logger.h"

// 辅助宏：记录函数入口
#define LOG_ENTRY() LOG_DEBUG("[" << __func__ << "] 进入")
#define LOG_EXIT() LOG_DEBUG("[" << __func__ << "] 退出")

namespace Core
{
    PlaybackEngine::PlaybackEngine()
        : m_running(false), m_playing(false), m_paused(false),
          m_current_time(0.0), m_playback_speed(1.0)
    {
        LOG_ENTRY();

        // Initialize 8 channels (UI 侧 8 个通道卡片, 对齐避免幽灵通道)
        for (int i = 0; i < 8; ++i)
        {
            m_channels.push_back(std::make_unique<ChannelSettings>());
        }

        m_running = true;
        m_thread = std::jthread([this] { playback_thread(); });

        LOG_INFO("PlaybackEngine 初始化完成，播放线程已启动");
    }

    PlaybackEngine::~PlaybackEngine()
    {
        LOG_ENTRY();

        shutdown();

        LOG_INFO("PlaybackEngine 已销毁");
    }

    void PlaybackEngine::shutdown()
    {
        LOG_ENTRY();

        if (!m_running.load())
        {
            LOG_WARN("PlaybackEngine::shutdown() 被多次调用，跳过");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_playing = false;
            m_paused = false;
        }

        m_running = false;
        m_cv.notify_all();

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void PlaybackEngine::load_midi(const Midi::MidiFile &midi_file)
    {
        LOG_ENTRY();

        // Stop playback and clear state before loading new file
        stop();

        std::lock_guard<std::mutex> lock(m_mutex);
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
        std::ranges::sort(m_all_notes,
                          [](const Midi::RawNote& left, const Midi::RawNote& right)
        {
            return left.start_s < right.start_s;
        });

        auto histograms = build_transpose_histograms(
            m_all_notes, midi_file.raw_notes_by_track.size());
        m_track_pitch_histograms = std::move(histograms.tracks);
        m_global_histogram = std::move(histograms.global);

        LOG_INFO("MIDI 文件已加载: 音符数=" << m_all_notes.size()
                                            << ", 时长=" << m_total_duration << "s"
                                            << ", 音轨数=" << midi_file.raw_notes_by_track.size());

        m_config_version++; // Trigger rebuild
        m_all_notes_generation.fetch_add(1, std::memory_order_release);
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

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_paused = true;
        }

        // 唤醒播放线程，让 A1 的可中断睡眠立即响应暂停
        m_cv.notify_all();

        release_all_keys();
        m_simulator.release_all_mods();

        LOG_INFO("播放暂停，当前时间=" << m_current_time << "s");
    }

    void PlaybackEngine::stop()
    {
        LOG_ENTRY();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_playing = false;
            m_paused = false;
            m_current_time = 0.0;
        }

        // 唤醒播放线程，让 A1 的可中断睡眠立即响应停止
        m_cv.notify_all();

        release_all_keys();
        m_simulator.release_all_mods();

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

    void PlaybackEngine::release_all_keys()
    {
        std::vector<std::pair<int, void *>> keys;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_active_keys.empty())
                return;
            keys.reserve(m_active_keys.size());
            for (const auto &[k, count] : m_active_keys)
                keys.push_back(k);
            m_active_keys.clear();
        }
        m_simulator.release_keys(keys);
    }

    PlannerConfig PlaybackEngine::make_planner_config_snapshot()
    {
        PlannerConfig config;
        config.min_pitch = m_min_pitch.load();
        config.max_pitch = m_max_pitch.load();
        config.decompose = m_decompose.load();
        config.smart_algorithm = SmartTransposeAlgorithm::CONFLICT_AWARE;
        config.conflict_key_mode = ConflictKeyMode::PHYSICAL;

        for (std::size_t i = 0; i < m_channels.size(); ++i)
        {
            PlannerChannelConfig channel;
            channel.channel_index = static_cast<int>(i);
            channel.track_index = m_channels[i]->track_index.load();
            channel.transpose = m_channels[i]->transpose.load();
            channel.enabled = m_channels[i]->enabled.load();
            channel.window_handle = m_channels[i]->window_handle.load();
            config.channels.push_back(channel);
        }

        const auto keymap_snapshot = m_key_manager.get_map();
        for (const auto& [pitch, mapping] : keymap_snapshot)
            config.keymap[pitch] = {mapping.vk_code, mapping.modifier};
        return config;
    }

    bool PlaybackEngine::try_rebuild_events(std::unique_lock<std::mutex>& lock)
    {
        // 在锁内快照全部共享数据，确保一次计划只使用同一个配置版本。
        std::vector<Midi::RawNote> notes_snapshot = m_all_notes;
        std::vector<std::vector<float>> track_hists_snapshot = m_track_pitch_histograms;
        std::vector<float> global_hist_snapshot = m_global_histogram;
        int notes_gen = m_all_notes_generation.load(std::memory_order_acquire);
        int config_version = m_config_version.load(std::memory_order_acquire);
        PlannerConfig plan_config = make_planner_config_snapshot();

        lock.unlock();

        PlaybackPlan plan = build_playback_plan(
            notes_snapshot, track_hists_snapshot, global_hist_snapshot, plan_config);

        lock.lock();
        if (m_all_notes_generation.load(std::memory_order_acquire) == notes_gen &&
            m_config_version.load(std::memory_order_acquire) == config_version)
        {
            if (!plan.ok)
            {
                LOG_ERROR("智能移调计划生成失败，保留上一版事件");
                return false;
            }

            m_events = std::move(plan.events);
            m_built_version = config_version;

            const double retention = plan.metrics.source_duration_weight > 0.0
                ? plan.metrics.retained_duration_weight * 100.0 /
                      plan.metrics.source_duration_weight
                : 100.0;
            LOG_INFO("播放计划完成: 全局移调=" << plan.global_shift
                     << " 半音, 映射=" << plan.metrics.mapped_notes
                     << ", 冲突=" << plan.metrics.conflict_merges
                     << ", 精确重复=" << plan.metrics.exact_duplicates
                     << ", 回卷=" << plan.metrics.folded_notes
                     << ", 保留时值=" << retention << "%");
            return true;
        }

        // MIDI 或配置在锁外生成计划期间发生变化，丢弃旧计划。
        return false;
    }

    void PlaybackEngine::seek(double time_s)
    {
        LOG_DEBUG("跳转播放位置: " << time_s << "s");

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_current_time = time_s;
            m_seek_triggered = true;
            if (m_current_time < 0)
                m_current_time = 0;
            if (m_current_time > m_total_duration)
                m_current_time = m_total_duration;
        }

        // 唤醒播放线程，让 A1 的可中断睡眠立即响应跳转
        m_cv.notify_all();

        release_all_keys();
        m_simulator.release_all_mods();

        LOG_DEBUG("跳转完成，当前位置=" << m_current_time << "s");
    }

    void PlaybackEngine::set_speed(double speed)
    {
        LOG_DEBUG("设置播放速度: " << speed << "x");
        m_playback_speed = speed;
    }

    bool PlaybackEngine::is_valid_channel(int channel) const
    {
        return channel >= 0 &&
               channel < 8 &&
               static_cast<std::size_t>(channel) < m_channels.size();
    }

    void PlaybackEngine::request_plan_rebuild()
    {
        m_config_version++;
        m_cv.notify_all();
    }

    void PlaybackEngine::set_channel_transpose(int channel, int semitones)
    {
        LOG_DEBUG("设置通道 " << channel << " 移调: " << semitones << " 半音");

        std::lock_guard<std::mutex> lock(m_mutex);
        if (is_valid_channel(channel))
        {
            if (m_channels[channel]->transpose != semitones)
            {
                m_channels[channel]->transpose = semitones;
                // 未启用通道配置变化无需重建(不参与播放), 启用时 set_channel_enable 会触发
                if (m_channels[channel]->enabled)
                    request_plan_rebuild();
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
        if (is_valid_channel(channel))
        {
            if (m_channels[channel]->enabled != enabled)
            {
                m_channels[channel]->enabled = enabled;
                request_plan_rebuild();
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
        if (is_valid_channel(channel))
        {
            if (m_channels[channel]->window_handle != hwnd)
            {
                m_channels[channel]->window_handle = hwnd;
                if (m_channels[channel]->enabled)
                    request_plan_rebuild();
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
        if (is_valid_channel(channel))
        {
            if (m_channels[channel]->track_index != track_index)
            {
                m_channels[channel]->track_index = track_index;
                if (m_channels[channel]->enabled)
                    request_plan_rebuild();
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
            request_plan_rebuild();
        }
    }

    void PlaybackEngine::set_decompose(bool decompose)
    {
        LOG_DEBUG("设置分解和弦模式: " << (decompose ? "启用" : "禁用"));

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_decompose != decompose)
        {
            m_decompose = decompose;
            request_plan_rebuild();
        }
    }

    void PlaybackEngine::notify_keymap_changed()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            request_plan_rebuild();
        }
        LOG_DEBUG("键位映射已变更，播放计划等待重建");
    }

    void PlaybackEngine::configure_playback_thread()
    {
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
    }

    std::size_t PlaybackEngine::event_index_at_or_after(double time) const
    {
        const auto it = std::lower_bound(
            m_events.begin(), m_events.end(), time,
            [](const PlaybackEvent& event, double target)
            {
                return event.time < target;
            });
        return static_cast<std::size_t>(std::distance(m_events.begin(), it));
    }

    void PlaybackEngine::playback_thread()
    {
        // 提高定时器精度以获得准确的 sleep
        timeBeginPeriod(1);
        configure_playback_thread();

        size_t next_event_idx = 0;
        auto last_loop_time = std::chrono::steady_clock::now();

        while (m_running)
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            // Check for config changes or new file
            if (m_config_version != m_built_version)
            {
                try_rebuild_events(lock);

                // Reset index based on current time
                next_event_idx = event_index_at_or_after(m_current_time.load());
            }

            // Wait if paused or not playing
            while (m_running && (!m_playing || m_paused))
            {
                m_cv.wait(lock);
                last_loop_time = std::chrono::steady_clock::now();

                // If seeked/unpaused, update index
                if (m_config_version != m_built_version)
                {
                    try_rebuild_events(lock);
                }

                next_event_idx = event_index_at_or_after(m_current_time.load());
            }

            if (!m_running)
                break;

            if (m_seek_triggered)
            {
                m_seek_triggered = false;
                next_event_idx = event_index_at_or_after(m_current_time.load());
                // 重置计时器，避免 seek 后 m_current_time 多跳一个 dt
                last_loop_time = std::chrono::steady_clock::now();
            }

            // Lock is still held here

            // Playback Logic
            auto now = std::chrono::steady_clock::now();
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

                // 更新 active_keys：引用计数，防止同一按键多次 Note On 后单次 Note Off 提前释放
                if (evt.is_note_on)
                {
                    auto [it, inserted] = m_active_keys.try_emplace(std::make_pair(evt.vk_code, evt.window_handle), 1);
                    if (!inserted)
                        it->second++;  // 已存在，引用计数 +1
                }
                else
                {
                    auto it = m_active_keys.find(std::make_pair(evt.vk_code, evt.window_handle));
                    if (it != m_active_keys.end())
                    {
                        if (--it->second == 0)
                            m_active_keys.erase(it);  // 引用计数归零才真正移除
                    }
                }

                next_event_idx++;
            }

            // 在锁外执行按键发送，减少锁持有时间
            lock.unlock();

            // 二次检查：如果在收集事件期间有 stop/pause/seek 清空了按键，
            // 丢弃已收集的陈旧事件，避免发送被 release_all_keys() 盖过的按键
            {
                std::lock_guard<std::mutex> check_lock(m_mutex);
                if (!m_playing || m_paused || m_seek_triggered)
                    m_key_event_buffer.clear();
            }

            // A2: 整帧按键合并为单次批量发送（无目标窗口 → 单次 SendInput，有窗口 → PostMessage）
            if (!m_key_event_buffer.empty())
            {
                m_simulator.send_key_events(m_key_event_buffer);
            }

            // A1: 绝对 deadline + 尾段忙等
            // 计算到下一事件的墙钟等待时间（上限 15ms，保证 UI 轮询到的时间刷新频率）
            double wall_ms_until_next = 15.0;
            if (next_event_idx < m_events.size())
            {
                double time_to_next = m_events[next_event_idx].time - m_current_time;
                if (time_to_next > 0)
                {
                    // Convert to wall time
                    double wall_ms = (time_to_next / m_playback_speed) * 1000.0;
                    if (wall_ms < wall_ms_until_next)
                    {
                        wall_ms_until_next = wall_ms;
                    }
                }
                else
                {
                    wall_ms_until_next = 0.0; // Catch up immediately
                }
            }

            // 尾段忙等时长：wait_for 的唤醒延迟有抖动（可达数 ms），
            // 只靠 -0.5ms 经验补偿是开环修正。改为：睡到 (deadline - 尾段)，
            // 最后一段用 QPC 忙等到绝对 deadline，把音符触发抖动压到微秒级。
            const double kSpinMarginMs = 1.5;

            auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::duration<double, std::milli>(wall_ms_until_next);

            // 使用条件变量实现可中断的睡眠，shutdown()/pause()/stop()/seek() notify 后可立即唤醒线程
            if (wall_ms_until_next > kSpinMarginMs)
            {
                const double sleep_ms = wall_ms_until_next - kSpinMarginMs;
                {
                    std::unique_lock<std::mutex> sleep_lock(m_mutex);
                    m_cv.wait_for(sleep_lock,
                        std::chrono::duration<double, std::milli>(sleep_ms),
                        [this] { return !m_running; });
                }
                if (!m_running)
                    break;

                // 睡眠被暂停/停止/跳转打断：不忙等，直接回到循环顶部处理新状态
                if (!m_playing || m_paused || m_seek_triggered)
                    continue;
            }

            // 尾段忙等至绝对 deadline（≤ kSpinMarginMs，通常 ≤1.5ms），消除唤醒抖动
            while (std::chrono::steady_clock::now() < deadline)
            {
                YieldProcessor();
            }

            if (!m_running)
                break;
        }

        timeEndPeriod(1);
    }

}
