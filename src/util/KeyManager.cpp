#include "KeyManager.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace {
    std::string to_lower_copy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string trim_copy(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
        return s.substr(start, end - start);
    }

    void replace_all(std::string& s, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    std::string normalize_line(std::string s) {
        replace_all(s, u8"：", ":");
        replace_all(s, u8"＝", "=");
        replace_all(s, u8"－", "-");
        replace_all(s, u8"＋", "+");
        replace_all(s, u8"　", " ");
        replace_all(s, u8"（", "(");
        replace_all(s, u8"）", ")");
        return s;
    }

    bool is_digits(const std::string& s) {
        if (s.empty()) return false;
        return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
    }

    const std::map<std::string, int>& get_vk_map() {
        static std::map<std::string, int> map = {
            {"q", 0x51}, {"w", 0x57}, {"e", 0x45}, {"r", 0x52}, {"t", 0x54}, {"y", 0x59}, {"u", 0x55}, {"i", 0x49}, {"o", 0x4F}, {"p", 0x50},
            {"a", 0x41}, {"s", 0x53}, {"d", 0x44}, {"f", 0x46}, {"g", 0x47}, {"h", 0x48}, {"j", 0x4A}, {"k", 0x4B}, {"l", 0x4C},
            {"z", 0x5A}, {"x", 0x58}, {"c", 0x43}, {"v", 0x56}, {"b", 0x42}, {"n", 0x4E}, {"m", 0x4D},
            {"1", 0x31}, {"2", 0x32}, {"3", 0x33}, {"4", 0x34}, {"5", 0x35}, {"6", 0x36}, {"7", 0x37}, {"8", 0x38}, {"9", 0x39}, {"0", 0x30},
            {"[", 0xDB}, {"]", 0xDD}, {"\\", 0xDC}, {"'", 0xDE}, {"-", 0xBD}, {"=", 0xBB}, {"+", 0xBB}, {"/", 0xBF}, {",", 0xBC}, {".", 0xBE}, {";", 0xBA},
            {"`", 0xC0}
        };
        return map;
    }

    const std::map<int, std::string>& get_vk_reverse_map() {
        static std::map<int, std::string> map = [] {
            std::map<int, std::string> r;
            for (const auto& pair : get_vk_map()) {
                r[pair.second] = pair.first;
            }
            return r;
        }();
        return map;
    }
}

namespace Util {

    KeyManager::KeyManager() {
        init_default_map();
    }

    KeyMapping KeyManager::get_mapping(int note) {
        // 优化：使用 O(1) 缓存数组查找，避免 std::map 的 O(log n) 开销
        if (note >= 0 && note < 128 && m_lookup_valid[note]) {
            return m_lookup_cache[note];
        }
        return {0, 0};
    }

    bool KeyManager::load_config(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return false;

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        std::map<int, KeyMapping> new_map;

        // Only support TXT format
        std::istringstream lines(content);
        std::string line;
        // 优化：静态缓存正则表达式，避免每次调用重新编译
        static const std::regex re(R"((?:音符\s+)?([A-G][#B]?\d+|\d+)(?:\s*\(.*?\))?[\s]*[:=\-\s]+[\s]*([^\s]+))", std::regex::icase | std::regex::optimize);
        while (std::getline(lines, line)) {
            line = trim_copy(line);
            if (line.empty()) continue;
            std::string lower = to_lower_copy(line);
            if (!lower.empty() && (lower[0] == '#' || lower[0] == '-')) continue;

            line = normalize_line(line);
            std::smatch match;
            if (std::regex_search(line, match, re)) {
                std::string key_part = match[1].str();
                std::string value_part = match[2].str();

                int pitch = -1;
                if (is_digits(key_part)) {
                    pitch = std::stoi(key_part);
                } else {
                    if (!get_pitch_from_name(key_part, pitch)) {
                        continue;
                    }
                }

                int vk = 0;
                int modifier = 0;
                if (parse_key_string(value_part, vk, modifier)) {
                    new_map[pitch] = {vk, modifier};
                }
            }
        }
        
        if (!new_map.empty()) {
            m_note_map = new_map;
            rebuild_lookup_cache();
            return true;
        }
        return false;
    }

