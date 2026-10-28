#pragma once

#include <cstddef>
#include <span>
#include <unordered_map>
#include <vector>

#include "../midi/MidiParser.h"

namespace Core
{
    enum class SmartTransposeAlgorithm
    {
        LEGACY,
        CONFLICT_AWARE
    };

    enum class ConflictKeyMode
    {
        PHYSICAL,
        LOGICAL
    };

    enum class HistogramMode
    {
        LEGACY,
        CORRECTED
    };

    struct PlannerKeyMapping
    {
        int vk_code{0};
        int modifier{0};
    };

    struct PlannerChannelConfig
    {
        int channel_index{-1};
        int track_index{-1};
        int transpose{0};
        bool enabled{true};
        void* window_handle{nullptr};
    };

    struct PlannerConfig
    {
        int min_pitch{48};
        int max_pitch{84};
        bool decompose{false};
        SmartTransposeAlgorithm smart_algorithm{SmartTransposeAlgorithm::CONFLICT_AWARE};
        ConflictKeyMode conflict_key_mode{ConflictKeyMode::PHYSICAL};
        std::vector<PlannerChannelConfig> channels;
        std::unordered_map<int, PlannerKeyMapping> keymap;
    };

    struct PlaybackEvent
    {
        double time{0.0};
        bool is_note_on{false};
        int vk_code{0};
        int modifier{0};
        void* window_handle{nullptr};

        bool operator<(const PlaybackEvent& other) const
        {
            if (std::abs(time - other.time) > 1e-6)
                return time < other.time;
            return is_note_on < other.is_note_on;
        }
    };

    struct PlanMetrics
    {
        std::size_t source_notes{0};
        std::size_t mapped_notes{0};
        std::size_t unmapped_notes{0};
        std::size_t folded_notes{0};
        std::size_t conflict_merges{0};
        std::size_t exact_duplicates{0};
        std::size_t output_notes{0};
        std::size_t output_events{0};
        double source_duration_weight{0.0};
        double retained_duration_weight{0.0};
        double folded_duration_weight{0.0};
    };

    struct PlanNote
    {
        double start{0.0};
        double end{0.0};
        int pitch{0};
        int track{-1};
        int velocity{0};
        bool folded{false};
    };

    struct ShiftCandidateMetrics
    {
        int shift{0};
        double center_score{0.0};
        double retained_duration_weight{0.0};
        double folded_duration_weight{0.0};
        std::size_t conflicts{0};
        std::size_t exact_duplicates{0};
        std::size_t folded_notes{0};
        std::size_t output_notes{0};
        std::size_t unmapped_notes{0};
        bool pareto_optimal{false};
    };

    struct PlaybackPlan
    {
        bool ok{false};
        std::vector<PlaybackEvent> events;
        std::vector<PlanNote> notes;
        std::vector<ShiftCandidateMetrics> global_candidates;
        std::vector<int> track_shifts;
        int global_shift{0};
        PlanMetrics metrics;
    };

    struct TransposeHistograms
    {
        std::vector<std::vector<float>> tracks;
        std::vector<float> global;
    };

    TransposeHistograms build_transpose_histograms(
        std::span<const Midi::RawNote> notes,
        std::size_t track_count,
        HistogramMode mode = HistogramMode::CORRECTED);

    PlaybackPlan build_playback_plan(
        std::span<const Midi::RawNote> input_notes,
        std::span<const std::vector<float>> track_hists,
        std::span<const float> global_hist,
        const PlannerConfig& config);
}
