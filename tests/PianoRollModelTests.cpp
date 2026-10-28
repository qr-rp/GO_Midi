#include "ui/PianoRollModel.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>

namespace {

    int g_failures = 0;

    void expect(bool condition, const std::string& message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    UI::PianoRollLayout make_layout(int min_note, int max_note)
    {
        return UI::PianoRollLayout({min_note, max_note, 30, 20, 92, 56});
    }

    void expect_geometry(const UI::PianoRollLayout& layout, int note,
                         int x, int width, const std::string& message)
    {
        const auto key = layout.key_geometry(note);
        expect(key.x == x && key.width == width, message);
    }

    void test_c_octave_layout()
    {
        const auto layout = make_layout(60, 72);

        expect_geometry(layout, 60, 0, 30, "C4 should start at x=0");
        expect_geometry(layout, 61, 20, 20,
                        "C#4 should straddle the C/D boundary");
        expect_geometry(layout, 62, 30, 30, "D4 should follow C4");
        expect(layout.content_width() == 240,
               "C4 through C5 inclusive should occupy eight white-key widths");
    }

    void test_black_key_start_does_not_overlap()
    {
        const auto layout = make_layout(61, 71);
        const auto c_sharp = layout.key_geometry(61);
        const auto d_sharp = layout.key_geometry(63);

        expect_geometry(layout, 61, 0, 20,
                        "the first visible black key should remain fully visible");
        expect_geometry(layout, 62, 10, 30,
                        "the first white key should follow the black-key offset");
        expect_geometry(layout, 63, 30, 20,
                        "visible keys should retain their real chromatic spacing");
        expect(c_sharp.x != d_sharp.x,
               "C# should not be painted at D#'s position when the range starts on C#");
        expect(layout.content_width() == 190,
               "C#4 through B4 should include the left half-black-key offset");
        expect(layout.hit_test(15, 20) == 61,
               "black keys should win over overlapping white keys");
        expect(layout.hit_test(15, 70) == 62,
               "the lower part of the overlapping area should hit the white D key");
        expect(layout.hit_test(-1, 20) == -1,
               "negative content coordinates should miss");
        expect(layout.hit_test(15, 92) == -1,
               "coordinates below the white keyboard should miss");
    }

    void test_conflict_analysis_distinguishes_identity_modes()
    {
        std::map<int, Util::KeyMapping> map = {
            {60, {'A', Util::kModNone}},
            {61, {'A', Util::kModNone}},
            {62, {'A', Util::kModShift}},
            {72, {'B', Util::kModNone}}
        };

        const auto analysis = UI::analyze_key_bindings(map, 60, 71);
        expect(analysis.bound_notes == 4, "all valid mappings should be counted");
        expect(analysis.unbound_in_range == 9,
               "twelve-note range with three mapped notes should have nine unbound notes");
        expect(analysis.logical_duplicates == std::set<int>({60, 61}),
               "logical duplicates should require the same VK and modifier");
        expect(analysis.physical_conflicts == std::set<int>({60, 61, 62}),
               "physical conflicts should group by VK regardless of modifier");
        expect(analysis.out_of_range == std::set<int>({72}),
               "mappings outside the target range should be reported");
    }

    void test_analysis_ignores_invalid_vk()
    {
        std::map<int, Util::KeyMapping> map = {
            {60, {0, Util::kModNone}},
            {61, {'K', Util::kModCtrl}}
        };

        const auto analysis = UI::analyze_key_bindings(map, 60, 61);
        expect(analysis.bound_notes == 1, "invalid VK entries should not count as bound");
        expect(analysis.unbound_in_range == 1,
               "a note with an invalid VK should still count as unbound");
        expect(analysis.physical_conflicts.empty(),
               "invalid VK entries should not create conflict groups");
    }

} // namespace

int main()
{
    test_c_octave_layout();
    test_black_key_start_does_not_overlap();
    test_conflict_analysis_distinguishes_identity_modes();
    test_analysis_ignores_invalid_vk();

    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Piano roll model tests passed\n";
    return EXIT_SUCCESS;
}
