#include "core/TransposePlanner.h"
#include "midi/MidiParser.h"
#include "util/KeyManager.h"
#include "util/Logger.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

    struct MemorySnapshot {
        std::size_t working_set;
        std::size_t peak_working_set;
        std::size_t pagefile_usage;
        std::size_t peak_pagefile_usage;
    };

    MemorySnapshot capture_memory()
    {
        PROCESS_MEMORY_COUNTERS counters{};
        if (!GetProcessMemoryInfo(
                GetCurrentProcess(), &counters, sizeof(counters))) {
            return {};
        }
        return {
            counters.WorkingSetSize,
            counters.PeakWorkingSetSize,
            counters.PagefileUsage,
            counters.PeakPagefileUsage
        };
    }

    void print_memory(const char* stage, const MemorySnapshot& memory)
    {
        std::cout << stage
                  << "_working_kb=" << memory.working_set / 1024
                  << ' ' << stage
                  << "_peak_kb=" << memory.peak_working_set / 1024
                  << ' ' << stage
                  << "_pagefile_kb=" << memory.pagefile_usage / 1024
                  << ' ' << stage
                  << "_peak_pagefile_kb=" << memory.peak_pagefile_usage / 1024
                  << '\n';
    }

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: planner_memory_benchmark <input.mid>\n";
        return EXIT_FAILURE;
    }

    Util::Logger::Instance().Initialize(
        Util::LogLevel::Error, "", false);
    print_memory("baseline", capture_memory());

    Midi::MidiFile midi(std::filesystem::path(argv[1]).wstring());
    if (!midi.is_valid()) {
        std::cerr << "MIDI parse failed: " << midi.error_msg() << '\n';
        return EXIT_FAILURE;
    }
    print_memory("parsed", capture_memory());

    std::vector<Midi::RawNote> notes;
    for (const auto& track : midi.raw_notes_by_track)
        notes.insert(notes.end(), track.begin(), track.end());
    std::ranges::sort(notes, {}, &Midi::RawNote::start_s);

    const auto histograms = Core::build_transpose_histograms(
        notes, midi.raw_notes_by_track.size());

    Util::KeyManager keys;
    keys.load_yysls_preset();
    Core::PlannerConfig config;
    config.min_pitch = 48;
    config.max_pitch = 83;
    config.smart_algorithm = Core::SmartTransposeAlgorithm::CONFLICT_AWARE;
    config.conflict_key_mode = Core::ConflictKeyMode::PHYSICAL;

    Core::PlannerChannelConfig channel;
    channel.channel_index = 0;
    channel.track_index = -1;
    channel.transpose = 0;
    channel.enabled = true;
    config.channels.push_back(channel);
    for (const auto& [pitch, mapping] : keys.get_map())
        config.keymap[pitch] = {mapping.vk_code, mapping.modifier};

    const auto started = std::chrono::steady_clock::now();
    const auto plan = Core::build_playback_plan(
        notes, histograms.tracks, histograms.global, config);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    print_memory("planned", capture_memory());
    std::cout << "elapsed_ms=" << elapsed.count()
              << " raw_note_bytes=" << sizeof(Midi::RawNote)
              << " playback_event_bytes=" << sizeof(Core::PlaybackEvent)
              << " source_notes=" << notes.size()
              << " output_notes=" << plan.metrics.output_notes
              << " output_events=" << plan.events.size() << '\n';
    return plan.ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
