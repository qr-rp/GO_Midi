#include "util/KeyManager.h"
#include "util/Logger.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

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

    void test_builtin_presets()
    {
        Util::KeyManager manager;
        auto default_map = manager.get_map();
        expect(default_map.size() == 37, "FF14 preset should contain 37 notes");
        expect(default_map[60].vk_code == 'Q', "FF14 C4 should map to Q");
        expect(default_map[60].modifier == 0, "FF14 C4 should not use a modifier");

        manager.load_yysls_preset();
        auto yysls_map = manager.get_map();
        expect(yysls_map.size() == 36, "Yysls preset should contain 36 notes");
        expect(yysls_map[48].vk_code == 'Z', "Yysls C3 should map to Z");
        expect(yysls_map[49].vk_code == 'Z' && yysls_map[49].modifier == Util::kModShift,
               "Yysls C#3 should map to Shift+Z");
    }

    void test_map_roundtrip_and_reset()
    {
        Util::KeyManager manager;
        std::map<int, Util::KeyMapping> custom = {
            {60, {'A', Util::kModShift}},
            {64, {'S', Util::kModNone}}
        };

        manager.set_map(custom);
        const auto roundtrip = manager.get_map();
        expect(roundtrip.size() == custom.size(), "set_map/get_map should preserve map size");
        expect(roundtrip.at(60).vk_code == 'A' &&
                   roundtrip.at(60).modifier == Util::kModShift,
               "set_map/get_map should round-trip a modified mapping");
        expect(roundtrip.at(64).vk_code == 'S' &&
                   roundtrip.at(64).modifier == Util::kModNone,
               "set_map/get_map should round-trip an unmodified mapping");

        manager.reset_to_default();
        auto reset_map = manager.get_map();
        expect(reset_map.size() == 37, "reset_to_default should restore the default preset");
        expect(reset_map[60].vk_code == 'Q', "reset_to_default should restore FF14 mappings");
    }

    void test_utf8_config_loading()
    {
        const auto path = std::filesystem::temp_directory_path() / "go_midi_keymap_utf8.txt";
        {
            std::ofstream file(path, std::ios::binary);
            file << "\xEF\xBB\xBF"
                 << "# UTF-8 BOM keymap\n"
                 << "60: q\n"
                 << "61: 2\n";
        }

        Util::KeyManager manager;
        expect(manager.load_config(path.wstring()), "UTF-8 BOM keymap should load");
        const auto map = manager.get_map();
        expect(map.size() == 2, "UTF-8 keymap should contain two mappings");
        expect(map.at(60).vk_code == 'Q', "UTF-8 keymap should parse note 60");
        expect(map.at(61).vk_code == '2', "UTF-8 keymap should parse note 61");
        std::filesystem::remove(path);
    }
}

int main()
{
    Util::Logger::Instance().Initialize(Util::LogLevel::Error, "", false);

    test_builtin_presets();
    test_map_roundtrip_and_reset();
    test_utf8_config_loading();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "KeyManager tests passed\n";
    return EXIT_SUCCESS;
}