    bool KeyManager::save_config(const std::string& path) const {
        std::vector<int> keys;
        for (const auto& pair : m_note_map) {
            keys.push_back(pair.first);
        }
        std::sort(keys.begin(), keys.end());

        std::vector<std::pair<int, std::string>> entries;
        entries.reserve(keys.size());
        for (int k : keys) {
            auto it = m_note_map.find(k);
            if (it == m_note_map.end()) continue;
            std::string key_str = format_key_string(it->second.vk_code, it->second.modifier);
            if (key_str.empty()) continue;
            entries.push_back({k, key_str});
        }

        std::ostringstream out;

        // Always save as TXT
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &time);
        out << " ################################################################\n";
        out << " # MIDI 键位映射配置文件\n";
        out << " # 导出时间: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
        out << " ################################################################\n";
        out << " #\n";
        out << " # [编写规则说明]\n";
        out << " # 1. 每行定义一个音符映射，格式为: 音符(或音名) 分隔符 按键\n";
        out << " # 2. 音符表示法: 支持 MIDI 编号 (如 60) 或 音名 (如 C4, C#4, Eb4)\n";
        out << " # 3. 分隔符: 支持 冒号(:)、等号(=)、减号(-)、空格 或 全角符号(：、＝、－)\n";
        out << " # 4. 修饰符: 在按键后加 '+' 表示 Shift，加 '-' 表示 Ctrl\n";
        out << " # 5. 自由度: 所有的符号都不分全角/半角，且不区分大小写\n";
        out << " #\n";
        out << " # [示例格式]\n";
        out << " #   60: z            (半角冒号)\n";
        out << " #   C4 = x           (音名 + 等号)\n";
        out << " #   音符 62 (D4)：c  (带备注 + 全角冒号)\n";
        out << " #   64　v            (全角空格)\n";
        out << " #\n";
        out << " ################################################################\n";
        out << "\n";

