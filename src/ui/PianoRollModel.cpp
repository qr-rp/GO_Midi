#include "PianoRollModel.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace UI {

namespace {

    constexpr int kBlackPitchClasses[] = {1, 3, 6, 8, 10};

    bool is_black_pitch_class(int pitch_class)
    {
        return std::ranges::find(kBlackPitchClasses, pitch_class) !=
               std::end(kBlackPitchClasses);
    }

    void collect_conflicts(const std::map<int, std::vector<int>>& groups,
                           std::set<int>& conflicts)
    {
        for (const auto& [identity, notes] : groups) {
            (void)identity;
            if (notes.size() <= 1)
                continue;
            conflicts.insert(notes.begin(), notes.end());
        }
    }

    void collect_conflicts(
        const std::map<std::pair<int, int>, std::vector<int>>& groups,
        std::set<int>& conflicts)
    {
        for (const auto& [identity, notes] : groups) {
            (void)identity;
            if (notes.size() <= 1)
                continue;
            conflicts.insert(notes.begin(), notes.end());
        }
    }

} // namespace

void PianoRollEditHistory::reset()
{
    m_undo.clear();
    m_redo.clear();
}

void PianoRollEditHistory::record_change(
    int note,
    std::optional<Util::KeyMapping> before,
    std::optional<Util::KeyMapping> after)
{
    push_limited(m_undo, {note, std::move(before), std::move(after)});
    m_redo.clear();
}

bool PianoRollEditHistory::undo(std::map<int, Util::KeyMapping>& current)
{
    return restore(true, current);
}

bool PianoRollEditHistory::redo(std::map<int, Util::KeyMapping>& current)
{
    return restore(false, current);
}

void PianoRollEditHistory::push_limited(
    std::vector<Change>& stack, const Change& change)
{
    if (stack.size() >= kMaxSteps)
        stack.erase(stack.begin());
    stack.push_back(change);
}

bool PianoRollEditHistory::restore(
    bool undo, std::map<int, Util::KeyMapping>& current)
{
    auto& source = undo ? m_undo : m_redo;
    auto& target = undo ? m_redo : m_undo;
    if (source.empty())
        return false;

    Change change = std::move(source.back());
    source.pop_back();
    if (undo)
    {
        if (change.before)
            current[change.note] = *change.before;
        else
            current.erase(change.note);
    }
    else
    {
        if (change.after)
            current[change.note] = *change.after;
        else
            current.erase(change.note);
    }
    push_limited(target, change);
    return true;
}

PianoRollLayout::PianoRollLayout(const PianoRollLayoutConfig& config)
{
    configure(config);
}

void PianoRollLayout::configure(const PianoRollLayoutConfig& config)
{
    m_display_min = std::clamp(config.display_min, 0, kMidiNoteCount - 1);
    m_display_max = std::clamp(config.display_max, m_display_min, kMidiNoteCount - 1);
    m_white_width = std::max(1, config.white_width);
    m_black_width = std::clamp(config.black_width, 1, m_white_width);
    m_white_height = std::max(1, config.white_height);
    m_black_height = std::clamp(config.black_height, 1, m_white_height);
    rebuild();
}

bool PianoRollLayout::contains(int note) const
{
    return note >= m_display_min && note <= m_display_max;
}

bool PianoRollLayout::is_black_key(int note) const
{
    const int pitch_class = (note % 12 + 12) % 12;
    return is_black_pitch_class(pitch_class);
}

PianoKeyGeometry PianoRollLayout::key_geometry(int note) const
{
    if (!contains(note))
        return {};
    return m_keys[static_cast<std::size_t>(note)];
}

