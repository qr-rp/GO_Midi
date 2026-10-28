#include "core/TransposePlanner.h"
#include "midi/MidiParser.h"
#include "util/KeyManager.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct RegressionCase
    {
        const char* filename;
        int keyboard_size;
        int min_pitch;
        int max_pitch;
        Core::HistogramMode histogram_mode;
        Core::SmartTransposeAlgorithm algorithm;
        int expected_shift;
        double expected_retained_percent;
        std::uint64_t expected_event_hash;
    };

    int g_failures = 0;

    void expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    void hash_bytes(std::uint64_t& hash, const void* data, std::size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ULL;
        }
    }

    std::uint64_t hash_events(const std::vector<Core::PlaybackEvent>& events)
    {
        std::uint64_t hash = 1469598103934665603ULL;
        for (const auto& event : events)
        {
            const auto timestamp = static_cast<std::int64_t>(
                std::llround(event.time * 1000.0));
            const int note_on = event.is_note_on ? 1 : 0;
            hash_bytes(hash, &timestamp, sizeof(timestamp));
            hash_bytes(hash, &note_on, sizeof(note_on));
            hash_bytes(hash, &event.vk_code, sizeof(event.vk_code));
            hash_bytes(hash, &event.modifier, sizeof(event.modifier));
        }
        return hash;
    }

    Core::PlannerConfig make_config(const Midi::MidiFile& midi,
                                    int keyboard_size,
                                    Core::HistogramMode,
                                    Core::SmartTransposeAlgorithm algorithm)
    {
        Util::KeyManager key_manager;
        if (keyboard_size == 36)
            key_manager.load_yysls_preset();

        Core::PlannerConfig config;
        config.min_pitch = 48;
        config.max_pitch = keyboard_size == 36 ? 83 : 84;
        config.smart_algorithm = algorithm;
        config.conflict_key_mode = Core::ConflictKeyMode::PHYSICAL;

        Core::PlannerChannelConfig channel;
        channel.channel_index = 0;
        channel.track_index = -1;
        channel.transpose = 0;
        channel.enabled = true;
        config.channels.push_back(channel);

        for (const auto& [pitch, mapping] : key_manager.get_map())
            config.keymap[pitch] = {mapping.vk_code, mapping.modifier};
        (void)midi;
        return config;
    }

    Core::PlaybackPlan load_and_plan(const std::filesystem::path& root,
                                     const RegressionCase& regression)
    {
        Midi::MidiFile midi((root / regression.filename).wstring());
        if (!midi.is_valid())
            return {};

        std::vector<Midi::RawNote> notes;
        for (const auto& track : midi.raw_notes_by_track)
            notes.insert(notes.end(), track.begin(), track.end());
        std::sort(notes.begin(), notes.end(), [](const Midi::RawNote& left, const Midi::RawNote& right)
        {
            return left.start_s < right.start_s;
        });

        const auto histograms = Core::build_transpose_histograms(
            notes, midi.raw_notes_by_track.size(), regression.histogram_mode);
        return Core::build_playback_plan(
            notes,
            histograms.tracks,
            histograms.global,
            make_config(midi, regression.keyboard_size,
                        regression.histogram_mode, regression.algorithm));
    }

    std::vector<RegressionCase> default_cases()
    {
        return {
            {"frere-jacques.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 86.975, 2014594103686070988ULL},
            {"frere-jacques.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 90.130, 7128562725466720626ULL},
            {"maple-leaf-rag.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 86.659, 3122745055491967420ULL},
            {"maple-leaf-rag.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 87.358, 17964546469692772092ULL},
            {"chopin-etude-10-4.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 92.987, 16303673851312540154ULL},
            {"chopin-etude-10-4.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 93.236, 12504541224091010093ULL},
            {"bach-brandenburg-3.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             12, 43.457, 12546315892688052805ULL},
            {"bach-brandenburg-3.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             12, 43.793, 380268727595076032ULL},
            {"beethoven-symphony-5.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 53.625, 12050130154075753127ULL},
            {"beethoven-symphony-5.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 55.598, 9486965367148401204ULL},
            {"star-spangled-hillery.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 66.667, 13343470688561175787ULL},
            {"star-spangled-hillery.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 66.667, 2355529300834473379ULL}
        };
    }

    std::vector<RegressionCase> pareto_cases()
    {
        auto cases = default_cases();
        for (auto& regression : cases)
        {
            regression.histogram_mode = Core::HistogramMode::CORRECTED;
            regression.algorithm = Core::SmartTransposeAlgorithm::CONFLICT_AWARE;
        }
        return cases;
    }

    double retained_percent(const Core::PlaybackPlan& plan)
    {
        return plan.metrics.source_duration_weight > 0.0
            ? plan.metrics.retained_duration_weight * 100.0 /
                  plan.metrics.source_duration_weight
            : 100.0;
    }

    void run_cases(const std::filesystem::path& root,
                   const std::vector<RegressionCase>& cases,
                   bool print_only)
    {
        for (const auto& regression : cases)
        {
            const auto plan = load_and_plan(root, regression);
            const std::string label =
                std::string(regression.filename) + " " +
                std::to_string(regression.keyboard_size) + " keys";
            if (print_only)
            {
                std::cout << label
                          << " shift=" << plan.global_shift
                          << " retained=" << retained_percent(plan)
                          << " hash=" << hash_events(plan.events) << '\n';
                continue;
            }

            expect(plan.ok, label + " should produce a plan");
            expect(plan.global_shift == regression.expected_shift,
                   label + " shift regression");
            expect(std::abs(retained_percent(plan) - regression.expected_retained_percent) < 0.05,
                   label + " retained-duration regression");
            expect(hash_events(plan.events) == regression.expected_event_hash,
                   label + " event hash regression");

            if (regression.algorithm == Core::SmartTransposeAlgorithm::CONFLICT_AWARE)
            {
                const auto selected = std::find_if(
                    plan.global_candidates.begin(),
                    plan.global_candidates.end(),
                    [&](const Core::ShiftCandidateMetrics& candidate)
                    {
                        return candidate.shift == plan.global_shift;
                    });
                expect(selected != plan.global_candidates.end() && selected->pareto_optimal,
                       label + " selected shift should be Pareto-optimal");
            }
        }
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: transpose_regression_tests <fixture-dir> [--print]\n";
        return EXIT_FAILURE;
    }

    Util::Logger::Instance().Initialize(Util::LogLevel::Error, "", false);
    const std::filesystem::path root(argv[1]);
    const bool print_only = argc >= 3 && std::string(argv[2]) == "--print";
    run_cases(root, default_cases(), print_only);
    run_cases(root, pareto_cases(), print_only);

    if (g_failures != 0)
    {
        std::cerr << g_failures << " regression(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Transpose regression tests passed\n";
    return EXIT_SUCCESS;
}
