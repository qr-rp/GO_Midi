#pragma once

#include <array>
#include <map>
#include <set>

#include "../util/KeyManager.h"

namespace UI {

struct PianoKeyGeometry {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool black = false;
};

struct PianoRollBindingAnalysis {
    int bound_notes = 0;
    int unbound_in_range = 0;
    std::set<int> logical_duplicates;
    std::set<int> physical_conflicts;
    std::set<int> out_of_range;
};

struct PianoRollLayoutConfig {
    int display_min = 0;
    int display_max = -1;
    int white_width = 1;
    int black_width = 1;
    int white_height = 1;
    int black_height = 1;
};

/// 与 wxWidgets 解耦的钢琴卷几何模型。
///
/// 黑键位置使用固定 MIDI 原点计算, 不再依赖当前显示范围。这样即使音域从
/// C# 等黑键开始, 黑键也不会与相邻黑键重叠; 所有绘制和命中测试都可直接查表。
class PianoRollLayout {
public:
    PianoRollLayout() = default;
    explicit PianoRollLayout(const PianoRollLayoutConfig& config);

    void configure(const PianoRollLayoutConfig& config);

    [[nodiscard]] int content_width() const { return m_content_width; }

    [[nodiscard]] bool contains(int note) const;
    [[nodiscard]] bool is_black_key(int note) const;
    [[nodiscard]] PianoKeyGeometry key_geometry(int note) const;
    [[nodiscard]] int hit_test(int content_x, int y) const;

private:
    static constexpr int kMidiNoteCount = 128;

    void rebuild();

    int m_display_min = 0;
    int m_display_max = -1;
    int m_white_width = 1;
    int m_black_width = 1;
    int m_white_height = 1;
    int m_black_height = 1;
    int m_content_width = 0;
    std::array<PianoKeyGeometry, kMidiNoteCount> m_keys{};
};

/// 同时统计智能移调使用的两种键位身份:
/// - 逻辑重复: 相同 vk_code + modifier;
/// - 物理冲突: 相同 vk_code, 即使 modifier 不同。
///
/// 键位编辑器始终显示两类冲突, 避免与实际播放/移调模型产生认知偏差。
[[nodiscard]] PianoRollBindingAnalysis analyze_key_bindings(
    const std::map<int, Util::KeyMapping>& map,
    int min_pitch, int max_pitch);

} // namespace UI