int PianoRollLayout::hit_test(int content_x, int y) const
{
    if (content_x < 0 || content_x >= m_content_width || y < 0 || y >= m_white_height)
        return -1;

    // 黑键位于白键上方, 必须先命中。
    for (int note = m_display_min; note <= m_display_max; ++note) {
        if (!m_keys[static_cast<std::size_t>(note)].black)
            continue;
        const auto& key = m_keys[static_cast<std::size_t>(note)];
        if (content_x >= key.x && content_x < key.x + key.width &&
            y >= key.y && y < key.y + key.height) {
            return note;
        }
    }
    for (int note = m_display_min; note <= m_display_max; ++note) {
        const auto& key = m_keys[static_cast<std::size_t>(note)];
        if (key.black)
            continue;
        if (content_x >= key.x && content_x < key.x + key.width &&
            y >= key.y && y < key.y + key.height) {
            return note;
        }
    }
    return -1;
}

void PianoRollLayout::rebuild()
{
    std::array<int, kMidiNoteCount> absolute_x{};
    std::array<int, kMidiNoteCount> widths{};
    std::array<bool, kMidiNoteCount> black{};
    int white_cursor = 0;

    for (int note = 0; note < kMidiNoteCount; ++note) {
        const bool is_black = is_black_key(note);
        black[static_cast<std::size_t>(note)] = is_black;
        if (is_black) {
            absolute_x[static_cast<std::size_t>(note)] =
                white_cursor - m_black_width / 2;
            widths[static_cast<std::size_t>(note)] = m_black_width;
        } else {
            absolute_x[static_cast<std::size_t>(note)] = white_cursor;
            widths[static_cast<std::size_t>(note)] = m_white_width;
            white_cursor += m_white_width;
        }
    }

    int min_x = absolute_x[static_cast<std::size_t>(m_display_min)];
    int max_right = min_x;
    for (int note = m_display_min; note <= m_display_max; ++note) {
        const auto index = static_cast<std::size_t>(note);
        min_x = std::min(min_x, absolute_x[index]);
        max_right = std::max(max_right, absolute_x[index] + widths[index]);
    }

    m_content_width = std::max(0, max_right - min_x);
    for (int note = 0; note < kMidiNoteCount; ++note) {
        const auto index = static_cast<std::size_t>(note);
        m_keys[index] = {
            absolute_x[index] - min_x,
            0,
            widths[index],
            black[index] ? m_black_height : m_white_height,
            black[index]
        };
    }
}

PianoRollBindingAnalysis analyze_key_bindings(
    const std::map<int, Util::KeyMapping>& map,
    int min_pitch, int max_pitch)
{
    PianoRollBindingAnalysis result;
    if (min_pitch > max_pitch)
        std::swap(min_pitch, max_pitch);

    std::map<std::pair<int, int>, std::vector<int>> logical_groups;
    std::map<int, std::vector<int>> physical_groups;

    for (const auto& [note, mapping] : map) {
        if (mapping.vk_code <= 0)
            continue;
        ++result.bound_notes;
        if (note < min_pitch || note > max_pitch)
            result.out_of_range.insert(note);
        logical_groups[{mapping.vk_code, mapping.modifier}].push_back(note);
        physical_groups[mapping.vk_code].push_back(note);
    }

    for (int note = min_pitch; note <= max_pitch; ++note) {
        const auto it = map.find(note);
        if (it == map.end() || it->second.vk_code <= 0)
            ++result.unbound_in_range;
    }

    collect_conflicts(logical_groups, result.logical_duplicates);
    collect_conflicts(physical_groups, result.physical_conflicts);
    return result;
}

std::optional<int> find_next_navigation_note(
    int anchor, int direction,
    int display_min, int display_max,
    const PianoPitchRange& editable_range)
{
    if (direction == 0 || display_min > display_max)
        return std::nullopt;

    const int step = direction > 0 ? 1 : -1;
    int note = anchor + step;
    if (anchor < 0) {
        if (direction > 0)
            note = display_min;
        else
            note = display_max;
    }
    while (note >= display_min && note <= display_max) {
        if (editable_range.contains(note))
            return note;
        note += step;
    }
    return std::nullopt;
}

} // namespace UI
