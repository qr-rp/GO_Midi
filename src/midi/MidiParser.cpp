#include "MidiParser.h"
#include <array>
#include <cstring>
#include <tuple>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <ranges>
#include "../util/Logger.h"

namespace Midi
{

// 辅助宏：记录函数入口
#define LOG_ENTRY() LOG_DEBUG("[" << __func__ << "] 进入")

    namespace
    {
        template <typename T>
        std::optional<T> find_tick_zero_value(
            const std::vector<std::pair<int, T>>& events)
        {
            for (const auto& [tick, value] : events)
            {
                if (tick == 0)
                    return value;
                if (tick > 0)
                    break;
            }
            return std::nullopt;
        }

        struct ActiveNote
        {
            int start_tick{0};
            int velocity{64};
            int program{0};
        };

        class ActiveNoteTracker
        {
        public:
            void note_on(int key_index, int tick, int velocity, int program)
            {
                const std::size_t index = static_cast<std::size_t>(key_index);
                if (m_depth[index] > 0)
                    m_overflow[key_index].push_back(m_top[index]);

                m_top[index] = {tick, velocity, program};
                ++m_depth[index];
            }

            bool note_off(int key_index, ActiveNote& released)
            {
                const std::size_t index = static_cast<std::size_t>(key_index);
                if (m_depth[index] <= 0)
                    return false;

                released = m_top[index];
                --m_depth[index];
                if (m_depth[index] > 0)
                {
                    auto overflow = m_overflow.find(key_index);
                    if (overflow != m_overflow.end() && !overflow->second.empty())
                    {
                        m_top[index] = overflow->second.back();
                        overflow->second.pop_back();
                        if (overflow->second.empty())
                            m_overflow.erase(overflow);
                    }
                }
                return true;
            }

            template <typename Callback>
            void for_each_open(int end_tick, Callback callback) const
            {
                for (int key_index = 0; key_index < static_cast<int>(m_top.size()); ++key_index)
                {
                    if (m_depth[key_index] <= 0)
                        continue;

                    callback(key_index, m_top[key_index], end_tick);
                    auto overflow = m_overflow.find(key_index);
                    if (overflow == m_overflow.end())
                        continue;
                    for (const auto& note : overflow->second)
                        callback(key_index, note, end_tick);
                }
            }

        private:
            std::array<ActiveNote, 2048> m_top{};
            std::array<std::int8_t, 2048> m_depth{};
            std::unordered_map<int, std::vector<ActiveNote>> m_overflow;
        };
    }

