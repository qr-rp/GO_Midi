#include "KeyManager.h"
#include "Logger.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace
{
    std::string to_lower_copy(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string trim_copy(const std::string &s)
    {
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
            start++;
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
            end--;
        return s.substr(start, end - start);
    }

    void replace_all(std::string &s, const std::string &from, const std::string &to)
    {
        if (from.empty())
            return;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos)
        {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    std::string normalize_line(std::string s)
    {
        replace_all(s, u8"：", ":");
        replace_all(s, u8"＝", "=");
        replace_all(s, u8"－", "-");
        replace_all(s, u8"＋", "+");
        replace_all(s, u8"　", " ");
        replace_all(s, u8"（", "(");
        replace_all(s, u8"）", ")");
        return s;
    }

    bool is_digits(const std::string &s)
    {
        if (s.empty())
            return false;
        return std::all_of(s.begin(), s.end(), [](unsigned char c)
                           { return std::isdigit(c); });
    }

    // 编码信息结构体
    struct EncodingInfo
    {
        const char *name;
        UINT codepage;
        int priority; // 优先级，数值越小优先级越高
    };

    // 使用字节频率分析来估算编码类型
    EncodingInfo analyze_encoding(const std::vector<char> &buffer)
    {
        if (buffer.empty())
            return {"System", GetACP(), 100};

        // 统计字节频率
        int high_bytes = 0;      // 0x80-0xFF 范围内的字节数
        int null_bytes = 0;      // 0x00 字节数
        int ascii_printable = 0; // 可打印 ASCII 字符数
        int total = static_cast<int>(buffer.size());

        for (int i = 0; i < total; ++i)
        {
            unsigned char c = static_cast<unsigned char>(buffer[i]);
            if (c >= 0x80)
                high_bytes++;
            else if (c == 0)
                null_bytes++;
            else if (c >= 0x20 && c < 0x7F)
                ascii_printable++;
        }

        double high_byte_ratio = static_cast<double>(high_bytes) / total;

        // 如果高层字节很少，可能是纯 ASCII 或 UTF-8
        if (high_byte_ratio < 0.01)
        {
            // 可能是 ASCII 或 UTF-8，优先级较低
            return {"UTF-8", CP_UTF8, 50};
        }

        // 分析高层字节模式来猜测编码
        // GBK/GB2312: 高字节范围 0xA1-0xF7, 低字节范围 0xA1-0xFE
        // Big5: 高字节范围 0xA1-0xF9, 低字节范围 0x40-0x7E 或 0xA1-0xFE
        // Shift-JIS: 高字节范围 0x81-0x9F 或 0xE0-0xFC
        // ISO-8859-1: 高字节通常是 0xC0-0xFF 的欧洲字符
        // Windows-1252: 高字节范围 0x80-0x9F 有特殊字符

        // 检查是否有明显的 GBK/GB2312 特征
        bool has_gbk_pattern = false;
        bool has_big5_pattern = false;
        bool has_sjis_pattern = false;
        bool has_western_pattern = false;

        for (int i = 0; i < total - 1; ++i)
        {
            unsigned char c1 = static_cast<unsigned char>(buffer[i]);
            unsigned char c2 = static_cast<unsigned char>(buffer[i + 1]);

            if (c1 >= 0xA1 && c1 <= 0xF7 && c2 >= 0xA1 && c2 <= 0xFE)
            {
                has_gbk_pattern = true;
            }
            else if (c1 >= 0xA1 && c1 <= 0xF9 &&
                     ((c2 >= 0x40 && c2 <= 0x7E) || (c2 >= 0xA1 && c2 <= 0xFE)))
            {
                has_big5_pattern = true;
            }
            else if ((c1 >= 0x81 && c1 <= 0x9F) || (c1 >= 0xE0 && c1 <= 0xFC))
            {
                if ((c2 >= 0x40 && c2 <= 0x7E) || (c2 >= 0x80 && c2 <= 0xFC))
                    has_sjis_pattern = true;
            }
            else if (c1 >= 0xC0 && c1 <= 0xFF && c2 >= 0x80 && c2 <= 0xFF)
            {
                // 可能是西欧字符
                has_western_pattern = true;
            }
        }

        // 根据特征返回最可能的编码
        if (has_gbk_pattern)
            return {"GBK", 936, 10};
        if (has_big5_pattern)
            return {"Big5", 950, 20};
        if (has_sjis_pattern)
            return {"Shift-JIS", 932, 30};
        if (has_western_pattern)
            return {"Windows-1252", 1252, 40};

        // 如果没有明显特征，使用默认优先级
        return {"System", GetACP(), 100};
    }

    // 尝试使用指定编码转换
    bool try_convert_to_utf8(const std::vector<char> &buffer, UINT codepage, std::string &result)
    {
        // 使用 MB_ERR_INVALID_CHARS 标志（如果支持）来验证转换
        int wide_len = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS,
                                           buffer.data(), static_cast<int>(buffer.size()), nullptr, 0);

        // 如果验证失败，尝试不带验证的转换
        if (wide_len == 0)
        {
            wide_len = MultiByteToWideChar(codepage, 0,
                                           buffer.data(), static_cast<int>(buffer.size()), nullptr, 0);
        }

        if (wide_len == 0)
            return false;

        std::vector<wchar_t> wide_buffer(wide_len);
        int converted = MultiByteToWideChar(codepage, 0,
                                            buffer.data(), static_cast<int>(buffer.size()), wide_buffer.data(), wide_len);

        if (converted == 0)
            return false;

        // 再转换为 UTF-8
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_buffer.data(), wide_len, nullptr, 0, nullptr, nullptr);
        if (utf8_len == 0)
            return false;

        // 验证转换后的 UTF-8 是否有效
        std::vector<char> utf8_buffer(utf8_len);
        WideCharToMultiByte(CP_UTF8, 0, wide_buffer.data(), wide_len, utf8_buffer.data(), utf8_len, nullptr, nullptr);

        // 简单验证：检查是否有合理的字符密度
        int printable = 0;
        for (size_t i = 0; i < utf8_buffer.size(); ++i)
        {
            unsigned char c = static_cast<unsigned char>(utf8_buffer[i]);
            if (c >= 0x20 || c == 0x09 || c == 0x0A || c == 0x0D)
                printable++;
        }

        // 如果可打印字符比例太低，可能转换失败
        double ratio = static_cast<double>(printable) / utf8_buffer.size();
        if (ratio < 0.5)
            return false;

        result = std::string(utf8_buffer.begin(), utf8_buffer.end());
        return true;
    }

    // 检测文件编码并转换为 UTF-8
    std::string read_file_with_encoding(const std::string &path)
    {
        LOG_DEBUG("[read_file_with_encoding] 尝试打开文件: " << path);
        std::ifstream file;
#ifdef _WIN32
        // On Windows, convert narrow string path to wide string to support non-ASCII characters
        int wide_len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
        if (wide_len > 0)
        {
            std::vector<wchar_t> wide_path(wide_len);
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), wide_path.data(), wide_len);
            file.open(wide_path.data(), std::ios::binary);
        }
        else
        {
            file.open(path, std::ios::binary);
        }
