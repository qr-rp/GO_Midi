#include "midi/MidiParser.h"
#include "util/Logger.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

    void test_fixture(const std::filesystem::path& root,
                      const std::string& filename,
                      std::size_t minimum_tracks,
                      std::size_t minimum_notes)
    {
        const auto path = root / filename;
        Midi::MidiFile midi(path.wstring());
        expect(midi.is_valid(), filename + " should parse");
        expect(midi.tracks.size() >= minimum_tracks,
               filename + " should expose expected tracks");
        expect(midi.raw_notes_by_track.size() == midi.tracks.size(),
               filename + " should keep note groups aligned with tracks");
        expect(midi.length > 0.0f, filename + " should have positive duration");

        std::size_t note_count = 0;
        for (const auto& track : midi.raw_notes_by_track)
        {
            note_count += track.size();
            for (const auto& note : track)
            {
                expect(note.pitch >= 0 && note.pitch <= 127,
                       filename + " note pitch should be in MIDI range");
                expect(note.duration >= 0.0f,
                       filename + " note duration should be non-negative");
                expect(note.channel >= 1 && note.channel <= 16,
                       filename + " note channel should use the public 1-16 convention");
            }
        }
        expect(note_count >= minimum_notes, filename + " should parse expected notes");
    }
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: midi_parser_tests <fixture-dir>\n";
        return EXIT_FAILURE;
    }

    Util::Logger::Instance().Initialize(Util::LogLevel::Error, "", false);
    const std::filesystem::path root(argv[1]);
    test_fixture(root, "frere-jacques.mid", 5, 300);
    test_fixture(root, "maple-leaf-rag.mid", 3, 2000);
    test_fixture(root, "chopin-etude-10-4.mid", 2, 2000);
    test_fixture(root, "bach-brandenburg-3.mid", 11, 20000);
    test_fixture(root, "beethoven-symphony-5.mid", 13, 8000);
    test_fixture(root, "star-spangled-hillery.mid", 3, 300);

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "MidiParser tests passed\n";
    return EXIT_SUCCESS;
}
