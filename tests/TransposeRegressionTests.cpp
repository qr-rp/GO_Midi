#include "core/TransposePlanner.h"
#include "midi/MidiParser.h"
#include "util/KeyManager.h"
#include "util/Logger.h"

#include <algorithm>
#include <cmath>
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
        std::size_t expected_folded_notes;
        std::size_t expected_conflict_merges;
        std::size_t expected_exact_duplicates;
        std::size_t expected_output_notes;
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
             0, 86.975, 41, 346, 1, 328},
            {"frere-jacques.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 90.130, 9, 345, 1, 332},
            {"maple-leaf-rag.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 86.659, 343, 2546, 296, 2267},
            {"maple-leaf-rag.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 87.358, 320, 2530, 286, 2280},
            {"chopin-etude-10-4.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 92.987, 404, 2191, 75, 2132},
            {"chopin-etude-10-4.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 93.236, 395, 2175, 72, 2136},
            {"bach-brandenburg-3.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             12, 43.457, 5879, 27367, 13102, 12911},
            {"bach-brandenburg-3.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             12, 43.793, 5263, 27351, 13066, 12996},
            {"beethoven-symphony-5.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 53.625, 1181, 8274, 3052, 4584},
            {"beethoven-symphony-5.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 55.598, 993, 8258, 2960, 4719},
            {"star-spangled-hillery.mid", 36, 48, 83,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 66.667, 0, 284, 101, 202},
            {"star-spangled-hillery.mid", 37, 48, 84,
             Core::HistogramMode::LEGACY, Core::SmartTransposeAlgorithm::LEGACY,
             0, 66.667, 0, 282, 101, 202}
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
                          << " folded=" << plan.metrics.folded_notes
                          << " conflicts=" << plan.metrics.conflict_merges
                          << " duplicates=" << plan.metrics.exact_duplicates
                          << " output=" << plan.metrics.output_notes << '\n';
                continue;
            }

            expect(plan.ok, label + " should produce a plan");
            expect(plan.global_shift == regression.expected_shift,
                   label + " shift regression");
            // Compiler-specific floating-point accumulation shifts this by
            // less than a percentage point; octave/fold regressions are caught
            // by the integer metrics below.
            const double retained = retained_percent(plan);
            expect(std::abs(retained - regression.expected_retained_percent) < 1.0,
                   label + " retained-duration regression: got " +
                       std::to_string(retained) + " expected " +
                       std::to_string(regression.expected_retained_percent));
            expect(plan.metrics.folded_notes == regression.expected_folded_notes,
                   label + " folded-note regression");
            expect(plan.metrics.conflict_merges == regression.expected_conflict_merges,
                   label + " conflict-merge regression");
            expect(plan.metrics.output_notes == regression.expected_output_notes,
                   label + " output-note regression");

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
                if (selected != plan.global_candidates.end())
                {
                    expect(selected->output_notes == plan.metrics.output_notes,
                           label + " selected candidate output-note mismatch");
                    expect(std::abs(
                               selected->retained_duration_weight -
                               plan.metrics.retained_duration_weight) < 1e-6,
                           label + " selected candidate retention mismatch");
                }
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
