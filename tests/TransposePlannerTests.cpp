#include "core/TransposePlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static_assert(sizeof(Midi::RawNote) <= 20,
              "RawNote should stay compact for large MIDI files");
static_assert(sizeof(Core::PlaybackEvent) <= 24,
              "PlaybackEvent should stay compact for playback storage");

namespace
{
    int g_failures = 0;

    void expect(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    Core::PlannerConfig make_config(int min_pitch, int max_pitch,
                                    Core::SmartTransposeAlgorithm algorithm,
                                    Core::ConflictKeyMode conflict_mode)
    {
        Core::PlannerConfig config;
        config.min_pitch = min_pitch;
        config.max_pitch = max_pitch;
        config.smart_algorithm = algorithm;
        config.conflict_key_mode = conflict_mode;

        Core::PlannerChannelConfig channel;
        channel.channel_index = 0;
        channel.track_index = -1;
        channel.transpose = 0;
        channel.enabled = true;
        config.channels.push_back(channel);

        for (int pitch = 0; pitch < 128; ++pitch)
            config.keymap[pitch] = {pitch + 1, 0};

        return config;
    }

    Midi::RawNote make_note(float start, float duration, int pitch, int velocity = 100)
    {
        Midi::RawNote note;
        note.start_s = start;
        note.duration = duration;
        note.pitch = pitch;
        note.track_index = 0;
        note.channel = 1;
        note.velocity = velocity;
        note.program = 0;
        return note;
    }

    std::vector<float> make_hist(const std::vector<Midi::RawNote>& notes)
    {
        std::vector<float> hist(128, 0.0f);
        for (const auto& note : notes)
        {
            if (note.pitch >= 0 && note.pitch < 128)
                hist[note.pitch] += std::sqrt(note.duration) * note.velocity;
        }
        return hist;
    }

    Core::PlaybackPlan build_plan(const std::vector<Midi::RawNote>& notes,
                                  const std::vector<float>& hist,
                                  const Core::PlannerConfig& config)
    {
        const std::vector<std::vector<float>> track_hists = {hist};
        return Core::build_playback_plan(notes, track_hists, hist, config);
    }

    bool dominates(const Core::ShiftCandidateMetrics& left,
                   const Core::ShiftCandidateMetrics& right)
    {
        const bool no_worse =
            left.retained_duration_weight >= right.retained_duration_weight &&
            left.conflicts <= right.conflicts &&
            left.exact_duplicates <= right.exact_duplicates &&
            left.unmapped_notes <= right.unmapped_notes &&
            left.folded_duration_weight <= right.folded_duration_weight &&
            left.center_score >= right.center_score;
        const bool strictly_better =
            left.retained_duration_weight > right.retained_duration_weight ||
            left.conflicts < right.conflicts ||
            left.exact_duplicates < right.exact_duplicates ||
            left.unmapped_notes < right.unmapped_notes ||
            left.folded_duration_weight < right.folded_duration_weight ||
            left.center_score > right.center_score;
        return no_worse && strictly_better;
    }

    void test_legacy_keeps_original_octave_choice()
    {
        std::vector<Midi::RawNote> notes;
        for (int pitch = 96; pitch <= 108; ++pitch)
            notes.push_back(make_note(0.0f, 1.0f, pitch));

        const auto hist = make_hist(notes);
        const auto result = build_plan(
            notes, hist,
            make_config(48, 84, Core::SmartTransposeAlgorithm::LEGACY,
                        Core::ConflictKeyMode::PHYSICAL));

        expect(result.ok, "legacy plan should succeed");
        expect(result.global_shift == -36,
               "legacy algorithm should retain its original center-weighted octave choice");
    }

    void test_conflict_aware_accounts_for_keymap_coverage()
    {
        std::vector<Midi::RawNote> notes;
        for (int pitch = 48; pitch <= 59; ++pitch)
            notes.push_back(make_note(0.0f, 2.0f, pitch));
        const auto hist = make_hist(notes);

        auto legacy_config = make_config(
            48, 84, Core::SmartTransposeAlgorithm::LEGACY,
            Core::ConflictKeyMode::PHYSICAL);
        for (int pitch = 60; pitch < 128; ++pitch)
            legacy_config.keymap.erase(pitch);

        auto improved_config = legacy_config;
        improved_config.smart_algorithm = Core::SmartTransposeAlgorithm::CONFLICT_AWARE;

        const auto legacy = build_plan(notes, hist, legacy_config);
        const auto improved = build_plan(notes, hist, improved_config);

        expect(legacy.ok && improved.ok, "both comparison plans should succeed");
        expect(legacy.global_shift == 12,
               "legacy center scoring should shift away from the mapped octave");
        expect(improved.global_shift == 0,
               "conflict-aware plan should choose the fully mapped octave, got " +
                   std::to_string(improved.global_shift));
        expect(improved.metrics.retained_duration_weight >
                   legacy.metrics.retained_duration_weight,
               "conflict-aware plan should retain more weighted duration, got improved=" +
                   std::to_string(improved.metrics.retained_duration_weight) +
                   " legacy=" + std::to_string(legacy.metrics.retained_duration_weight));

        const auto selected = std::find_if(
            improved.global_candidates.begin(),
            improved.global_candidates.end(),
            [&](const Core::ShiftCandidateMetrics& candidate)
            {
                return candidate.shift == improved.global_shift;
            });
        expect(selected != improved.global_candidates.end() && selected->pareto_optimal,
               "selected shift should belong to the Pareto frontier");
        for (const auto& candidate : improved.global_candidates)
        {
            expect(selected == improved.global_candidates.end() ||
                       !dominates(candidate, *selected),
                   "no evaluated candidate should dominate the selected Pareto point");
        }
    }

    void test_conflict_identity_modes()
    {
        std::vector<Midi::RawNote> notes = {
            make_note(0.0f, 1.0f, 48),
            make_note(0.0f, 1.0f, 49)
        };
        const auto hist = make_hist(notes);

        auto physical_config = make_config(
            0, 127, Core::SmartTransposeAlgorithm::LEGACY,
            Core::ConflictKeyMode::PHYSICAL);
        physical_config.channels[0].transpose = 1;
        physical_config.keymap[49] = {physical_config.keymap[48].vk_code, 0};
        physical_config.keymap[50] = {physical_config.keymap[48].vk_code, 1};

        auto logical_config = physical_config;
        logical_config.conflict_key_mode = Core::ConflictKeyMode::LOGICAL;

        const auto physical = build_plan(notes, hist, physical_config);
        const auto logical = build_plan(notes, hist, logical_config);

        expect(physical.metrics.output_notes == 1,
               "physical conflict mode should merge the same VK even with different modifiers");
        expect(logical.metrics.output_notes == 2,
               "logical conflict mode should preserve distinct VK plus modifier bindings");
    }

    void test_octave_fold_preserves_existing_fallback_behavior()
    {
        const std::vector<Midi::RawNote> notes = {
            make_note(0.0f, 1.0f, 36),
            make_note(0.0f, 1.0f, 96)
        };
        const auto hist = make_hist(notes);
        const auto result = build_plan(
            notes, hist,
            make_config(48, 84, Core::SmartTransposeAlgorithm::LEGACY,
                        Core::ConflictKeyMode::PHYSICAL));

        expect(result.ok, "fold plan should succeed");
        expect(result.metrics.folded_notes > 0,
               "a source range wider than the target should report octave folds");
    }

    void test_event_only_output_skips_note_payload()
    {
        std::vector<Midi::RawNote> notes = {
            make_note(0.0f, 1.0f, 60)
        };
        const auto hist = make_hist(notes);
        const auto config = make_config(
            48, 84, Core::SmartTransposeAlgorithm::LEGACY,
            Core::ConflictKeyMode::PHYSICAL);

        const auto full = build_plan(notes, hist, config);
        const auto events_only = Core::build_playback_plan(
            notes, std::vector<std::vector<float>>{hist}, hist, config,
            Core::PlanOutputMode::EVENTS);

        expect(full.ok && events_only.ok, "both output modes should produce plans");
        expect(events_only.notes.empty(),
               "event-only mode should not retain PlanNote payload");
        expect(events_only.events.size() == full.events.size(),
               "event-only and full modes should emit the same event count");
        expect(events_only.metrics.output_notes == full.metrics.output_notes,
               "event-only mode should retain the same output metrics");
    }

    void test_decompose_staggers_chords()
    {
        const std::vector<Midi::RawNote> notes = {
            make_note(0.0f, 1.0f, 60),
            make_note(0.0f, 1.0f, 64)
        };
        const auto hist = make_hist(notes);
        auto config = make_config(
            48, 83, Core::SmartTransposeAlgorithm::LEGACY,
            Core::ConflictKeyMode::LOGICAL);
        config.decompose = true;

        const auto result = build_plan(notes, hist, config);
        std::vector<double> note_on_times;
        for (const auto& event : result.events)
        {
            if (event.is_note_on)
                note_on_times.push_back(event.time);
        }

        expect(note_on_times.size() == 2, "decomposed chord should still emit two note-on events");
        expect(note_on_times.size() == 2 && note_on_times[1] - note_on_times[0] > 0.049,
               "decomposed chord should use the existing 50ms stagger");
    }

    void test_corrected_skew_strengthens_sparse_high_notes()
    {
        std::vector<Midi::RawNote> notes;
        for (int index = 0; index < 20; ++index)
            notes.push_back(make_note(0.0f, 1.0f, 60));
        notes.push_back(make_note(0.0f, 1.0f, 84));

        const auto legacy = Core::build_transpose_histograms(
            notes, 1, Core::HistogramMode::LEGACY);
        const auto corrected = Core::build_transpose_histograms(
            notes, 1, Core::HistogramMode::CORRECTED);

        expect(corrected.tracks[0][84] > legacy.tracks[0][84],
               "corrected skew should strengthen sparse high notes instead of weakening them");
    }
}

int main()
{
    test_legacy_keeps_original_octave_choice();
    test_conflict_aware_accounts_for_keymap_coverage();
    test_conflict_identity_modes();
    test_octave_fold_preserves_existing_fallback_behavior();
    test_event_only_output_skips_note_payload();
    test_decompose_staggers_chords();
    test_corrected_skew_strengthens_sparse_high_notes();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "TransposePlanner tests passed\n";
    return EXIT_SUCCESS;
}