#else
        file.open(path, std::ios::binary);
#endif
        if (!file.is_open())
        {
            LOG_ERROR("[read_file_with_encoding] 无法打开文件: " << path);
            return "";
        }

        std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        LOG_DEBUG("[read_file_with_encoding] 文件大小 (字节): " << buffer.size());
        if (buffer.empty())
        {
            LOG_ERROR("[read_file_with_encoding] 文件内容为空");
            return "";
        }

        // 检测 UTF-8 BOM
        if (buffer.size() >= 3 &&
            static_cast<unsigned char>(buffer[0]) == 0xEF &&
            static_cast<unsigned char>(buffer[1]) == 0xBB &&
            static_cast<unsigned char>(buffer[2]) == 0xBF)
        {
            LOG_DEBUG("[read_file_with_encoding] 检测到 UTF-8 with BOM");
            // UTF-8 with BOM，跳过 BOM
            return std::string(buffer.begin() + 3, buffer.end());
        }

        // 尝试作为 UTF-8 处理（不带 BOM）- 严格的 UTF-8 有效性检查
        bool is_utf8 = true;
        size_t i = 0;
        while (i < buffer.size())
        {
            unsigned char c = static_cast<unsigned char>(buffer[i]);

            if (c < 0x80)
            {
                // ASCII 字符
                i++;
            }
            else if (c < 0xC0)
            {
                // 独立的高位字节，不是有效的 UTF-8 开始
                is_utf8 = false;
                break;
            }
            else if (c < 0xE0)
            {
                // 2 字节序列
                if (i + 1 >= buffer.size() ||
                    (static_cast<unsigned char>(buffer[i + 1]) & 0xC0) != 0x80)
                {
                    is_utf8 = false;
                    break;
                }
                i += 2;
            }
            else if (c < 0xF0)
            {
                // 3 字节序列
                if (i + 2 >= buffer.size() ||
                    (static_cast<unsigned char>(buffer[i + 1]) & 0xC0) != 0x80 ||
                    (static_cast<unsigned char>(buffer[i + 2]) & 0xC0) != 0x80)
                {
                    is_utf8 = false;
                    break;
                }
                // 验证 UTF-16 代签（surrogate pair in UTF-8）
                if (c == 0xED && static_cast<unsigned char>(buffer[i + 1]) > 0x9F)
                {
                    is_utf8 = false;
                    break;
                }
                i += 3;
            }
            else if (c < 0xF8)
            {
                // 4 字节序列
                if (i + 3 >= buffer.size() ||
                    (static_cast<unsigned char>(buffer[i + 1]) & 0xC0) != 0x80 ||
                    (static_cast<unsigned char>(buffer[i + 2]) & 0xC0) != 0x80 ||
                    (static_cast<unsigned char>(buffer[i + 3]) & 0xC0) != 0x80)
                {
                    is_utf8 = false;
                    break;
                }
                // 验证 Unicode 范围（不能超过 U+10FFFF）
                if (c == 0xF4 && static_cast<unsigned char>(buffer[i + 1]) > 0x8F)
                {
                    is_utf8 = false;
                    break;
                }
                i += 4;
            }
            else
            {
                // 无效的 UTF-8 开始字节
                is_utf8 = false;
                break;
            }
        }

        if (is_utf8)
        {
            LOG_DEBUG("[read_file_with_encoding] 检测到 UTF-8 (无 BOM)");
            return std::string(buffer.begin(), buffer.end());
        }

        // UTF-8 检测失败，使用字节频率分析来辅助检测编码
        EncodingInfo encoding = analyze_encoding(buffer);
        LOG_DEBUG("[read_file_with_encoding] 分析出的编码: " << encoding.name << ", codepage: " << encoding.codepage);

        // 按优先级尝试不同的编码转换
        // 编码优先级列表：GBK, GB2312, Big5, Shift-JIS, Windows-1252, ISO-8859-1, 系统默认
        const EncodingInfo encodings[] = {
            {"GBK", 936, 10},
            {"GB2312", 936, 15}, // GB2312 也使用代码页 936
            {"Big5", 950, 20},
            {"Shift-JIS", 932, 30},
            {"Windows-1252", 1252, 40},
            {"ISO-8859-1", 28591, 50}, // ISO-8859-1
            {"System", GetACP(), 100}};

        // 如果分析结果优先级更高，优先尝试
        std::string result;
        if (encoding.priority < 100)
        {
            if (try_convert_to_utf8(buffer, encoding.codepage, result))
            {
                LOG_DEBUG("[read_file_with_encoding] 使用分析出的编码转换成功: " << encoding.name);
                return result;
            }
            else
            {
                LOG_DEBUG("[read_file_with_encoding] 使用分析出的编码转换失败: " << encoding.name);
            }
        }

        // 按优先级顺序尝试其他编码
        for (const auto &enc : encodings)
        {
            // 跳过已经尝试过的编码
            if (encoding.priority < 100 && enc.codepage == encoding.codepage)
                continue;

            if (try_convert_to_utf8(buffer, enc.codepage, result))
            {
                LOG_DEBUG("[read_file_with_encoding] 使用编码转换成功: " << enc.name);
                return result;
            }
            else
            {
                LOG_DEBUG("[read_file_with_encoding] 使用编码转换失败: " << enc.name);
            }
        }

        // 所有编码都失败，返回原始数据
        LOG_DEBUG("[read_file_with_encoding] 所有编码转换失败，返回原始数据");
        return std::string(buffer.begin(), buffer.end());
    }

    const std::map<std::string, int> &get_vk_map()
    {
        static std::map<std::string, int> map = {
            {"q", 0x51}, {"w", 0x57}, {"e", 0x45}, {"r", 0x52}, {"t", 0x54}, {"y", 0x59}, {"u", 0x55}, {"i", 0x49}, {"o", 0x4F}, {"p", 0x50}, {"a", 0x41}, {"s", 0x53}, {"d", 0x44}, {"f", 0x46}, {"g", 0x47}, {"h", 0x48}, {"j", 0x4A}, {"k", 0x4B}, {"l", 0x4C}, {"z", 0x5A}, {"x", 0x58}, {"c", 0x43}, {"v", 0x56}, {"b", 0x42}, {"n", 0x4E}, {"m", 0x4D}, {"1", 0x31}, {"2", 0x32}, {"3", 0x33}, {"4", 0x34}, {"5", 0x35}, {"6", 0x36}, {"7", 0x37}, {"8", 0x38}, {"9", 0x39}, {"0", 0x30}, {"[", 0xDB}, {"]", 0xDD}, {"\\", 0xDC}, {"'", 0xDE}, {"-", 0xBD}, {"=", 0xBB}, {"+", 0xBB}, {"/", 0xBF}, {",", 0xBC}, {".", 0xBE}, {";", 0xBA}, {"`", 0xC0}};
        return map;
    }

    const std::map<int, std::string> &get_vk_reverse_map()
    {
        static std::map<int, std::string> map = []
        {
            std::map<int, std::string> r;
            for (const auto &pair : get_vk_map())
            {
                r[pair.second] = pair.first;
            }
            return r;
        }();
        return map;
    }
}

