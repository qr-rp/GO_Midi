#include "MidiParser.h"
#include <stdexcept>
#include <cstring>
#include <tuple>
#include <unordered_map>
#include "../util/Logger.h"

namespace Midi
{

// 辅助宏：记录函数入口
#define LOG_ENTRY() LOG_DEBUG("[" << __func__ << "] 进入")

    MidiFile::MidiFile(const std::string &filepath)
    {
        LOG_ENTRY();
        LOG_DEBUG("加载 MIDI 文件: " << filepath);

        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            LOG_ERROR("无法打开文件: " << filepath);
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        m_data.resize(size);
        if (!file.read(reinterpret_cast<char *>(m_data.data()), size))
        {
            LOG_ERROR("无法读取文件: " << filepath);
            throw std::runtime_error("Failed to read file: " + filepath);
        }

        LOG_DEBUG("文件大小: " << size << " 字节");
        parse();
    }

#ifdef _WIN32
    MidiFile::MidiFile(const std::wstring &filepath)
    {
        LOG_ENTRY();
        LOG_DEBUG("加载 MIDI 文件 (宽字符路径)");

        std::ifstream file(filepath.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            LOG_ERROR("无法打开文件 (宽字符路径)");
            throw std::runtime_error("Failed to open file (wstring path)");
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        m_data.resize(size);
        if (!file.read(reinterpret_cast<char *>(m_data.data()), size))
        {
            LOG_ERROR("无法读取文件 (宽字符路径)");
            throw std::runtime_error("Failed to read file (wstring path)");
        }

        LOG_DEBUG("文件大小: " << size << " 字节");
        parse();
    }
#endif

    uint16_t MidiFile::readU16(size_t offset)
    {
        if (offset + 2 > m_data.size())
            throw std::out_of_range("Offset out of range");
        return (static_cast<uint16_t>(m_data[offset]) << 8) | m_data[offset + 1];
    }

    uint32_t MidiFile::readU32(size_t offset)
    {
        if (offset + 4 > m_data.size())
            throw std::out_of_range("Offset out of range");
        return (static_cast<uint32_t>(m_data[offset]) << 24) |
               (static_cast<uint32_t>(m_data[offset + 1]) << 16) |
               (static_cast<uint32_t>(m_data[offset + 2]) << 8) |
               m_data[offset + 3];
    }

    std::pair<uint32_t, size_t> MidiFile::readVarLen(size_t offset)
    {
        size_t n = m_data.size();
        if (offset >= n)
            throw std::out_of_range("Unexpected EOF in varlen");

        uint8_t b0 = m_data[offset];
        if ((b0 & 0x80) == 0)
        {
            return {b0, offset + 1};
        }

        uint32_t value = b0 & 0x7F;
        offset++;
        for (int i = 0; i < 3; ++i)
        {
            if (offset >= n)
                throw std::out_of_range("Unexpected EOF in varlen");
            uint8_t b = m_data[offset];
            value = (value << 7) | (b & 0x7F);
            offset++;
            if ((b & 0x80) == 0)
            {
                return {value, offset};
            }
        }

        if (offset >= n)
            throw std::out_of_range("Unexpected EOF in varlen");
        uint8_t b = m_data[offset];
        value = (value << 7) | (b & 0x7F);
        offset++;
        if ((b & 0x80) != 0)
        {
            throw std::runtime_error("Varlen too long");
        }
        return {value, offset};
    }

    std::string MidiFile::decodeText(size_t start, size_t len)
    {
        if (start + len > m_data.size())
            return "";
        return std::string(reinterpret_cast<char *>(m_data.data() + start), len);
    }

    void MidiFile::parse()
    {
        LOG_ENTRY();

        if (m_data.size() < 14)
        {
            LOG_ERROR("无效的 MIDI 文件: 文件太小");
            throw std::runtime_error("Invalid MIDI file");
        }
        if (memcmp(m_data.data(), "MThd", 4) != 0)
        {
            LOG_ERROR("无效的 MIDI 文件: 缺少 MThd 头");
            throw std::runtime_error("Invalid MIDI header");
        }

        uint32_t header_len = readU32(4);
        if (header_len < 6)
        {
            LOG_ERROR("无效的 MIDI 头长度: " << header_len);
            throw std::runtime_error("Invalid MIDI header length");
        }

        format = readU16(8);
        int track_count = readU16(10);
        division = readU16(12);

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
        std::vector<std::vector<std::tuple<int, int, int, int>>> parsed_notes_by_track;
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
                throw std::runtime_error("Invalid track chunk header");
            }

            uint32_t chunk_len = readU32(pos + 4);
            size_t chunk_start = pos + 8;
            size_t chunk_end = chunk_start + chunk_len;
            if (chunk_end > m_data.size())
            {
                LOG_ERROR("音轨块长度无效: " << chunk_len << ", 位置: " << pos);
                throw std::runtime_error("Invalid track chunk length");
            }

            auto res = parse_track(chunk_start, chunk_len, i);
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
        std::sort(m_tempo_events.begin(), m_tempo_events.end());

        m_time_sig_events = all_time_sig_events;
        std::sort(m_time_sig_events.begin(), m_time_sig_events.end());

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
    }

