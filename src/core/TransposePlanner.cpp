#include "TransposePlanner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace Core
{
    namespace
    {
        struct WorkingNote
        {
            double start{0.0};
            double end{0.0};
            int vk{0};
            int modifier{0};
            void* hwnd{nullptr};
            int pitch{0};
            int track{-1};
            int velocity{0};
            bool folded{false};
        };

        struct CandidateScore
        {
            double retained_weight{-1.0};
            std::size_t conflicts{0};
            std::size_t exact_duplicates{0};
            std::size_t unmapped_notes{0};
            double folded_weight{std::numeric_limits<double>::max()};
            double center_score{-1.0};
            int shift{0};
        };

        struct CandidateRanges
        {
            double min_retained{0.0};
            double max_retained{0.0};
            double min_folded{0.0};
            double max_folded{0.0};
            std::size_t min_conflicts{0};
            std::size_t max_conflicts{0};
            std::size_t min_duplicates{0};
            std::size_t max_duplicates{0};
            std::size_t min_unmapped{0};
            std::size_t max_unmapped{0};
        };

        struct PlanBuild
        {
            std::vector<PlaybackEvent> events;
            std::vector<PlanNote> notes;
            std::vector<WorkingNote> working_notes;
            PlanMetrics metrics;
        };

        struct ShiftBuildInput
        {
            std::span<const Midi::RawNote> notes;
            int global_shift{0};
            std::span<const int> track_shifts;
            const PlannerConfig& config;
            PlanOutputMode output_mode{PlanOutputMode::METRICS_ONLY};
        };

        struct ShiftEvaluation
        {
            CandidateScore score;
            ShiftCandidateMetrics metrics;
        };

        struct AutoGroups
        {
            bool has_global{false};
            std::vector<bool> tracks;
        };

        struct ShiftSearchContext
        {
            std::span<const Midi::RawNote> notes;
            std::span<const std::vector<float>> track_hists;
            std::span<const float> global_hist;
            const PlannerConfig& config;
            AutoGroups groups;
            bool full_range{false};
        };

        int positive_velocity(int velocity)
        {
            return std::max(velocity, 1);
        }

        double note_weight(int velocity)
        {
            return static_cast<double>(positive_velocity(velocity));
        }

        bool fold_pitch(int pitch, int min_pitch, int max_pitch, int& folded_pitch, std::size_t& wraps)
        {
            folded_pitch = pitch;
            wraps = 0;

            while (folded_pitch < min_pitch)
            {
                folded_pitch += 12;
                ++wraps;
            }
            while (folded_pitch > max_pitch)
            {
                folded_pitch -= 12;
                ++wraps;
            }

            if (folded_pitch < min_pitch)
            {
                folded_pitch = min_pitch;
                ++wraps;
            }
            if (folded_pitch > max_pitch)
            {
                folded_pitch = max_pitch;
                ++wraps;
            }

            return folded_pitch >= 0 && folded_pitch <= 127;
        }

        float center_shift_score(std::span<const float> hist,
                                 int shift,
                                 int min_pitch,
                                 int max_pitch)
        {
            if (hist.size() != 128)
                return 0.0f;

            const float center = (min_pitch + max_pitch) / 2.0f;
            const float half_range = std::max((max_pitch - min_pitch) / 2.0f, 1.0f);
            const float sigma = half_range * 0.4f;

            int low = min_pitch - shift;
            int high = max_pitch - shift;
            low = std::max(low, 0);
            high = std::min(high, 127);

            float score = 0.0f;
            for (int pitch = low; pitch <= high; ++pitch)
            {
                if (hist[pitch] <= 0.0f)
                    continue;

                const float mapped = static_cast<float>(pitch + shift);
                const float distance = std::abs(mapped - center);
                const float weight = std::exp(-0.5f * (distance / sigma) * (distance / sigma));
                score += hist[pitch] * weight;
            }
            return score;
        }

        int choose_legacy_shift(std::span<const float> hist,
                                int min_pitch,
                                int max_pitch)
        {
            float scores[9] = {};
            for (int octave = -4; octave <= 4; ++octave)
            {
                const int shift = octave * 12;
                scores[octave + 4] = center_shift_score(hist, shift, min_pitch, max_pitch);
            }

            float best_score = -1.0f;
            int best_index = 4;
            for (int index = 0; index < 9; ++index)
            {
                if (scores[index] > best_score)
                {
                    best_score = scores[index];
                    best_index = index;
                }
                else if (scores[index] == best_score &&
                         std::abs(index - 4) < std::abs(best_index - 4))
                {
                    best_index = index;
                }
            }
            return (best_index - 4) * 12;
        }

        AutoGroups find_auto_groups(std::span<const PlannerChannelConfig> channels,
                                    std::size_t track_count)
        {
            AutoGroups groups;
            groups.tracks.assign(track_count, false);

            for (const auto& channel : channels)
            {
                if (!channel.enabled || channel.transpose != 0)
                    continue;

                if (channel.track_index >= 0 &&
                    static_cast<std::size_t>(channel.track_index) < groups.tracks.size())
                {
                    groups.tracks[channel.track_index] = true;
                }
                else if (channel.track_index < 0)
                {
                    groups.has_global = true;
                }
            }
            return groups;
        }

        std::vector<PlannerChannelConfig> active_channels(const PlannerConfig& config)
        {
            std::vector<PlannerChannelConfig> channels;
            for (const auto& channel : config.channels)
            {
                if (channel.enabled)
                    channels.push_back(channel);
            }

            if (channels.empty())
            {
                PlannerChannelConfig fallback;
                fallback.channel_index = 0;
                fallback.enabled = true;
                fallback.track_index = -1;
                fallback.transpose = 0;
                channels.push_back(fallback);
            }
            return channels;
        }

        bool conflict_key_equal(const WorkingNote& left, const WorkingNote& right,
                                ConflictKeyMode mode)
        {
            return left.hwnd == right.hwnd &&
                   left.vk == right.vk &&
                   (mode == ConflictKeyMode::PHYSICAL || left.modifier == right.modifier);
        }

        void resolve_conflict(WorkingNote* previous, WorkingNote* current, PlanMetrics& metrics)
        {
            ++metrics.conflict_merges;

            if (std::abs(previous->start - current->start) < 1e-5 &&
                std::abs(previous->end - previous->start -
                         (current->end - current->start)) < 1e-5)
            {
                ++metrics.exact_duplicates;
                current->end = current->start - 1.0;
                return;
            }

            if (current->start < previous->start)
                current->start = previous->start;

            if (previous->end > current->end)
                current->end = previous->end;

            if (previous->end > current->start)
                previous->end = current->start;
        }

        void apply_conflict_resolution(std::vector<WorkingNote>& notes,
                                       ConflictKeyMode mode,
                                       PlanMetrics& metrics)
        {
            std::vector<WorkingNote*> active;

            for (auto& current : notes)
            {
                if (current.vk == 0 || current.end <= current.start)
                    continue;

                auto it = std::ranges::find_if(active, [&](const WorkingNote* previous)
                {
                    return conflict_key_equal(*previous, current, mode);
                });

                if (it != active.end())
                    resolve_conflict(*it, &current, metrics);

                if (current.end > current.start)
                {
                    if (it != active.end())
                        *it = &current;
                    else
                        active.push_back(&current);
                }
            }
        }

        void sort_notes_by_start(std::vector<WorkingNote>& notes)
        {
            std::ranges::sort(notes, [](const WorkingNote& left, const WorkingNote& right)
            {
                return left.start < right.start;
            });
        }

        std::size_t stagger_chord(std::vector<WorkingNote>& group,
                                  std::size_t first,
                                  double chord_threshold,
                                  double stagger_seconds)
        {
            std::size_t end = first + 1;
            while (end < group.size() &&
                   (group[end].start - group[first].start) < chord_threshold &&
                   group[end].vk != group[first].vk)
            {
                ++end;
            }

            if (end - first <= 1)
                return end;

            std::ranges::sort(group.begin() + first, group.begin() + end,
                              [](const WorkingNote& left, const WorkingNote& right)
            {
                return left.pitch < right.pitch;
            });

            for (std::size_t chord_index = 1; chord_index < end - first; ++chord_index)
            {
                const double shift = chord_index * stagger_seconds;
                group[first + chord_index].start += shift;
                group[first + chord_index].end += shift;
            }
            return end;
        }

        void enforce_monophonic_group(std::vector<WorkingNote>& group)
        {
            sort_notes_by_start(group);
            for (std::size_t note_index = 0; note_index + 1 < group.size(); ++note_index)
            {
                if (group[note_index].end > group[note_index + 1].start)
                    group[note_index].end = group[note_index + 1].start;
            }
        }

        void append_valid_notes(std::vector<WorkingNote>& output,
                                const std::vector<WorkingNote>& group)
        {
            for (const auto& note : group)
            {
                if (note.end > note.start)
                    output.push_back(note);
            }
        }

        void decompose_group(std::vector<WorkingNote>& group)
        {
            constexpr double kChordThreshold = 0.03;
            constexpr double kStagger = 0.05;

            sort_notes_by_start(group);
            std::size_t index = 0;
            while (index < group.size())
                index = stagger_chord(group, index, kChordThreshold, kStagger);
            enforce_monophonic_group(group);
        }

        void apply_decompose(std::vector<WorkingNote>& notes)
        {
            std::unordered_map<void*, std::vector<WorkingNote>> grouped;
            for (const auto& note : notes)
            {
                if (note.end > note.start)
                    grouped[note.hwnd].push_back(note);
            }

            std::vector<WorkingNote> mono_notes;
            mono_notes.reserve(notes.size());
            for (auto& [hwnd, group] : grouped)
            {
                (void)hwnd;
                decompose_group(group);
                append_valid_notes(mono_notes, group);
            }

            notes = std::move(mono_notes);
        }

        bool channel_accepts_note(const Midi::RawNote& raw,
                                  const PlannerChannelConfig& channel)
        {
            if (channel.track_index >= 0)
                return raw.track_index == channel.track_index;
            return raw.channel != 10;
        }

        int effective_transpose(const PlannerChannelConfig& channel,
                                int global_shift,
                                std::span<const int> track_shifts)
        {
            if (channel.transpose != 0)
                return channel.transpose;

            if (channel.track_index >= 0 &&
                static_cast<std::size_t>(channel.track_index) < track_shifts.size())
            {
                return track_shifts[channel.track_index];
            }
            if (channel.track_index < 0)
                return global_shift;
            return 0;
        }

        void append_planned_note(const Midi::RawNote& raw,
                                 const PlannerChannelConfig& channel,
                                 int transpose,
                                 const PlannerConfig& config,
                                 PlanBuild& build)
        {
            const bool smart_transpose = (channel.transpose == 0);
            const int shifted_pitch = raw.pitch + transpose;
            int folded_pitch = shifted_pitch;
            std::size_t wraps = 0;
            const bool range_valid = smart_transpose
                ? fold_pitch(shifted_pitch, config.min_pitch, config.max_pitch,
                             folded_pitch, wraps)
                : (shifted_pitch >= 0 && shifted_pitch <= 127);

            ++build.metrics.source_notes;
            const double duration_weight =
                std::max(0.0f, raw.duration) * note_weight(raw.velocity);
            build.metrics.source_duration_weight += duration_weight;

            if (!range_valid)
            {
                ++build.metrics.unmapped_notes;
                return;
            }

            if (wraps > 0)
            {
                ++build.metrics.folded_notes;
                build.metrics.folded_duration_weight += duration_weight;
            }

            const auto mapping_it = config.keymap.find(folded_pitch);
            if (mapping_it == config.keymap.end() || mapping_it->second.vk_code == 0)
            {
                ++build.metrics.unmapped_notes;
                return;
            }

            ++build.metrics.mapped_notes;
            build.working_notes.push_back({
                raw.start_s,
                raw.start_s + raw.duration,
                mapping_it->second.vk_code,
                mapping_it->second.modifier,
                channel.window_handle,
                folded_pitch,
                raw.track_index,
                raw.velocity,
                wraps > 0
            });
        }

        void collect_working_notes(
            std::span<const Midi::RawNote> input_notes,
            int global_shift,
            std::span<const int> track_shifts,
            const PlannerConfig& config,
            PlanBuild& build)
        {
            const auto channels = active_channels(config);
            build.working_notes.reserve(input_notes.size());

            for (const auto& raw : input_notes)
            {
                for (const auto& channel : channels)
                {
                    if (!channel_accepts_note(raw, channel))
                        continue;
                    const int transpose = effective_transpose(
                        channel, global_shift, track_shifts);
                    append_planned_note(
                        raw, channel, transpose, config, build);
                }
            }
        }

        void finalize_plan_output(std::vector<WorkingNote>& notes,
                                  PlanBuild& build,
                                  PlanOutputMode output_mode)
        {
            const bool emit_events = output_mode != PlanOutputMode::METRICS_ONLY;
            const bool emit_notes = output_mode == PlanOutputMode::EVENTS_AND_NOTES;
            std::size_t valid_notes = 0;
            for (const auto& note : notes)
            {
                if (note.end > note.start)
                {
                    ++valid_notes;
                    build.metrics.retained_duration_weight +=
                        (note.end - note.start) * note_weight(note.velocity);
                }
            }
            build.metrics.output_notes = valid_notes;

            if (!emit_events)
                return;

            build.events.reserve(valid_notes * 2);
            if (emit_notes)
                build.notes.reserve(valid_notes);
            for (const auto& note : notes)
            {
                if (note.end <= note.start)
                    continue;

                if (emit_notes)
                {
                    build.notes.push_back({
                        note.start,
                        note.end,
                        note.pitch,
                        note.track,
                        note.velocity,
                        note.folded
                    });
                }
                build.events.push_back(
                    {note.start, true,
                     static_cast<unsigned>(note.vk),
                     static_cast<unsigned>(note.modifier),
                     note.hwnd});
                build.events.push_back(
                    {note.end, false,
                     static_cast<unsigned>(note.vk),
                     static_cast<unsigned>(note.modifier),
                     note.hwnd});
            }

            std::ranges::sort(
                build.events,
                [](const PlaybackEvent& left, const PlaybackEvent& right)
                {
                    return left < right;
                });
            build.metrics.output_events = build.events.size();
        }

        void build_for_shifts(const ShiftBuildInput& input, PlanBuild& build)
        {
            build.events.clear();
            build.notes.clear();
            build.working_notes.clear();
            build.metrics = {};
            collect_working_notes(
                input.notes, input.global_shift, input.track_shifts,
                input.config, build);

            apply_conflict_resolution(
                build.working_notes, input.config.conflict_key_mode, build.metrics);
            if (input.config.decompose)
                apply_decompose(build.working_notes);

            finalize_plan_output(build.working_notes, build, input.output_mode);
        }

        PlanBuild build_for_shifts(const ShiftBuildInput& input)
        {
            PlanBuild build;
            build_for_shifts(input, build);
            return build;
        }

        CandidateScore score_plan(const PlanBuild& build, int shift, float center_score)
        {
            CandidateScore score;
            score.retained_weight = build.metrics.retained_duration_weight;
            score.conflicts = build.metrics.conflict_merges;
            score.exact_duplicates = build.metrics.exact_duplicates;
            score.unmapped_notes = build.metrics.unmapped_notes;
            score.folded_weight = build.metrics.folded_duration_weight;
            score.center_score = center_score;
            score.shift = shift;
            return score;
        }

        bool candidate_is_better(const CandidateScore& candidate,
                                 const CandidateScore& current)
        {
            constexpr double kEpsilon = 1e-9;
            if (candidate.retained_weight > current.retained_weight + kEpsilon)
                return true;
            if (candidate.retained_weight + kEpsilon < current.retained_weight)
                return false;

            if (candidate.conflicts != current.conflicts)
                return candidate.conflicts < current.conflicts;

            if (candidate.folded_weight + kEpsilon < current.folded_weight)
                return true;
            if (candidate.folded_weight > current.folded_weight + kEpsilon)
                return false;

            if (candidate.center_score > current.center_score + kEpsilon)
                return true;
            if (candidate.center_score + kEpsilon < current.center_score)
                return false;

            return std::abs(candidate.shift) < std::abs(current.shift);
        }

        bool candidate_is_equivalent(const CandidateScore& left,
                                     const CandidateScore& right)
        {
            constexpr double kEpsilon = 1e-9;
            return std::abs(left.retained_weight - right.retained_weight) <= kEpsilon &&
                   left.conflicts == right.conflicts &&
                   left.exact_duplicates == right.exact_duplicates &&
                   left.unmapped_notes == right.unmapped_notes &&
                   std::abs(left.folded_weight - right.folded_weight) <= kEpsilon;
        }

        bool candidate_dominates(const CandidateScore& left,
                                 const CandidateScore& right)
        {
            constexpr double kEpsilon = 1e-9;
            const bool no_worse =
                left.retained_weight + kEpsilon >= right.retained_weight &&
                left.conflicts <= right.conflicts &&
                left.exact_duplicates <= right.exact_duplicates &&
                left.unmapped_notes <= right.unmapped_notes &&
                left.folded_weight <= right.folded_weight + kEpsilon;
            if (!no_worse || candidate_is_equivalent(left, right))
                return false;

            return left.retained_weight > right.retained_weight + kEpsilon ||
                   left.conflicts < right.conflicts ||
                   left.exact_duplicates < right.exact_duplicates ||
                   left.unmapped_notes < right.unmapped_notes ||
                   left.folded_weight + kEpsilon < right.folded_weight;
        }

        std::vector<CandidateScore> pareto_frontier(
            std::span<const CandidateScore> candidates)
        {
            std::vector<CandidateScore> frontier;
            for (std::size_t index = 0; index < candidates.size(); ++index)
            {
                bool dominated = false;
                for (std::size_t other = 0; other < candidates.size(); ++other)
                {
                    if (index == other)
                        continue;
                    if (candidate_dominates(candidates[other], candidates[index]))
                    {
                        dominated = true;
                        break;
                    }
                }
                if (!dominated)
                    frontier.push_back(candidates[index]);
            }
            return frontier;
        }

        void mark_pareto_candidates(
            std::vector<ShiftCandidateMetrics>& candidates,
            std::span<const CandidateScore> frontier)
        {
            for (auto& candidate : candidates)
            {
                candidate.pareto_optimal = std::ranges::any_of(
                    frontier, [&](const CandidateScore& score)
                {
                    return candidate.shift == score.shift;
                });
            }
        }

        double normalized_loss(double value, double low, double high, bool maximize)
        {
            if (high - low <= 1e-12)
                return 0.0;
            double normalized = (value - low) / (high - low);
            if (maximize)
                normalized = 1.0 - normalized;
            return normalized * normalized;
        }

        CandidateRanges measure_candidate_ranges(
            std::span<const CandidateScore> candidates)
        {
            CandidateRanges ranges{
                candidates.front().retained_weight,
                candidates.front().retained_weight,
                candidates.front().folded_weight,
                candidates.front().folded_weight,
                candidates.front().conflicts,
                candidates.front().conflicts,
                candidates.front().exact_duplicates,
                candidates.front().exact_duplicates,
                candidates.front().unmapped_notes,
                candidates.front().unmapped_notes
            };
            for (const auto& candidate : candidates)
            {
                ranges.min_retained = std::min(ranges.min_retained, candidate.retained_weight);
                ranges.max_retained = std::max(ranges.max_retained, candidate.retained_weight);
                ranges.min_folded = std::min(ranges.min_folded, candidate.folded_weight);
                ranges.max_folded = std::max(ranges.max_folded, candidate.folded_weight);
                ranges.min_conflicts = std::min(ranges.min_conflicts, candidate.conflicts);
                ranges.max_conflicts = std::max(ranges.max_conflicts, candidate.conflicts);
                ranges.min_duplicates =
                    std::min(ranges.min_duplicates, candidate.exact_duplicates);
                ranges.max_duplicates =
                    std::max(ranges.max_duplicates, candidate.exact_duplicates);
                ranges.min_unmapped =
                    std::min(ranges.min_unmapped, candidate.unmapped_notes);
                ranges.max_unmapped =
                    std::max(ranges.max_unmapped, candidate.unmapped_notes);
            }
            return ranges;
        }

        double pareto_regret(const CandidateScore& candidate,
                             const CandidateRanges& ranges)
        {
            double loss = normalized_loss(
                candidate.retained_weight,
                ranges.min_retained,
                ranges.max_retained,
                true);
            loss = std::max(loss, normalized_loss(
                candidate.folded_weight,
                ranges.min_folded,
                ranges.max_folded,
                false));
            loss = std::max(loss, normalized_loss(
                static_cast<double>(candidate.conflicts),
                static_cast<double>(ranges.min_conflicts),
                static_cast<double>(ranges.max_conflicts),
                false));
            loss = std::max(loss, normalized_loss(
                static_cast<double>(candidate.exact_duplicates),
                static_cast<double>(ranges.min_duplicates),
                static_cast<double>(ranges.max_duplicates),
                false));
            return std::max(loss, normalized_loss(
                static_cast<double>(candidate.unmapped_notes),
                static_cast<double>(ranges.min_unmapped),
                static_cast<double>(ranges.max_unmapped),
                false));
        }

        CandidateScore select_pareto_candidate(
            std::span<const CandidateScore> frontier,
            int preferred_shift)
        {
            if (frontier.size() == 1)
                return frontier.front();

            // 原算法若仍处于 Pareto 前沿，则没有必须接受任何单维退化。
            const auto preferred = std::ranges::find_if(
                frontier, [&](const CandidateScore& candidate)
            {
                return candidate.shift == preferred_shift;
            });
            if (preferred != frontier.end())
                return *preferred;

            const CandidateRanges ranges = measure_candidate_ranges(frontier);
            CandidateScore best = frontier.front();
            double best_loss = std::numeric_limits<double>::max();
            for (const auto& candidate : frontier)
            {
                // 最小化最大归一化遗憾：避免任一维度出现巨大退化。
                const double loss = pareto_regret(candidate, ranges);
                if (loss < best_loss - 1e-12 ||
                    (std::abs(loss - best_loss) <= 1e-12 &&
                     candidate_is_better(candidate, best)))
                {
                    best = candidate;
                    best_loss = loss;
                }
            }
            return best;
        }

        void initialize_legacy_shifts(PlaybackPlan& plan,
                                      const ShiftSearchContext& context)
        {
            if (context.full_range)
                return;

            if (context.groups.has_global && context.global_hist.size() == 128)
            {
                plan.global_shift = choose_legacy_shift(
                    context.global_hist,
                    context.config.min_pitch,
                    context.config.max_pitch);
            }

            for (std::size_t track = 0; track < context.groups.tracks.size(); ++track)
            {
                if (context.groups.tracks[track] &&
                    context.track_hists[track].size() == 128)
                {
                    plan.track_shifts[track] = choose_legacy_shift(
                        context.track_hists[track],
                        context.config.min_pitch,
                        context.config.max_pitch);
                }
            }
        }

        ShiftCandidateMetrics make_shift_metrics(int shift,
                                                 const CandidateScore& score,
                                                 const PlanBuild& build)
        {
            return {
                shift,
                score.center_score,
                build.metrics.retained_duration_weight,
                build.metrics.folded_duration_weight,
                build.metrics.conflict_merges,
                build.metrics.exact_duplicates,
                build.metrics.folded_notes,
                build.metrics.output_notes,
                build.metrics.unmapped_notes,
                false
            };
        }

        ShiftEvaluation evaluate_shift(
            const ShiftSearchContext& context,
            int shift,
            std::span<const int> track_shifts,
            std::span<const float> histogram,
            PlanBuild& build)
        {
            build_for_shifts({
                context.notes,
                shift,
                track_shifts,
                context.config,
                PlanOutputMode::METRICS_ONLY
            }, build);
            const CandidateScore score = score_plan(
                build,
                shift,
                center_shift_score(
                    histogram,
                    shift,
                    context.config.min_pitch,
                    context.config.max_pitch));
            return {
                score,
                make_shift_metrics(shift, score, build)
            };
        }

        void optimize_global_shift(PlaybackPlan& plan,
                                   const ShiftSearchContext& context)
        {
            if (!context.groups.has_global)
                return;

            const int preferred_shift = plan.global_shift;
            std::vector<CandidateScore> candidates;
            candidates.reserve(9);
            PlanBuild candidate_build;
            for (int octave = -4; octave <= 4; ++octave)
            {
                const int shift = octave * 12;
                ShiftEvaluation evaluation = evaluate_shift(
                    context, shift, plan.track_shifts,
                    context.global_hist, candidate_build);
                plan.global_candidates.push_back(evaluation.metrics);
                candidates.push_back(evaluation.score);
            }

            const auto frontier = pareto_frontier(candidates);
            mark_pareto_candidates(plan.global_candidates, frontier);
            const CandidateScore best = select_pareto_candidate(frontier, preferred_shift);
            plan.global_shift = best.shift;
        }

        void optimize_track_shifts(PlaybackPlan& plan,
                                   const ShiftSearchContext& context)
        {
            for (std::size_t track = 0; track < context.groups.tracks.size(); ++track)
            {
                if (!context.groups.tracks[track])
                    continue;

                const int preferred_shift = plan.track_shifts[track];
                std::vector<CandidateScore> candidates;
                candidates.reserve(9);
                PlanBuild candidate_build;
                for (int octave = -4; octave <= 4; ++octave)
                {
                    const int shift = octave * 12;
                    plan.track_shifts[track] = shift;
                    ShiftEvaluation evaluation = evaluate_shift(
                        context, plan.global_shift, plan.track_shifts,
                        context.track_hists[track], candidate_build);
                    candidates.push_back(evaluation.score);
                }

                const auto frontier = pareto_frontier(candidates);
                const CandidateScore best = select_pareto_candidate(
                    frontier, preferred_shift);
                plan.track_shifts[track] = best.shift;
            }
        }

        float compute_skewness(std::span<const float> hist, int low, int high)
        {
            double sum_weight = 0.0;
            double sum_pitch_weight = 0.0;
            for (int pitch = low; pitch <= high; ++pitch)
            {
                if (hist[pitch] <= 0.0f)
                    continue;
                const double weight = hist[pitch];
                sum_weight += weight;
                sum_pitch_weight += static_cast<double>(pitch) * weight;
            }

            if (sum_weight < 1e-10)
                return 0.0f;

            const double mean = sum_pitch_weight / sum_weight;
            double variance = 0.0;
            double skewness = 0.0;
            for (int pitch = low; pitch <= high; ++pitch)
            {
                if (hist[pitch] <= 0.0f)
                    continue;
                const double delta = static_cast<double>(pitch) - mean;
                const double weight = hist[pitch];
                variance += delta * delta * weight;
                skewness += delta * delta * delta * weight;
            }

            variance /= sum_weight;
            if (variance < 1e-10)
                return 0.0f;

            const double sigma = std::sqrt(variance);
            skewness /= sum_weight;
            return static_cast<float>(skewness / (sigma * sigma * sigma));
        }

        void apply_high_note_boost(std::span<float> hist, HistogramMode mode)
        {
            constexpr float kHighNoteBaseBoost = 1.2f;
            constexpr float kHighNoteStepDecay = 0.1f;
            constexpr int kMinBoostDepth = 4;
            constexpr int kBoostDepthPercent = 15;

            int highest_pitch = 60;
            for (int pitch = 127; pitch >= 0; --pitch)
            {
                if (hist[pitch] > 0.0f)
                {
                    highest_pitch = pitch;
                    break;
                }
            }

            int lowest_pitch = highest_pitch;
            for (int pitch = 0; pitch < highest_pitch; ++pitch)
            {
                if (hist[pitch] > 0.0f)
                {
                    lowest_pitch = pitch;
                    break;
                }
            }

            const int pitch_range = highest_pitch - lowest_pitch;
            const float skewness = compute_skewness(hist, lowest_pitch, highest_pitch);
            const float direction = mode == HistogramMode::LEGACY ? -1.0f : 1.0f;
            const float skew_adjust =
                1.0f + std::clamp(direction * skewness * 0.12f, -0.3f, 0.5f);
            const int boost_depth = std::max(
                kMinBoostDepth,
                static_cast<int>(pitch_range * kBoostDepthPercent / 100 * skew_adjust));
            const float base_boost =
                1.0f + (kHighNoteBaseBoost - 1.0f) * skew_adjust;

            const int start_pitch = std::max(0, highest_pitch - boost_depth + 1);
            for (int pitch = start_pitch; pitch <= highest_pitch; ++pitch)
            {
                if (hist[pitch] <= 0.0f)
                    continue;

                const float distance = static_cast<float>(highest_pitch - pitch);
                const float boost = base_boost - kHighNoteStepDecay * distance;
                if (boost > 1.0f)
                    hist[pitch] *= boost;
            }
        }
    }

    TransposeHistograms build_transpose_histograms(
        std::span<const Midi::RawNote> notes,
        std::size_t track_count,
        HistogramMode mode)
    {
        TransposeHistograms histograms;
        histograms.tracks.assign(track_count, std::vector<float>(128, 0.0f));
        histograms.global.assign(128, 0.0f);

        for (const auto& note : notes)
        {
            if (note.pitch < 0 || note.pitch >= 128)
                continue;

            const float weight =
                std::sqrt(std::max(0.0f, note.duration)) * note.velocity;
            if (note.track_index >= 0 &&
                static_cast<std::size_t>(note.track_index) < histograms.tracks.size())
            {
                histograms.tracks[note.track_index][note.pitch] += weight;
            }

            if (note.channel != 10 &&
                note.program != 43 &&
                (note.program < 33 || note.program > 40))
            {
                histograms.global[note.pitch] += weight;
            }
        }

        for (auto& track : histograms.tracks)
            apply_high_note_boost(track, mode);
        apply_high_note_boost(histograms.global, mode);
        return histograms;
    }

    PlaybackPlan build_playback_plan(
        std::span<const Midi::RawNote> input_notes,
        std::span<const std::vector<float>> track_hists,
        std::span<const float> global_hist,
        const PlannerConfig& config,
        PlanOutputMode output_mode)
    {
        PlaybackPlan plan;
        if (config.min_pitch < 0 || config.max_pitch > 127 ||
            config.min_pitch > config.max_pitch)
        {
            return plan;
        }

        plan.track_shifts.assign(track_hists.size(), 0);
        const auto channels = active_channels(config);
        ShiftSearchContext context{
            input_notes,
            track_hists,
            global_hist,
            config,
            find_auto_groups(channels, track_hists.size()),
            config.min_pitch == 0 && config.max_pitch == 127
        };

        initialize_legacy_shifts(plan, context);
        if (!context.full_range &&
            config.smart_algorithm == SmartTransposeAlgorithm::CONFLICT_AWARE)
        {
            optimize_global_shift(plan, context);
            optimize_track_shifts(plan, context);
        }

        PlanBuild final_build = build_for_shifts(
            {
                input_notes,
                plan.global_shift,
                plan.track_shifts,
                config,
                output_mode
            });
        plan.events = std::move(final_build.events);
        plan.notes = std::move(final_build.notes);
        plan.metrics = final_build.metrics;
        plan.ok = true;
        return plan;
    }
}