namespace Util
{

    KeyManager::KeyManager()
    {
        LOG_DEBUG("[KeyManager] 初始化，加载默认键位映射");
        init_default_map();
    }

    KeyMapping KeyManager::get_mapping(int note)
    {
        // 优化：使用 O(1) 缓存数组查找，避免 std::map 的 O(log n) 开销
        if (note >= 0 && note < 128 && m_lookup_valid[note])
        {
            return m_lookup_cache[note];
        }
        return {0, 0};
    }

    bool KeyManager::load_config(const std::string &path)
    {
        LOG_DEBUG("[KeyManager] 加载键位配置: " << path);
        LOG_DEBUG("[KeyManager] 检查文件是否存在: " << path);

        std::string content = read_file_with_encoding(path);
        LOG_DEBUG("[KeyManager] read_file_with_encoding returned content length: " << content.size());
        if (content.empty())
        {
            LOG_WARN("无法读取键位配置文件或文件为空: " << path);
            return false;
        }

        std::map<int, KeyMapping> new_map;

        // Only support TXT format
        std::istringstream lines(content);
        std::string line;
        int line_count = 0;
        int valid_count = 0;
        // 优化：静态缓存正则表达式，避免每次调用重新编译
        static const std::regex re(R"((?:音符\s+)?([A-G][#B]?\d+|\d+)(?:\s*\(.*?\))?[\s]*[:=\-\s]+[\s]*([^\s]+))", std::regex::icase | std::regex::optimize);
        while (std::getline(lines, line))
        {
            line_count++;
            line = trim_copy(line);
            if (line.empty())
                continue;
            std::string lower = to_lower_copy(line);
            if (!lower.empty() && (lower[0] == '#' || lower[0] == '-'))
                continue;

            line = normalize_line(line);
            std::smatch match;
            if (std::regex_search(line, match, re))
            {
                std::string key_part = match[1].str();
                std::string value_part = match[2].str();

                int pitch = -1;
                if (is_digits(key_part))
                {
                    pitch = std::stoi(key_part);
                }
                else
                {
                    if (!get_pitch_from_name(key_part, pitch))
                    {
                        LOG_WARN("无法解析音名: " << key_part << " (行 " << line_count << ")");
                        continue;
                    }
                }

                int vk = 0;
                int modifier = 0;
                if (parse_key_string(value_part, vk, modifier))
                {
                    new_map[pitch] = {vk, modifier};
                    valid_count++;
                }
                else
                {
                    LOG_WARN("无法解析按键: " << value_part << " (行 " << line_count << ")");
                }
            }
        }

        if (!new_map.empty())
        {
            m_note_map = new_map;
            rebuild_lookup_cache();
            LOG_INFO("键位配置加载成功: " << valid_count << " 个映射 (共 " << line_count << " 行)");
            return true;
        }

        LOG_WARN("键位配置文件无有效映射: " << path);
        return false;
    }