        for (const auto& entry : entries) {
            std::string name = get_note_name(entry.first);
            out << " 音符 " << entry.first << " (" << name << "): " << entry.second << "\n";
        }

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return false;
        file << out.str();
        return true;
    }

    void KeyManager::set_map(const std::map<int, KeyMapping>& map) {
        m_note_map = map;
        rebuild_lookup_cache();
    }

    const std::map<int, KeyMapping>& KeyManager::get_map() const {
        return m_note_map;
    }

    void KeyManager::reset_to_default() {
        init_default_map();
    }

    void KeyManager::init_default_map() {
        m_note_map.clear();
        
        // Default mapping from original python source (key_manager.py)
        // Note: Default mapping does not use modifiers (Shift=1, Ctrl=2)
        
        // High range / Special chars
        m_note_map[48] = {'I', 0};        // i
        m_note_map[50] = {'O', 0};        // o
        m_note_map[52] = {'P', 0};        // p
        m_note_map[53] = {VK_OEM_4, 0};   // [
        m_note_map[55] = {VK_OEM_6, 0};   // ]
        m_note_map[57] = {VK_OEM_5, 0};   // \ (backslash)
        m_note_map[59] = {VK_OEM_7, 0};   // ' (quote)

        // Mid range (QWERTY row)
        m_note_map[60] = {'Q', 0};
        m_note_map[62] = {'W', 0};
        m_note_map[64] = {'E', 0};
        m_note_map[65] = {'R', 0};
        m_note_map[67] = {'T', 0};
        m_note_map[69] = {'Y', 0};
        m_note_map[71] = {'U', 0};

        // Low range / Numbers
        m_note_map[81] = {'N', 0};
        m_note_map[83] = {'M', 0};
        m_note_map[49] = {'8', 0};
        m_note_map[51] = {'9', 0};
        m_note_map[54] = {'0', 0};
        m_note_map[56] = {VK_OEM_MINUS, 0}; // -
        m_note_map[58] = {VK_OEM_PLUS, 0};  // =

        // Numbers row
        m_note_map[61] = {'2', 0};
        m_note_map[63] = {'3', 0};
        m_note_map[66] = {'5', 0};
        m_note_map[68] = {'6', 0};
        m_note_map[70] = {'7', 0};

        // H, J
        m_note_map[80] = {'H', 0};
        m_note_map[82] = {'J', 0};

        // Bottom row (ZXCV...)
        m_note_map[72] = {'Z', 0};
        m_note_map[73] = {'S', 0};
        m_note_map[74] = {'X', 0};
        m_note_map[75] = {'D', 0};
        m_note_map[76] = {'C', 0};
        m_note_map[77] = {'V', 0};
        m_note_map[78] = {'G', 0};
        m_note_map[79] = {'B', 0};
        m_note_map[84] = {VK_OEM_2, 0};     // /
        rebuild_lookup_cache();
    }

    void KeyManager::rebuild_lookup_cache() {
        std::memset(m_lookup_valid, 0, sizeof(m_lookup_valid));
        for (const auto& pair : m_note_map) {
            if (pair.first >= 0 && pair.first < 128) {
                m_lookup_cache[pair.first] = pair.second;
                m_lookup_valid[pair.first] = true;
            }
        }
    }

    std::string KeyManager::format_key_string(int vk, int modifier) const {
        const auto& rev = get_vk_reverse_map();
        auto it = rev.find(vk);
        if (it == rev.end()) return "";
        std::string key = it->second;
        if (modifier == 1) key += "+";
        else if (modifier == 2) key += "-";
        return key;
    }

    bool KeyManager::parse_key_string(const std::string& key_str, int& vk, int& modifier) const {
        std::string s = normalize_line(to_lower_copy(trim_copy(key_str)));
        if (s.empty()) return false;

        modifier = 0;
        if (s.size() > 1) {
            char last = s.back();
            if (last == '+') {
                modifier = 1;
                s.pop_back();
            } else if (last == '-') {
                modifier = 2;
                s.pop_back();
            }
        }

        s = trim_copy(s);
        const auto& map = get_vk_map();
        auto it = map.find(s);
        if (it == map.end()) return false;
        vk = it->second;
        return true;
    }

    std::string KeyManager::get_note_name(int midi_pitch) const {
        static const std::string notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        if (midi_pitch < 0 || midi_pitch > 127) return "";
        int octave = (midi_pitch / 12) - 1;
        int idx = midi_pitch % 12;
        return notes[idx] + std::to_string(octave);
    }

    bool KeyManager::get_pitch_from_name(const std::string& name, int& pitch) const {
        std::regex re(R"(^\s*([A-Ga-g])([#bB]?)(-?\d+)\s*$)");
        std::smatch match;
        if (!std::regex_match(name, match, re)) return false;

        std::string note = match[1].str();
        std::string acc = match[2].str();
        std::string octave_str = match[3].str();

        std::string key = to_lower_copy(note);
        if (!acc.empty()) {
            char c = acc[0];
            if (c == '#' || c == 'b' || c == 'B') {
                key += (c == '#') ? "#" : "b";
            }
        }

        static const std::map<std::string, int> notes_map = {
            {"c", 0}, {"c#", 1}, {"db", 1}, {"d", 2}, {"d#", 3}, {"eb", 3}, {"e", 4},
            {"f", 5}, {"f#", 6}, {"gb", 6}, {"g", 7}, {"g#", 8}, {"ab", 8}, {"a", 9},
            {"a#", 10}, {"bb", 10}, {"b", 11}
        };

        auto it = notes_map.find(key);
        if (it == notes_map.end()) return false;
        int octave = std::stoi(octave_str);
        int value = (octave + 1) * 12 + it->second;
        if (value < 0 || value > 127) return false;
        pitch = value;
        return true;
    }

}
