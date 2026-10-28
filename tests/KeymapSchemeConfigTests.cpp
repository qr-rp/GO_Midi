#include "ui/KeymapSchemeConfig.h"

#include <wx/fileconf.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
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
}

int main()
{
    const std::vector<wxString> names = {"Alpha", "Beta"};
    expect(UI::KeymapSchemeStore::find_scheme_index(names, "Beta") == 1,
           "scheme index should use list order");
    expect(UI::KeymapSchemeStore::find_scheme_index(names, "Missing") == -1,
           "missing scheme should return -1");

    const auto path = std::filesystem::temp_directory_path() /
                      "go_midi_keymap_scheme_tests.ini";
    {
        wxFileConfig config(
            "GO_MidiTests", "Codex",
            wxString(path.wstring()),
            wxString(),
            wxCONFIG_USE_LOCAL_FILE);
        const std::map<int, Util::KeyMapping> mapping = {
            {60, {'Q', Util::kModNone}},
            {61, {'Q', Util::kModShift}}
        };
        UI::KeymapSchemeStore::write_scheme_map(&config, names, "Beta", mapping);
        UI::KeymapSchemeStore::write_scheme_pitch(&config, names, "Beta", 50, 86);
        UI::KeymapSchemeStore::write_scheme_pitch(
            &config, names, "@builtin_0", 47, 85);
        config.Flush();
    }

    {
        wxFileConfig config(
            "GO_MidiTests", "Codex",
            wxString(path.wstring()),
            wxString(),
            wxCONFIG_USE_LOCAL_FILE);
        std::map<int, Util::KeyMapping> mapping;
        expect(UI::KeymapSchemeStore::read_scheme_map(
                   &config, names, "Beta", mapping),
               "stored scheme map should load");
        expect(mapping.size() == 2, "stored scheme map should retain both notes");
        expect(mapping.at(61).vk_code == 'Q' &&
                   mapping.at(61).modifier == Util::kModShift,
               "stored modifiers should round-trip");

        int min_pitch = 0;
        int max_pitch = 0;
        expect(UI::KeymapSchemeStore::read_scheme_pitch(
                   &config, names, "Beta", min_pitch, max_pitch),
               "stored scheme pitch should load");
        expect(min_pitch == 50 && max_pitch == 86,
               "stored scheme pitch should round-trip");
        expect(UI::KeymapSchemeStore::read_scheme_pitch(
                   &config, names, "@builtin_0", min_pitch, max_pitch),
               "builtin FF14 pitch should load");
        expect(min_pitch == 47 && max_pitch == 85,
               "builtin FF14 pitch should round-trip");
    }

    std::filesystem::remove(path);
    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "KeymapSchemeConfig tests passed\n";
    return EXIT_SUCCESS;
}