    bool KeyManager::save_config(const std::string &path) const
    {
        LOG_DEBUG("[KeyManager] 保存键位配置: " << path);

        std::vector<int> keys;
        for (const auto &pair : m_note_map)
        {
            keys.push_back(pair.first);
        }
        std::sort(keys.begin(), keys.end());

        std::vector<std::pair<int, std::string>> entries;
        entries.reserve(keys.size());
        for (int k : keys)
        {
            auto it = m_note_map.find(k);
            if (it == m_note_map.end())
                continue;
            std::string key_str = format_key_string(it->second.vk_code, it->second.modifier);
            if (key_str.empty())
                continue;
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

        for (const auto &entry : entries)
        {
            std::string name = get_note_name(entry.first);
            out << " 音符 " << entry.first << " (" << name << "): " << entry.second << "\n";
        }

        std::string content = out.str();
        std::ofstream file;
#ifdef _WIN32
        // On Windows, convert narrow string path to wide string to support non-ASCII characters
        int wide_len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
        if (wide_len > 0)
        {
            std::vector<wchar_t> wide_path(wide_len);
            MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), wide_path.data(), wide_len);
            file.open(wide_path.data(), std::ios::binary);
        }
        else
        {
            file.open(path, std::ios::binary);
        }
#else
        file.open(path, std::ios::binary);
#endif
        if (!file.is_open())
        {
            LOG_ERROR("无法创建键位配置文件: " << path);
            return false;
        }