    MidiFile::MidiFile(const std::wstring &filepath)
    {
        LOG_ENTRY();
        LOG_DEBUG("加载 MIDI 文件 (宽字符路径)");

        std::ifstream file(std::filesystem::path(filepath), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            LOG_ERROR("无法打开文件 (宽字符路径)");
            m_error_msg = "无法打开文件 (宽字符路径)";
            return;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        m_data.resize(size);
        if (!file.read(reinterpret_cast<char *>(m_data.data()), size))
        {
            LOG_ERROR("无法读取文件 (宽字符路径)");
            m_error_msg = "无法读取文件 (宽字符路径)";
            return;
        }

        LOG_DEBUG("文件大小: " << size << " 字节");
        m_valid = true;
        if (!parse()) {
            return;
        }
        m_valid = true;
    }

    uint16_t MidiFile::readU16(size_t offset)
    {
        if (!m_valid) return 0;
        if (offset + 2 > m_data.size()) {
            LOG_ERROR("无效的 MIDI 数据偏移量");
            m_valid = false;
            m_error_msg = "无效的 MIDI 数据偏移量";
            return 0;
        }
        return (static_cast<uint16_t>(m_data[offset]) << 8) | m_data[offset + 1];
    }

    uint32_t MidiFile::readU32(size_t offset)
    {
        if (!m_valid) return 0;
        if (offset + 4 > m_data.size()) {
            LOG_ERROR("无效的 MIDI 数据偏移量");
            m_valid = false;
            m_error_msg = "无效的 MIDI 数据偏移量";
            return 0;
        }
        return (static_cast<uint32_t>(m_data[offset]) << 24) |
               (static_cast<uint32_t>(m_data[offset + 1]) << 16) |
               (static_cast<uint32_t>(m_data[offset + 2]) << 8) |
               m_data[offset + 3];
    }

    std::pair<uint32_t, size_t> MidiFile::readVarLen(size_t offset)
    {
        if (!m_valid) return {0, 0};
        size_t n = m_data.size();
        if (offset >= n) {
            LOG_ERROR("MIDI 数据意外结束");
            m_valid = false;
            m_error_msg = "MIDI 数据意外结束";
            return {0, 0};
        }

        uint8_t b0 = m_data[offset];
        if ((b0 & 0x80) == 0)
        {
            return {b0, offset + 1};
        }

        uint32_t value = b0 & 0x7F;
        offset++;
        for (int i = 0; i < 3; ++i)
        {
            if (offset >= n) {
                LOG_ERROR("MIDI 数据意外结束");
                m_valid = false;
                m_error_msg = "MIDI 数据意外结束";
                return {0, 0};
            }
            uint8_t b = m_data[offset];
            value = (value << 7) | (b & 0x7F);
            offset++;
            if ((b & 0x80) == 0)
            {
                return {value, offset};
            }
        }

        if (offset >= n) {
            LOG_ERROR("MIDI 数据意外结束");
            m_valid = false;
            m_error_msg = "MIDI 数据意外结束";
            return {0, 0};
        }
        uint8_t b = m_data[offset];
        value = (value << 7) | (b & 0x7F);
        offset++;
        if ((b & 0x80) != 0)
        {
            LOG_ERROR("变长数值过长");
            m_valid = false;
            m_error_msg = "变长数值过长";
            return {0, 0};
        }
        return {value, offset};
    }

    std::string MidiFile::decodeText(size_t start, size_t len)
    {
        if (start + len > m_data.size())
            return "";
        return std::string(reinterpret_cast<char *>(m_data.data() + start), len);
    }

    bool MidiFile::parse()
    {
        LOG_ENTRY();

        if (m_data.size() < 14)
        {
            LOG_ERROR("无效的 MIDI 文件: 文件太小");
            m_valid = false;
            m_error_msg = "无效的 MIDI 文件: 文件太小";
            return false;
        }
        if (memcmp(m_data.data(), "MThd", 4) != 0)
        {
            LOG_ERROR("无效的 MIDI 文件: 缺少 MThd 头");
            m_valid = false;
            m_error_msg = "无效的 MIDI 文件: 缺少 MThd 头";
            return false;
        }

        uint32_t header_len = readU32(4);
        if (!m_valid) return false;
        if (header_len < 6)
        {
            LOG_ERROR("无效的 MIDI 头长度: " << header_len);
            m_valid = false;
            m_error_msg = "无效的 MIDI 头长度";
            return false;
        }

        format = readU16(8);
        if (!m_valid) return false;
        int track_count = readU16(10);
        if (!m_valid) return false;
        division = readU16(12);
        if (!m_valid) return false;

        LOG_DEBUG("MIDI 格式: " << format << ", 音轨数: " << track_count << ", 分辨率: " << division);

        // SMPTE Handling
        if ((division & 0x8000) != 0)
        {
            int fps = -static_cast<int8_t>((division >> 8) & 0xFF);
            int ticks_per_frame = division & 0xFF;
            double fps_val = (double)fps;
            // MIDI standard: 29 represents 29.97 df
            if (fps == 29)
                fps_val = 29.97;
            m_smpte_ticks_per_second = fps_val * ticks_per_frame;
        }
        else
        {
            m_smpte_ticks_per_second = 0.0;
        }

        size_t pos = 8 + header_len;

        std::vector<std::pair<int, int>> all_tempo_events;
        std::vector<std::pair<int, std::pair<int, int>>> all_time_sig_events;
        std::vector<std::vector<std::tuple<int, int, int, int, int, int>>> parsed_notes_by_track; // start, end, pitch, ch, velocity, program
        int last_tick_global = 0;

        for (int i = 0; i < track_count; ++i)
        {
            if (pos + 8 > m_data.size())
            {
                LOG_WARN("音轨数据不完整，已解析 " << i << "/" << track_count << " 个音轨");
                break;
            }
            if (memcmp(m_data.data() + pos, "MTrk", 4) != 0)
            {
                LOG_ERROR("无效的音轨块头，位置: " << pos);
                m_valid = false;
                m_error_msg = "无效的音轨块头";
                return false;
            }

            uint32_t chunk_len = readU32(pos + 4);
            if (!m_valid) return false;
            size_t chunk_start = pos + 8;
            size_t chunk_end = chunk_start + chunk_len;
            if (chunk_end > m_data.size())
            {
                LOG_ERROR("音轨块长度无效: " << chunk_len << ", 位置: " << pos);
                m_valid = false;
                m_error_msg = "音轨块长度无效";
                return false;
            }

            auto res = parse_track(chunk_start, chunk_len, i);
            if (!m_valid) return false;
            tracks.push_back(std::move(res.track));
            parsed_notes_by_track.push_back(std::move(res.notes));
            all_tempo_events.insert(all_tempo_events.end(), res.tempo_events.begin(), res.tempo_events.end());
            all_time_sig_events.insert(all_time_sig_events.end(), res.time_sig_events.begin(), res.time_sig_events.end());

            LOG_DEBUG("音轨 " << i << " (" << tracks.back().name << "): "
                              << res.notes.size() << " 个音符, " << res.tempo_events.size() << " 个节奏事件");

            if (res.last_tick > last_tick_global)
            {
                last_tick_global = res.last_tick;
            }

            pos = chunk_end;
        }

        init_tempo_map(all_tempo_events);

        double max_end = 0.0;
        for (size_t i = 0; i < parsed_notes_by_track.size(); ++i)
        {
            std::vector<RawNote> track_raw_notes;
            const auto &notes = parsed_notes_by_track[i];
            for (const auto &note : notes)
            {
                int start_tick = std::get<0>(note);
                int end_tick = std::get<1>(note);
                int pitch = std::get<2>(note);
                int ch = std::get<3>(note);
                int vel = std::get<4>(note);
                int prog = std::get<5>(note);

                double start_s = tick_to_seconds(start_tick);
                double end_s = tick_to_seconds(end_tick);
                double dur = end_s - start_s;
                if (dur < 0.0)
                    dur = 0.0;
                if (end_s > max_end)
                    max_end = end_s;

                RawNote rn;
                rn.start_s = static_cast<float>(start_s);
                rn.pitch = pitch;
                rn.duration = static_cast<float>(dur);
                rn.track_index = static_cast<int>(i);
                rn.channel = ch;
                rn.velocity = vel;
                rn.program = prog;
                track_raw_notes.push_back(rn);
            }
            raw_notes_by_track.push_back(std::move(track_raw_notes));
        }

        if (max_end <= 0.0 && last_tick_global > 0)
        {
            length = static_cast<float>(tick_to_seconds(last_tick_global));
        }
        else
        {
            length = static_cast<float>(max_end);
        }

        m_tempo_events = all_tempo_events;
        std::ranges::sort(m_tempo_events);

        m_time_sig_events = all_time_sig_events;
        std::ranges::sort(m_time_sig_events);

        // Optimization: Clear raw data to free memory
        m_data.clear();
        m_data.shrink_to_fit();

        // 统计总音符数
        size_t total_notes = 0;
        for (const auto &track_notes : raw_notes_by_track)
        {
            total_notes += track_notes.size();
        }

        LOG_INFO("MIDI 解析完成: 格式=" << format
                                        << ", 音轨数=" << tracks.size()
                                        << ", 总音符=" << total_notes
                                        << ", 时长=" << length << "s"
                                        << ", 初始BPM=" << get_initial_bpm());
        return true;
    }

    bool MidiFile::parse_meta_event(size_t& position, size_t track_end, int abs_tick,
                                    TrackParseResult& result, bool& end_of_track)
    {
        end_of_track = false;
        if (position + 1 > track_end)
            return false;

        const uint8_t meta_type = m_data[position++];
        const auto length_info = readVarLen(position);
        if (!m_valid)
            return false;

        position = length_info.second;
        const size_t meta_end = position + length_info.first;
        if (meta_end > track_end)
            return false;

        if (meta_type == 0x2F)
        {
            end_of_track = true;
        }
        else if (meta_type == 0x03)
        {
            result.track.name = decodeText(position, length_info.first);
        }
        else if (meta_type == 0x51 && length_info.first == 3)
        {
            const int tempo_us =
                (m_data[position] << 16) |
                (m_data[position + 1] << 8) |
                m_data[position + 2];
            result.tempo_events.emplace_back(abs_tick, tempo_us);
        }
        else if (meta_type == 0x58 && length_info.first >= 4)
        {
            const int numerator = m_data[position];
            const int denominator = 1 << m_data[position + 1];
            result.time_sig_events.emplace_back(
                abs_tick, std::make_pair(numerator, denominator));
        }

        position = meta_end;
        return true;
    }

    MidiFile::TrackParseResult MidiFile::parse_track(size_t start, size_t len, int track_index)
    {
        TrackParseResult res;
        res.track.name = "";
        if (!m_valid) {
            return res;
        }

        // Estimate capacity to avoid reallocations (approx 4 bytes per event)
        res.notes.reserve(len / 4);

        size_t pos = start;
        size_t end_pos = start + len;
        int abs_tick = 0;
        uint8_t running_status = 0;

        // Track program per channel (MIDI channels 1-16, indexed 0-15)
        int channel_program[16];
        for (int i = 0; i < 16; ++i) channel_program[i] = 0;  // Default to piano

        ActiveNoteTracker active_notes;
        auto release_note = [&](int key_index, int pitch, int channel)
        {
            ActiveNote released;
            if (active_notes.note_off(key_index, released))
            {
                res.notes.emplace_back(
                    released.start_tick,
                    abs_tick,
                    pitch,
                    channel,
                    released.velocity,
                    released.program);
            }
        };

        while (pos < end_pos)
        {
            auto vl = readVarLen(pos);
            if (!m_valid) break;
            int delta = static_cast<int>(vl.first);
            pos = vl.second;
            abs_tick += delta;

            if (pos >= end_pos)
                break;

            uint8_t status = m_data[pos];
            if (status < 0x80)
            {
                if (running_status == 0)
                    break;
                status = running_status;
            }
            else
            {
                pos++;
                running_status = status;
            }

            if (status == 0xFF)
            { // Meta event
                bool end_of_track = false;
                if (!parse_meta_event(pos, end_pos, abs_tick, res, end_of_track))
                    break;
                if (end_of_track)
                    break;
                continue;
            }

            if (status == 0xF0 || status == 0xF7)
            { // SysEx
                auto vl_sysex = readVarLen(pos);
                if (!m_valid) break;
                pos = vl_sysex.second + vl_sysex.first;
                running_status = 0;
                continue;
            }

            uint8_t event_type = status & 0xF0;
            uint8_t channel0 = status & 0x0F;
            int channel = channel0 + 1;

            if (event_type == 0x90)
            { // Note On
                if (pos + 2 > end_pos)
                    break;
                int pitch = m_data[pos];
                int vel = m_data[pos + 1];
                pos += 2;

                int key_idx = channel0 * 128 + pitch;
                if (vel == 0)
                { // Note Off via Note On with vel=0
                    release_note(key_idx, pitch, channel);
                }
                else
                {
                    active_notes.note_on(
                        key_idx, abs_tick, vel, channel_program[channel0]);
                    res.track.note_count++;
                }
                continue;
            }

            if (event_type == 0x80)
            { // Note Off
                if (pos + 2 > end_pos)
                    break;
                int pitch = m_data[pos];
                // vel = m_data[pos + 1];
                pos += 2;
                int key_idx = channel0 * 128 + pitch;
                release_note(key_idx, pitch, channel);
                continue;
            }

            if (event_type == 0xA0 || event_type == 0xB0 || event_type == 0xE0)
            {
                pos += 2;
                continue;
            }

            if (event_type == 0xC0)
            { // Program Change
                if (pos + 1 > end_pos)
                    break;
                int program = m_data[pos];
                pos += 1;
                channel_program[channel0] = program;
                continue;
            }

            if (event_type == 0xD0)
            { // Channel Pressure
                pos += 1;
                continue;
            }

            // Unknown, try skip 1 byte? or fail. Python version skips 1 if pos < n
            if (pos < end_pos)
            {
                pos++;
            }
        }

        if (res.track.name.empty())
        {
            res.track.name = "Track " + std::to_string(track_index);
        }

        // 关闭未结束的音符
        active_notes.for_each_open(abs_tick,
            [&](int key_index, const ActiveNote& note, int end_tick)
        {
            const int channel = (key_index / 128) + 1;
            const int pitch = key_index % 128;
            res.notes.emplace_back(
                note.start_tick,
                end_tick,
                pitch,
                channel,
                note.velocity > 0 ? note.velocity : 64,
                note.program);
        });

        res.last_tick = abs_tick;
        return res;
    }

    void MidiFile::init_tempo_map(const std::vector<std::pair<int, int>> &tempo_events)
    {
        LOG_DEBUG("初始化节奏映射，事件数: " << tempo_events.size());

        if ((division & 0x8000) != 0)
        {
            int fps = 256 - ((division >> 8) & 0xFF);
            int ticks_per_frame = division & 0xFF;
            m_smpte_ticks_per_second = (double)fps * ticks_per_frame;
            m_tempo_ticks = {0};
            m_tempo_seconds = {0.0};
            m_tempo_values = {500000};
            m_last_tempo_idx = 0;
            LOG_DEBUG("SMPTE 时间模式: " << m_smpte_ticks_per_second << " ticks/s");
            return;
        }

        auto events = tempo_events;
        std::ranges::sort(events);

        if (events.empty() || events[0].first != 0)
        {
            events.insert(events.begin(), {0, 500000});
        }

        std::vector<std::pair<int, int>> deduped;
        int last_tick = -1;
        for (const auto &ev : events)
        {
            if (ev.first == last_tick)
            {
                if (!deduped.empty())
                    deduped.back() = ev;
            }
            else
            {
                deduped.push_back(ev);
                last_tick = ev.first;
            }
        }

        m_tempo_ticks.clear();
        m_tempo_values.clear();
        m_tempo_seconds.clear();
        m_last_tempo_idx = 0;

        m_tempo_ticks.push_back(deduped[0].first);
        m_tempo_values.push_back(deduped[0].second);
        m_tempo_seconds.push_back(0.0);

        for (size_t i = 1; i < deduped.size(); ++i)
        {
            int tick = deduped[i].first;
            int tempo = deduped[i].second;
            int prev_tick = m_tempo_ticks.back();
            int prev_tempo = m_tempo_values.back();
            double prev_sec = m_tempo_seconds.back();

            double dt = (double)(tick - prev_tick) * prev_tempo / (double)division / 1000000.0;
            m_tempo_ticks.push_back(tick);
            m_tempo_values.push_back(tempo);
            m_tempo_seconds.push_back(prev_sec + dt);
        }
    }

    double MidiFile::tick_to_seconds(int tick)
    {
        if ((division & 0x8000) != 0)
        {
            return (double)tick / m_smpte_ticks_per_second;
        }

        if (m_tempo_ticks.empty())
            return 0.0;

        // Optimization: Use cached index for O(1) amortized sequential access
        size_t idx = m_last_tempo_idx;

        // Reset if cached index is invalid or ahead of current tick (backward seek)
        if (idx >= m_tempo_ticks.size() || m_tempo_ticks[idx] > tick)
        {
            idx = 0;
        }

        // Fast forward if needed
        while (idx + 1 < m_tempo_ticks.size() && m_tempo_ticks[idx + 1] <= tick)
        {
            idx++;
        }

        m_last_tempo_idx = idx;

        int t0 = m_tempo_ticks[idx];
        double s0 = m_tempo_seconds[idx];
        int tempo = m_tempo_values[idx];

        return s0 + (double)(tick - t0) * tempo / (double)division / 1000000.0;
    }

    double MidiFile::get_initial_bpm() const
    {
        const auto tempo_us = find_tick_zero_value(m_tempo_events);
        if (!tempo_us || *tempo_us <= 0)
            return 0.0;
        return 60000000.0 / *tempo_us;
    }

    std::pair<int, int> MidiFile::get_initial_time_signature() const
    {
        return find_tick_zero_value(m_time_sig_events).value_or(
            std::pair<int, int>{4, 4});
    }

}