    MidiFile::TrackParseResult MidiFile::parse_track(size_t start, size_t len, int track_index)
    {
        TrackParseResult res;
        res.track.name = "";

        // Estimate capacity to avoid reallocations (approx 4 bytes per event)
        res.notes.reserve(len / 4);

        size_t pos = start;
        size_t end_pos = start + len;
        int abs_tick = 0;
        uint8_t running_status = 0;

        // 优化：使用扁平数组替代 vector<vector<int>>，减少小块堆分配
        // 大多数同一 pitch+channel 同时只有一个活跃音符
        int note_start_tick[2048];
        int8_t note_start_depth[2048]; // 0=无活跃, 1=使用 note_start_tick, >1=有溢出
        std::memset(note_start_depth, 0, sizeof(note_start_depth));
        std::unordered_map<int, std::vector<int>> note_overflow; // 仅用于罕见的同音重叠

        while (pos < end_pos)
        {
            auto vl = readVarLen(pos);
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
                if (pos + 1 > end_pos)
                    break;
                uint8_t meta_type = m_data[pos];
                pos++;
                auto vl_meta = readVarLen(pos);
                uint32_t length = vl_meta.first;
                pos = vl_meta.second;
                size_t meta_end = pos + length;
                if (meta_end > end_pos)
                    break;

                if (meta_type == 0x2F)
                { // End of track
                    pos = meta_end;
                    break;
                }
                if (meta_type == 0x03)
                { // Track name
                    res.track.name = decodeText(pos, length);
                    pos = meta_end;
                    continue;
                }
                if (meta_type == 0x51 && length == 3)
                { // Set Tempo
                    int tempo_us = (m_data[pos] << 16) | (m_data[pos + 1] << 8) | m_data[pos + 2];
                    res.tempo_events.emplace_back(abs_tick, tempo_us);
                    pos = meta_end;
                    continue;
                }
                if (meta_type == 0x58 && length >= 4)
                { // Time Signature
                    int nn = m_data[pos];
                    int dd = 1 << m_data[pos + 1];
                    res.time_sig_events.emplace_back(abs_tick, std::make_pair(nn, dd));
                    pos = meta_end;
                    continue;
                }
                pos = meta_end;
                continue;
            }

            if (status == 0xF0 || status == 0xF7)
            { // SysEx
                auto vl_sysex = readVarLen(pos);
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
                    if (note_start_depth[key_idx] > 0)
                    {
                        int start_tick = note_start_tick[key_idx];
                        note_start_depth[key_idx]--;
                        if (note_start_depth[key_idx] > 0)
                        {
                            auto it = note_overflow.find(key_idx);
                            if (it != note_overflow.end() && !it->second.empty())
                            {
                                note_start_tick[key_idx] = it->second.back();
                                it->second.pop_back();
                            }
                        }
                        res.notes.emplace_back(start_tick, abs_tick, pitch, channel);
                    }
                }
                else
                {
                    if (note_start_depth[key_idx] > 0)
                    {
                        note_overflow[key_idx].push_back(note_start_tick[key_idx]);
                    }
                    note_start_tick[key_idx] = abs_tick;
                    note_start_depth[key_idx]++;
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
                if (note_start_depth[key_idx] > 0)
                {
                    int start_tick = note_start_tick[key_idx];
                    note_start_depth[key_idx]--;
                    if (note_start_depth[key_idx] > 0)
                    {
                        auto it = note_overflow.find(key_idx);
                        if (it != note_overflow.end() && !it->second.empty())
                        {
                            note_start_tick[key_idx] = it->second.back();
                            it->second.pop_back();
                        }
                    }
                    res.notes.emplace_back(start_tick, abs_tick, pitch, channel);
                }
                continue;
            }

            if (event_type == 0xA0 || event_type == 0xB0 || event_type == 0xE0)
            {
                pos += 2;
                continue;
            }

            if (event_type == 0xC0 || event_type == 0xD0)
            {
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
        for (int i = 0; i < 2048; ++i)
        {
            if (note_start_depth[i] <= 0)
                continue;

            int ch = (i / 128) + 1; // 0-15 -> 1-16
            int pitch = i % 128;

            // 关闭栈顶音符
            res.notes.emplace_back(note_start_tick[i], abs_tick, pitch, ch);

            // 关闭溢出栈中的音符
            auto it = note_overflow.find(i);
            if (it != note_overflow.end())
            {
                for (int start_tick : it->second)
                {
                    res.notes.emplace_back(start_tick, abs_tick, pitch, ch);
                }
            }
        }

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
        std::sort(events.begin(), events.end());

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
        int tempo_us = 500000;

        // Optimization: m_tempo_events is already sorted in parse()
        for (const auto &ev : m_tempo_events)
        {
            if (ev.first == 0)
            {
                tempo_us = ev.second;
                break;
            }
            else if (ev.first > 0)
            {
                break;
            }
        }

        if (tempo_us <= 0)
            return 0.0;
        return 60000000.0 / tempo_us;
    }

    std::pair<int, int> MidiFile::get_initial_time_signature() const
    {
        std::pair<int, int> sig = {4, 4};
        bool found = false;

        // Optimization: m_time_sig_events is already sorted
        for (const auto &ev : m_time_sig_events)
        {
            if (ev.first == 0)
            {
                sig = ev.second;
                found = true;
                break;
            }
            else if (ev.first > 0)
            {
                break;
            }
        }

        return sig;
    }

}