        // 写入 UTF-8 BOM
        unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        file.write(reinterpret_cast<const char *>(bom), sizeof(bom));

        // 写入内容
        file << content;

        LOG_INFO("键位配置保存成功: " << entries.size() << " 个映射 -> " << path);
        return true;
    }

    void KeyManager::set_map(const std::map<int, KeyMapping> &map)
    {
        m_note_map = map;
        rebuild_lookup_cache();
    }

    const std::map<int, KeyMapping> &KeyManager::get_map() const
    {
        return m_note_map;
    }

    void KeyManager::reset_to_default()
    {
        LOG_DEBUG("[KeyManager] 重置为默认键位映射");
        init_default_map();
    }

    void KeyManager::init_default_map()
    {
        m_note_map.clear();

        LOG_DEBUG("加载 FF14 默认键位映射");

        // Default mapping from original python source (key_manager.py)
        // Note: Default mapping does not use modifiers (Shift=1, Ctrl=2)

        // High range / Special chars
        m_note_map[48] = {'I', 0};      // i
        m_note_map[50] = {'O', 0};      // o
        m_note_map[52] = {'P', 0};      // p
        m_note_map[53] = {VK_OEM_4, 0}; // [
        m_note_map[55] = {VK_OEM_6, 0}; // ]
        m_note_map[57] = {VK_OEM_5, 0}; // \ (backslash)
        m_note_map[59] = {VK_OEM_7, 0}; // ' (quote)

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
        m_note_map[84] = {VK_OEM_2, 0}; // /
        rebuild_lookup_cache();

        LOG_DEBUG("默认键位映射已加载: " << m_note_map.size() << " 个映射");
    }

    void KeyManager::rebuild_lookup_cache()
    {
        std::memset(m_lookup_valid, 0, sizeof(m_lookup_valid));
        for (const auto &pair : m_note_map)
        {
            if (pair.first >= 0 && pair.first < 128)
            {
                m_lookup_cache[pair.first] = pair.second;
                m_lookup_valid[pair.first] = true;
            }
        }
    }

    std::string KeyManager::format_key_string(int vk, int modifier) const
    {
        const auto &rev = get_vk_reverse_map();
        auto it = rev.find(vk);
        if (it == rev.end())
            return "";
        std::string key = it->second;
        if (modifier == 1)
            key += "+";
        else if (modifier == 2)
            key += "-";
        return key;
    }

    bool KeyManager::parse_key_string(const std::string &key_str, int &vk, int &modifier) const
    {
        std::string s = normalize_line(to_lower_copy(trim_copy(key_str)));
        if (s.empty())
            return false;

        modifier = 0;
        if (s.size() > 1)
        {
            char last = s.back();
            if (last == '+')
            {
                modifier = 1;
                s.pop_back();
            }
            else if (last == '-')
            {
                modifier = 2;
                s.pop_back();
            }
        }

        s = trim_copy(s);
        const auto &map = get_vk_map();
        auto it = map.find(s);
        if (it == map.end())
            return false;
        vk = it->second;
        return true;
    }

    std::string KeyManager::get_note_name(int midi_pitch) const
    {
        static const std::string notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        if (midi_pitch < 0 || midi_pitch > 127)
            return "";
        int octave = (midi_pitch / 12) - 1;
        int idx = midi_pitch % 12;
        return notes[idx] + std::to_string(octave);
    }

    bool KeyManager::get_pitch_from_name(const std::string &name, int &pitch) const
    {
        std::regex re(R"(^\s*([A-Ga-g])([#bB]?)(-?\d+)\s*$)");
        std::smatch match;
        if (!std::regex_match(name, match, re))
            return false;

        std::string note = match[1].str();
        std::string acc = match[2].str();
        std::string octave_str = match[3].str();

        std::string key = to_lower_copy(note);
        if (!acc.empty())
        {
            char c = acc[0];
            if (c == '#' || c == 'b' || c == 'B')
            {
                key += (c == '#') ? "#" : "b";
            }
        }

        static const std::map<std::string, int> notes_map = {
            {"c", 0}, {"c#", 1}, {"db", 1}, {"d", 2}, {"d#", 3}, {"eb", 3}, {"e", 4}, {"f", 5}, {"f#", 6}, {"gb", 6}, {"g", 7}, {"g#", 8}, {"ab", 8}, {"a", 9}, {"a#", 10}, {"bb", 10}, {"b", 11}};

        auto it = notes_map.find(key);
        if (it == notes_map.end())
            return false;
        int octave = std::stoi(octave_str);
        int value = (octave + 1) * 12 + it->second;
        if (value < 0 || value > 127)
            return false;
        pitch = value;
        return true;
    }

}
