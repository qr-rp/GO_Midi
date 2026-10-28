#include "core/TransposePlanner.h"
#include "midi/MidiParser.h"
#include "util/KeyManager.h"
#include "util/Logger.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct Variant
    {
        std::string name;
        Core::SmartTransposeAlgorithm algorithm;
        Core::ConflictKeyMode conflict_mode;
    };

    void write_source_notes(const std::filesystem::path& path,
                            const std::vector<Midi::RawNote>& notes,
                            bool exclude_percussion)
    {
        std::ofstream output(path);
        output << "start,end,pitch,velocity,track,channel,program,duration\n";
        output << std::fixed << std::setprecision(6);
        for (const auto& note : notes)
        {
            if (exclude_percussion && note.channel == 10)
                continue;
            output << note.start_s << ','
                   << note.start_s + note.duration << ','
                   << note.pitch << ','
                   << note.velocity << ','
                   << note.track_index << ','
                   << note.channel << ','
                   << note.program << ','
                   << note.duration << '\n';
        }
    }

    void write_plan_notes(const std::filesystem::path& path,
                          const Core::PlaybackPlan& plan)
    {
        std::ofstream output(path);
        output << "start,end,pitch,velocity,track,folded\n";
        output << std::fixed << std::setprecision(6);
        for (const auto& note : plan.notes)
        {
            output << note.start << ','
                   << note.end << ','
                   << note.pitch << ','
                   << note.velocity << ','
                   << note.track << ','
                   << (note.folded ? 1 : 0) << '\n';
        }
    }

    void write_plan_events(const std::filesystem::path& path,
                           const Core::PlaybackPlan& plan)
    {
        std::ofstream output(path);
        output << "time,note_on,vk,modifier\n";
        output << std::fixed << std::setprecision(6);
        for (const auto& event : plan.events)
        {
            output << event.time << ','
                   << (event.is_note_on ? 1 : 0) << ','
                   << event.vk_code << ','
                   << event.modifier << '\n';
        }
    }

    void write_summary_row(std::ostream& output,
                           const std::string& variant,
                           const Core::PlaybackPlan& plan)
    {
        const double retention = plan.metrics.source_duration_weight > 0.0
            ? plan.metrics.retained_duration_weight * 100.0 /
                  plan.metrics.source_duration_weight
            : 100.0;
        output << variant << ','
               << plan.global_shift << ','
               << plan.metrics.source_notes << ','
               << plan.metrics.mapped_notes << ','
               << plan.metrics.unmapped_notes << ','
               << plan.metrics.folded_notes << ','
               << plan.metrics.conflict_merges << ','
               << plan.metrics.exact_duplicates << ','
               << plan.metrics.output_notes << ','
               << std::fixed << std::setprecision(3)
               << retention << '\n';
    }

    const char* algorithm_name(Core::SmartTransposeAlgorithm algorithm)
    {
        return algorithm == Core::SmartTransposeAlgorithm::LEGACY
            ? "legacy"
            : "conflict_aware";
    }

    const char* conflict_name(Core::ConflictKeyMode mode)
    {
        return mode == Core::ConflictKeyMode::PHYSICAL
            ? "physical"
            : "logical";
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: transpose_compare <input.mid> <output-dir> [36|37]\n";
        return EXIT_FAILURE;
    }

    const int keyboard_size = argc >= 4 ? std::atoi(argv[3]) : 36;
    if (keyboard_size != 36 && keyboard_size != 37)
    {
        std::cerr << "Keyboard size must be 36 or 37\n";
        return EXIT_FAILURE;
    }

    Util::Logger::Instance().Initialize(Util::LogLevel::Error, "", false);

    const std::filesystem::path input_path(argv[1]);
    const std::filesystem::path output_dir(argv[2]);
    std::filesystem::create_directories(output_dir);

    Midi::MidiFile midi(input_path.wstring());
    if (!midi.is_valid())
    {
        std::cerr << "Failed to parse MIDI: " << midi.error_msg() << '\n';
        return EXIT_FAILURE;
    }

    std::vector<Midi::RawNote> notes;
    for (const auto& track : midi.raw_notes_by_track)
        notes.insert(notes.end(), track.begin(), track.end());
    std::sort(notes.begin(), notes.end(), [](const Midi::RawNote& left, const Midi::RawNote& right)
    {
        return left.start_s < right.start_s;
    });

    write_source_notes(output_dir / "source_all.csv", notes, false);
    write_source_notes(output_dir / "source_melodic.csv", notes, true);

    const auto legacy_histograms = Core::build_transpose_histograms(
        notes, midi.raw_notes_by_track.size(), Core::HistogramMode::LEGACY);
    const auto improved_histograms = Core::build_transpose_histograms(
        notes, midi.raw_notes_by_track.size(), Core::HistogramMode::CORRECTED);

    Util::KeyManager key_manager;
    const int min_pitch = 48;
    const int max_pitch = keyboard_size == 36 ? 83 : 84;
    if (keyboard_size == 36)
        key_manager.load_yysls_preset();

    Core::PlannerConfig base_config;
    base_config.min_pitch = min_pitch;
    base_config.max_pitch = max_pitch;
    base_config.decompose = false;
    base_config.smart_algorithm = Core::SmartTransposeAlgorithm::CONFLICT_AWARE;
    base_config.conflict_key_mode = Core::ConflictKeyMode::PHYSICAL;

    Core::PlannerChannelConfig channel;
    channel.channel_index = 0;
    channel.track_index = -1;
    channel.transpose = 0;
    channel.enabled = true;
    base_config.channels.push_back(channel);

    for (const auto& [pitch, mapping] : key_manager.get_map())
        base_config.keymap[pitch] = {mapping.vk_code, mapping.modifier};

    std::vector<Variant> variants = {
        {"legacy_physical", Core::SmartTransposeAlgorithm::LEGACY,
         Core::ConflictKeyMode::PHYSICAL},
        {"improved_physical", Core::SmartTransposeAlgorithm::CONFLICT_AWARE,
         Core::ConflictKeyMode::PHYSICAL},
        {"improved_logical", Core::SmartTransposeAlgorithm::CONFLICT_AWARE,
         Core::ConflictKeyMode::LOGICAL}
    };

    std::ofstream summary(output_dir / "summary.csv");
    summary << "variant,global_shift,source_notes,mapped,unmapped,folded,"
               "conflict_merges,exact_duplicates,output_notes,retained_percent\n";

    for (const auto& variant : variants)
    {
        Core::PlannerConfig config = base_config;
        config.smart_algorithm = variant.algorithm;
        config.conflict_key_mode = variant.conflict_mode;
        const auto& histograms = variant.algorithm == Core::SmartTransposeAlgorithm::LEGACY
            ? legacy_histograms
            : improved_histograms;

        Core::PlaybackPlan plan = Core::build_playback_plan(
            notes, histograms.tracks, histograms.global, config);
        if (!plan.ok)
        {
            std::cerr << "Failed to build plan: " << variant.name << '\n';
            return EXIT_FAILURE;
        }

        write_plan_notes(output_dir / (variant.name + "_notes.csv"), plan);
        write_plan_events(output_dir / (variant.name + "_events.csv"), plan);
        write_summary_row(summary, variant.name, plan);

        if (!plan.global_candidates.empty())
        {
            std::ofstream candidates(output_dir / (variant.name + "_candidates.csv"));
            candidates << "shift,center_score,retained_weight,folded_weight,"
                          "conflicts,exact_duplicates,folded_notes,output_notes,"
                          "unmapped,pareto_optimal\n";
            candidates << std::fixed << std::setprecision(6);
            for (const auto& candidate : plan.global_candidates)
            {
                candidates << candidate.shift << ','
                           << candidate.center_score << ','
                           << candidate.retained_duration_weight << ','
                           << candidate.folded_duration_weight << ','
                           << candidate.conflicts << ','
                           << candidate.exact_duplicates << ','
                           << candidate.folded_notes << ','
                           << candidate.output_notes << ','
                           << candidate.unmapped_notes << ','
                           << (candidate.pareto_optimal ? 1 : 0) << '\n';
            }
        }

        std::cout << variant.name
                  << " algorithm=" << algorithm_name(variant.algorithm)
                  << " conflict=" << conflict_name(variant.conflict_mode)
                  << " shift=" << plan.global_shift
                  << " retained=" << std::fixed << std::setprecision(2)
                  << (plan.metrics.source_duration_weight > 0.0
                          ? plan.metrics.retained_duration_weight * 100.0 /
                                plan.metrics.source_duration_weight
                          : 100.0)
                  << "% conflicts=" << plan.metrics.conflict_merges
                  << " folded=" << plan.metrics.folded_notes
                  << '\n';
    }

    std::ofstream info(output_dir / "metadata.txt");
    info << "input=" << input_path.string() << '\n';
    info << "keyboard_size=" << keyboard_size << '\n';
    info << "pitch_range=" << min_pitch << '-' << max_pitch << '\n';
    info << "tracks=" << midi.raw_notes_by_track.size() << '\n';
    info << "notes=" << notes.size() << '\n';
    info << "duration=" << std::fixed << std::setprecision(3) << midi.length << '\n';
    info << "keymap_entries=" << base_config.keymap.size() << '\n';
    return EXIT_SUCCESS;
}
