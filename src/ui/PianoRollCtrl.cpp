#include "PianoRollCtrl.h"

#include <algorithm>
#include <utility>

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>

#include <windows.h>  // VK_* 常量
#include <imm.h>      // ImmAssociateContext: 键位捕获时禁用输入法

namespace UI {

namespace {

    wxString format_modifiers(int mod, bool full_names)
    {
        static constexpr struct {
            int bit;
            const wchar_t* code;
            const wchar_t* name;
        } kModifiers[] = {
            {Util::kModCtrl, L"C", L"Ctrl+"},
            {Util::kModShift, L"S", L"Shift+"},
            {Util::kModAlt, L"A", L"Alt+"},
            {Util::kModMouseL, L"L", L"鼠标左键+"},
            {Util::kModMouseM, L"M", L"鼠标中键+"},
            {Util::kModMouseR, L"R", L"鼠标右键+"}
        };

        wxString result;
        for (const auto& modifier : kModifiers) {
            if (mod & modifier.bit)
                result += full_names ? modifier.name : modifier.code;
        }
        return result;
    }

    wxString vk_short_name(int vk)
    {
        if (vk >= 'A' && vk <= 'Z') return wxString(static_cast<wxChar>(vk));
        if (vk >= '0' && vk <= '9') return wxString(static_cast<wxChar>(vk));

        switch (vk) {
            case VK_SPACE: return L"Spc";
            case VK_RETURN: return L"Ent";
            case VK_TAB: return L"Tab";
            case VK_BACK: return L"Bk";
            case VK_ESCAPE: return L"Esc";
            case VK_DELETE: return L"Del";
            case VK_INSERT: return L"Ins";
            case VK_HOME: return L"Hm";
            case VK_END: return L"End";
            case VK_PRIOR: return L"PU";
            case VK_NEXT: return L"PD";
            case VK_LEFT: return L"←";
            case VK_RIGHT: return L"→";
            case VK_UP: return L"↑";
            case VK_DOWN: return L"↓";
            case VK_SNAPSHOT: return L"Prt";
            case VK_SCROLL: return L"Scr";
            case VK_PAUSE: return L"Pse";
            case VK_CAPITAL: return L"Cps";
            case VK_NUMLOCK: return L"Num";
            case VK_LWIN: case VK_RWIN: return L"Win";
            case VK_APPS: return L"Menu";
            case VK_OEM_1: return L";";
            case VK_OEM_PLUS: return L"+";
            case VK_OEM_COMMA: return L",";
            case VK_OEM_MINUS: return L"-";
            case VK_OEM_PERIOD: return L".";
            case VK_OEM_2: return L"/";
            case VK_OEM_3: return L"`";
            case VK_OEM_4: return L"[";
            case VK_OEM_5: return L"\\";
            case VK_OEM_6: return L"]";
            case VK_OEM_7: return L"'";
            case VK_ADD: return L"N+";
            case VK_SUBTRACT: return L"N-";
            case VK_MULTIPLY: return L"N*";
            case VK_DIVIDE: return L"N/";
            case VK_DECIMAL: return L"N.";
            default:
                if (vk >= VK_F1 && vk <= VK_F24)
                    return wxString::Format(L"F%d", vk - VK_F1 + 1);
                if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
                    return wxString::Format(L"N%d", vk - VK_NUMPAD0);
                return wxString::Format(L"#%d", vk);
        }
    }

    wxString fit_label(wxDC& dc, const wxString& text, int max_width)
    {
        if (text.IsEmpty() || max_width <= 0)
            return wxString();
        if (dc.GetTextExtent(text).x <= max_width)
            return text;

        const wxString ellipsis = L"…";
        wxString fitted = text;
        while (!fitted.IsEmpty() &&
               dc.GetTextExtent(fitted + ellipsis).x > max_width) {
            fitted.RemoveLast();
        }
        if (fitted.IsEmpty() || dc.GetTextExtent(ellipsis).x > max_width)
            return wxString();
        return fitted + ellipsis;
    }

    struct KeyColours {
        wxColour border;
        wxColour fill;
        wxColour note_text;
        wxColour key_text;
    };

    struct KeyPaintState {
        bool selected;
        bool black;
        bool in_range;
        bool logical;
        bool physical;
    };

    struct KeyLabels {
        wxString note;
        wxString binding;
        wxColour note_colour;
        wxColour key_colour;
    };

    int fill_state(bool selected, bool in_range)
    {
        if (selected) return 2;
        if (!in_range) return 1;
        return 0;
    }

    int key_text_state(bool in_range, bool logical, bool physical)
    {
        if (!in_range) return 0;
        if (physical) return 1;
        if (logical) return 2;
        return 3;
    }

    KeyColours key_colours(const KeyPaintState& state)
    {
        static const wxColour kWhiteBorder(0x3a, 0x42, 0x52);
        static const wxColour kBlackBorder(0x55, 0x5c, 0x6a);
        static const wxColour kFill[3][2] = {
            {wxColour(0xf4, 0xf4, 0xf4), wxColour(0x1a, 0x1a, 0x1e)},
            {wxColour(0x2e, 0x34, 0x3e), wxColour(0x14, 0x17, 0x1c)},
            {wxColour(0xcf, 0xe0, 0xff), wxColour(0x3a, 0x55, 0x8a)}
        };
        static const wxColour kNoteText[2][2] = {
            {wxColour(0x5c, 0x64, 0x72), wxColour(0x30, 0x36, 0x42)},
            {wxColour(0x6a, 0x72, 0x80), wxColour(0xcc, 0xd2, 0xdc)}
        };
        static const wxColour kKeyText[4][2] = {
            {wxColour(0x8a, 0x4a, 0x4a), wxColour(0xa0, 0x5a, 0x5a)},
            {wxColour(0xe0, 0x6a, 0x6a), wxColour(0xe0, 0x6a, 0x6a)},
            {wxColour(0xd6, 0xa5, 0x45), wxColour(0xd6, 0xa5, 0x45)},
            {wxColour(0xd0, 0x5a, 0x5a), wxColour(0xff, 0x9a, 0x9a)}
        };

        const int colour = state.black ? 1 : 0;
        wxColour border;
        if (state.selected)
            border = wxColour(0x4f, 0x8c, 0xff);
        else if (state.black)
            border = kBlackBorder;
        else
            border = kWhiteBorder;
        return {
            border,
            kFill[fill_state(state.selected, state.in_range)][colour],
            kNoteText[colour][state.in_range ? 1 : 0],
            kKeyText[key_text_state(
                state.in_range, state.logical, state.physical)][colour]
        };
    }

    void paint_key_labels(wxDC& dc, const wxRect& rect, bool black,
                          const KeyLabels& labels)
    {
        const int label_width = std::max(0, rect.width - 4);
        dc.SetTextForeground(labels.note_colour);
        dc.DrawText(fit_label(dc, labels.note, label_width), rect.x + 2, rect.y + 3);
        if (labels.binding.IsEmpty())
            return;

        dc.SetTextForeground(labels.key_colour);
        const int key_y = rect.y + rect.height - (black ? 13 : 14);
        dc.DrawText(fit_label(dc, labels.binding, label_width), rect.x + 2, key_y);
    }

    int required_control_height(int content_width, int view_width,
                                int keyboard_height, int scroll_height)
    {
        return content_width > view_width
            ? keyboard_height + scroll_height + 2
            : keyboard_height;
    }

} // namespace

wxBEGIN_EVENT_TABLE(PianoRollCtrl, wxWindow)
    EVT_PAINT(PianoRollCtrl::OnPaint)
    EVT_LEFT_DOWN(PianoRollCtrl::OnMouse)
    EVT_LEFT_UP(PianoRollCtrl::OnMouse)
    EVT_MIDDLE_DOWN(PianoRollCtrl::OnMouse)
    EVT_MIDDLE_UP(PianoRollCtrl::OnMouse)
    EVT_RIGHT_DOWN(PianoRollCtrl::OnMouse)
    EVT_RIGHT_UP(PianoRollCtrl::OnMouse)
    EVT_SIZE(PianoRollCtrl::OnSize)
    EVT_MOTION(PianoRollCtrl::OnMouseMove)
    EVT_MOUSEWHEEL(PianoRollCtrl::OnMouseWheel)
    EVT_KEY_DOWN(PianoRollCtrl::OnKeyDown)
    EVT_CHAR_HOOK(PianoRollCtrl::OnCharHook)
wxEND_EVENT_TABLE()

PianoRollCtrl::PianoRollCtrl(wxWindow* parent, int minPitch, int maxPitch)
    : wxWindow(parent, wxID_ANY),
      m_pitch_range{minPitch, maxPitch},
      m_layout_config{
          minPitch, maxPitch,
          FromDIP(30), FromDIP(20), FromDIP(92), FromDIP(56)
      },
      m_viewW(FromDIP(700)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // 键位捕获必须拿到物理按键: 输入法激活时数字/字母键会先变成 VK_PROCESSKEY,
    // wxKeyEvent::GetKeyCode() 返回 WXK_NONE(0), 导致所有键都被绑定成"键0".
    // 该控件只做按键采集, 不需要文本输入, 直接解除输入法上下文.
    ImmAssociateContext(static_cast<HWND>(GetHWND()), nullptr);
    m_lastHint = wxString::FromUTF8("点击琴键绑定 · 未选中时 ←/→ 浏览 · Ctrl+Z 撤销 · Esc 取消");
    RefreshLayout();
    SetMinSize(wxSize(FromDIP(240), m_layout_config.white_height));
    SetFocus();
}

void PianoRollCtrl::RefreshLayout() {
    m_layout.configure(m_layout_config);
    m_analysis = analyze_key_bindings(
        m_map, m_pitch_range.min_pitch, m_pitch_range.max_pitch);
}

void PianoRollCtrl::UpdateSize() {
    RefreshLayout();
    int actualWidth = GetClientSize().x;
    m_viewW = actualWidth > 1
        ? actualWidth
        : std::min(FromDIP(700), ContentWidth());
    if (m_viewW < 1) m_viewW = 1;
    ClampScroll();
    const int height = required_control_height(
        ContentWidth(), m_viewW, m_layout_config.white_height, FromDIP(6));
    SetMinSize(wxSize(FromDIP(240), height));
    InvalidateBestSize();
    Refresh();
}

void PianoRollCtrl::OnSize(wxSizeEvent& event) {
    m_viewW = std::max(1, event.GetSize().x);
    ClampScroll();
    const int height = required_control_height(
        ContentWidth(), m_viewW, m_layout_config.white_height, FromDIP(6));
    if (GetMinSize().y != height) {
        SetMinSize(wxSize(FromDIP(240), height));
        CallAfter([this] {
            if (GetParent())
                GetParent()->Layout();
        });
    }
    Refresh();
    event.Skip();
}

void PianoRollCtrl::ClampScroll() {
    int content = ContentWidth();
    int maxScroll = content - m_viewW;
    if (maxScroll < 0) maxScroll = 0;
    if (m_scrollX < 0) m_scrollX = 0;
    if (m_scrollX > maxScroll) m_scrollX = maxScroll;
}

wxRect PianoRollCtrl::ScrollBarRect() const {
    return wxRect(2, m_layout_config.white_height + 2,
                  m_viewW - 4, FromDIP(6));
}

wxRect PianoRollCtrl::ScrollThumbRect() const {
    wxRect bar = ScrollBarRect();
    int content = ContentWidth();
    int maxScroll = content - m_viewW;
    if (maxScroll <= 0) return wxRect(bar.x, bar.y, bar.width, bar.height);
    // 滑块宽按可视/内容比例
    int thumbW = bar.width * m_viewW / content;
    if (thumbW < 20) thumbW = 20;
    int range = bar.width - thumbW;
    int thumbX = bar.x + (maxScroll > 0 ? static_cast<int>(range * (double)m_scrollX / maxScroll) : 0);
    return wxRect(thumbX, bar.y, thumbW, bar.height);
}

void PianoRollCtrl::SetMap(const std::map<int, Util::KeyMapping>& map) {
    m_map = map;
    m_history.reset();
    m_mouseModifier = 0;
    m_selectedNote = -1;
    m_navigationAnchor = -1;
    RecalcDisplayRange();
    UpdateSize();
}

void PianoRollCtrl::SetPitchRange(int minPitch, int maxPitch) {
    m_pitch_range = {minPitch, maxPitch};
    if (m_pitch_range.min_pitch > m_pitch_range.max_pitch)
        std::swap(m_pitch_range.min_pitch, m_pitch_range.max_pitch);
    m_selectedNote = -1;
    m_navigationAnchor = -1;
    RecalcDisplayRange();
    UpdateSize();
}

void PianoRollCtrl::RecalcDisplayRange() {
    m_layout_config.display_min = m_pitch_range.min_pitch;
    m_layout_config.display_max = m_pitch_range.max_pitch;
    for (const auto& pair : m_map) {
        if (pair.first < m_layout_config.display_min)
            m_layout_config.display_min = pair.first;
        if (pair.first > m_layout_config.display_max)
            m_layout_config.display_max = pair.first;
    }
}

int PianoRollCtrl::HitTest(const wxPoint& pos) const {
    return m_layout.hit_test(pos.x + m_scrollX, pos.y);
}

wxString note_label(int note) {
    static const wxChar* kTones[] = { L"1", L"♯1", L"2", L"♯2", L"3",
                                      L"4", L"♯4", L"5", L"♯5", L"6", L"♯6", L"7" };
    int pc = (note % 12 + 12) % 12;
    return wxString(kTones[pc]);
}

wxString vk_base_name(int vk) {
    static constexpr std::pair<int, const wchar_t*> kNamedKeys[] = {
        {VK_SPACE, L"Space"}, {VK_RETURN, L"Enter"}, {VK_TAB, L"Tab"},
        {VK_BACK, L"Backspace"}, {VK_ESCAPE, L"Esc"}, {VK_DELETE, L"Del"},
        {VK_INSERT, L"Ins"}, {VK_HOME, L"Home"}, {VK_END, L"End"},
        {VK_PRIOR, L"PgUp"}, {VK_NEXT, L"PgDn"}, {VK_LEFT, L"←"},
        {VK_RIGHT, L"→"}, {VK_UP, L"↑"}, {VK_DOWN, L"↓"},
        {VK_SNAPSHOT, L"PrtSc"}, {VK_SCROLL, L"ScrLk"}, {VK_PAUSE, L"Pause"},
        {VK_CAPITAL, L"CapsLk"}, {VK_NUMLOCK, L"NumLk"}, {VK_LWIN, L"Win"},
        {VK_RWIN, L"Win"}, {VK_APPS, L"Menu"}, {VK_OEM_1, L";"},
        {VK_OEM_PLUS, L"+"}, {VK_OEM_COMMA, L","}, {VK_OEM_MINUS, L"-"},
        {VK_OEM_PERIOD, L"."}, {VK_OEM_2, L"/"}, {VK_OEM_3, L"`"},
        {VK_OEM_4, L"["}, {VK_OEM_5, L"\\"}, {VK_OEM_6, L"]"},
        {VK_OEM_7, L"'"}, {VK_ADD, L"小+"}, {VK_SUBTRACT, L"小-"},
        {VK_MULTIPLY, L"小*"}, {VK_DIVIDE, L"小/"}, {VK_DECIMAL, L"小."}
    };

    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return wxString(static_cast<wxChar>(vk));
    }
    for (const auto& [code, label] : kNamedKeys) {
        if (vk == code)
            return wxString(label);
    }
    if (vk >= VK_F1 && vk <= VK_F24)
        return wxString::Format(L"F%d", vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return wxString::Format(L"小%d", vk - VK_NUMPAD0);
    return wxString::Format(L"键%d", vk);
}

wxString vk_name(int vk, int mod) {
    return format_modifiers(mod, true) + vk_base_name(vk);
}

int vk_from_wx(int kc) {
    if (kc >= 'A' && kc <= 'Z') return kc;
    if (kc >= '0' && kc <= '9') return kc;
    // ASCII 标点(wxMSW GetKeyCode 对标点返回 ASCII 码)→ Windows VK
    switch (kc) {
        case ';': return VK_OEM_1;
        case '=': case '+': return VK_OEM_PLUS;
        case ',': return VK_OEM_COMMA;
        case '-': case '_': return VK_OEM_MINUS;
        case '.': return VK_OEM_PERIOD;
        case '/': case '?': return VK_OEM_2;
        case '`': case '~': return VK_OEM_3;
        case '[': case '{': return VK_OEM_4;
        case '\\': case '|': return VK_OEM_5;
        case ']': case '}': return VK_OEM_6;
        case '\'': case '"': return VK_OEM_7;
    }
    switch (kc) {
        case WXK_SPACE: return VK_SPACE;
        case WXK_RETURN: case WXK_NUMPAD_ENTER: return VK_RETURN;
        case WXK_TAB: return VK_TAB;
        case WXK_BACK: return VK_BACK;
        case WXK_ESCAPE: return VK_ESCAPE;
        case WXK_DELETE: case WXK_NUMPAD_DELETE: return VK_DELETE;
        case WXK_INSERT: return VK_INSERT;
        case WXK_HOME: return VK_HOME;
        case WXK_END: return VK_END;
        case WXK_PAGEUP: return VK_PRIOR;
        case WXK_PAGEDOWN: return VK_NEXT;
        case WXK_LEFT: return VK_LEFT;
        case WXK_RIGHT: return VK_RIGHT;
        case WXK_UP: return VK_UP;
        case WXK_DOWN: return VK_DOWN;
        case WXK_NUMPAD0: case WXK_NUMPAD1: case WXK_NUMPAD2: case WXK_NUMPAD3:
        case WXK_NUMPAD4: case WXK_NUMPAD5: case WXK_NUMPAD6: case WXK_NUMPAD7:
        case WXK_NUMPAD8: case WXK_NUMPAD9:
            return VK_NUMPAD0 + (kc - WXK_NUMPAD0);
        default:
            if (kc >= WXK_F1 && kc <= WXK_F24) return VK_F1 + (kc - WXK_F1);
            return kc;
    }
}

wxString PianoRollCtrl::CompactKeyLabel(int note) const {
    const auto it = m_map.find(note);
    if (it == m_map.end())
        return wxString();
    const wxString modifiers = format_modifiers(it->second.modifier, false);
    const wxString key = vk_short_name(it->second.vk_code);
    return modifiers.IsEmpty() ? key : modifiers + L"+" + key;
}

void PianoRollCtrl::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(0x10, 0x14, 0x1a)));
    dc.Clear();

    // MSVC 下变量名 small 撞 Windows SDK 宏(#define small char, windef.h 遗留),
    // wxFont small{...} 展开为 wxFont char{...} → C2628; MinGW 不定义该宏所以能过
    int smallSize = FromDIP(8);
    wxFont smallFont{smallSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL};
    dc.SetFont(smallFont);

    PaintKeys(dc, false);
    PaintKeys(dc, true);
    PaintScrollBar(dc);
}

void PianoRollCtrl::PaintKeys(wxDC& dc, bool paint_black) {
    for (int note = m_layout.display_min(); note <= m_layout.display_max(); ++note) {
        const auto key = m_layout.key_geometry(note);
        if (key.black != paint_black)
            continue;
        if (key.x - m_scrollX + key.width < 0 || key.x - m_scrollX > m_viewW)
            continue;
        PaintKey(dc, note);
    }
}

void PianoRollCtrl::PaintKey(wxDC& dc, int note) {
    const auto key = m_layout.key_geometry(note);
    const wxRect rect(key.x - m_scrollX, key.y, key.width, key.height);
    const bool in = InAudioRange(note);
    const bool selected = note == m_selectedNote;
    const bool logical = m_analysis.logical_duplicates.contains(note);
    const bool physical = m_analysis.physical_conflicts.contains(note);
    const auto colours = key_colours({
        selected, key.black, in, logical, physical
    });
    dc.SetPen(wxPen(colours.border, selected ? 2 : 1));
    dc.SetBrush(wxBrush(colours.fill));
    dc.DrawRectangle(rect);
    paint_key_labels(dc, rect, key.black, {
        note_label(note), CompactKeyLabel(note), colours.note_text, colours.key_text
    });
}

void PianoRollCtrl::PaintScrollBar(wxDC& dc) {
    if (ContentWidth() <= m_viewW)
        return;

    const wxRect bar = ScrollBarRect();
    dc.SetPen(wxPen(wxColour(0x2a, 0x30, 0x3a)));
    dc.SetBrush(wxBrush(wxColour(0x1c, 0x22, 0x2a)));
    dc.DrawRoundedRectangle(bar, 3);
    dc.SetBrush(wxBrush(wxColour(0x4a, 0x54, 0x64)));
    dc.DrawRoundedRectangle(ScrollThumbRect(), 3);
}

void PianoRollCtrl::OnMouse(wxMouseEvent& event) {
    const wxPoint pos = event.GetPosition();
    if (event.LeftDown()) {
        HandlePrimaryDown(pos);
    } else if (event.LeftUp()) {
        m_mouseModifier &= ~Util::kModMouseL;
        if (m_draggingScroll) {
            m_draggingScroll = false;
            if (HasCapture()) ReleaseMouse();
        }
    } else if (event.MiddleDown()) {
        SetMouseModifierFromButton(Util::kModMouseM, pos);
    } else if (event.MiddleUp()) {
        m_mouseModifier &= ~Util::kModMouseM;
    } else if (event.RightDown()) {
        HandleSecondaryDown(pos);
    } else if (event.RightUp()) {
        m_mouseModifier &= ~Util::kModMouseR;
    }
    event.Skip();
}

void PianoRollCtrl::BeginScrollInteraction(const wxPoint& pos) {
    const wxRect thumb = ScrollThumbRect();
    if (thumb.Contains(pos)) {
        m_draggingScroll = true;
        m_dragOffset = pos.x - thumb.x;
        CaptureMouse();
        return;
    }

    const int max_scroll = ContentWidth() - m_viewW;
    if (max_scroll <= 0)
        return;
    const wxRect bar = ScrollBarRect();
    const double ratio = static_cast<double>(pos.x - bar.x) / bar.width;
    m_scrollX = static_cast<int>(ratio * max_scroll);
    ClampScroll();
    Refresh();
    Update();
}

void PianoRollCtrl::HandleSecondaryDown(const wxPoint& pos) {
    if (pos.y >= m_layout_config.white_height)
        return;
    if (m_selectedNote >= 0) {
        SetMouseModifierFromButton(Util::kModMouseR, pos);
        return;
    }
    const int note = HitTest(pos);
    if (note < 0)
        return;
    if (m_map.find(note) == m_map.end()) {
        if (onStatus)
            onStatus(wxString::FromUTF8("该音符尚未绑定"));
        return;
    }

    m_history.record(m_map);
    m_map.erase(note);
    CommitMapChanged();
    if (onStatus)
        onStatus(wxString::FromUTF8("已清除该音符绑定, 可按 Ctrl+Z 撤销"));
}

void PianoRollCtrl::HandlePrimaryDown(const wxPoint& pos) {
    if (pos.y >= m_layout_config.white_height) {
        BeginScrollInteraction(pos);
        return;
    }

    const int note = HitTest(pos);
    if (note < 0)
        return;
    if (!InAudioRange(note)) {
        m_selectedNote = -1;
        m_navigationAnchor = note;
        if (onStatus)
            onStatus(wxString::FromUTF8("该音符在目标音域外, 请先扩展音域再绑定"));
        Refresh();
        Update();
        return;
    }
    if (m_selectedNote == note) {
        m_mouseModifier |= Util::kModMouseL;
        if (onStatus)
            onStatus(wxString::FromUTF8("鼠标左键已作为修饰键, 按主键完成绑定"));
        return;
    }
    SelectNote(note);
}

bool PianoRollCtrl::SetMouseModifierFromButton(int modifier, const wxPoint& pos) {
    if (m_selectedNote < 0 || HitTest(pos) < 0)
        return false;
    m_mouseModifier |= modifier;
    if (onStatus)
        onStatus(wxString::FromUTF8("鼠标键已作为修饰键, 按主键完成绑定"));
    return true;
}

void PianoRollCtrl::OnMouseMove(wxMouseEvent& event) {
    if (m_draggingScroll) {
        wxPoint pos = event.GetPosition();
        wxRect bar = ScrollBarRect();
        wxRect thumb = ScrollThumbRect();
        int content = ContentWidth();
        int maxScroll = content - m_viewW;
        if (maxScroll > 0) {
            int range = bar.width - thumb.width;
            double ratio = range > 0 ? (double)(pos.x - m_dragOffset - bar.x) / range : 0.0;
            m_scrollX = static_cast<int>(ratio * maxScroll);
            ClampScroll();
            Refresh();
            Update();
        }
        event.Skip();
        return;
    }

    UpdateHoverTooltip(event.GetPosition());
    event.Skip();
}

void PianoRollCtrl::UpdateHoverTooltip(const wxPoint& pos) {
    const int note = HitTest(pos);
    if (note == m_hoverNote)
        return;
    m_hoverNote = note;
    if (note < 0) {
        UnsetToolTip();
        return;
    }

    wxString tip = note_label(note);
    const auto it = m_map.find(note);
    if (it == m_map.end()) {
        SetToolTip(tip);
        return;
    }

    tip += wxString::FromUTF8(" · ") +
           vk_name(it->second.vk_code, it->second.modifier);
    if (m_analysis.physical_conflicts.contains(note))
        tip += wxString::FromUTF8(" · 物理主键冲突");
    else if (m_analysis.logical_duplicates.contains(note))
        tip += wxString::FromUTF8(" · 逻辑键位重复");
    SetToolTip(tip);
}

void PianoRollCtrl::OnMouseWheel(wxMouseEvent& event) {
    int maxScroll = ContentWidth() - m_viewW;
    if (maxScroll <= 0) { event.Skip(); return; }
    int delta = event.GetWheelRotation();
    int step = 40;
    m_scrollX += (delta > 0 ? -step : step);
    ClampScroll();
    Refresh();
    Update();
    // 已处理则不再冒泡给父级, 避免将来弹窗可纵向滚动时双重滚动
}

void PianoRollCtrl::SelectNote(int note) {
    if (!InAudioRange(note)) {
        if (onStatus)
            onStatus(wxString::FromUTF8("该音符在目标音域外, 请先扩展音域再绑定"));
        return;
    }

    m_selectedNote = note;
    m_navigationAnchor = note;
    SetFocus();
    m_lastHint = wxString::FromUTF8(
        "已选中音符, 按下新键绑定 · 鼠标键可作修饰 · Esc 取消");
    if (onStatus)
        onStatus(wxString::Format(wxString::FromUTF8("已选中 %s · %s"),
                                 note_label(note), m_lastHint));
    Refresh();
    Update();
}

void PianoRollCtrl::MoveSelection(int direction) {
    if (direction == 0)
        return;
    const auto note = find_next_navigation_note(
        m_navigationAnchor, direction,
        m_layout.display_min(), m_layout.display_max(), m_pitch_range);
    if (note) {
        SelectNote(*note);
        return;
    }
    if (onStatus)
        onStatus(wxString::FromUTF8("当前音域内没有可选择音符"));
}

bool PianoRollCtrl::HandleHistoryShortcut(const wxKeyEvent& event) {
    if (!event.ControlDown())
        return false;
    const int key = event.GetKeyCode();
    if (key == 'Z' || key == 'z') {
        ApplyHistory(!event.ShiftDown());
        return true;
    }
    if (key == 'Y' || key == 'y') {
        ApplyHistory(false);
        return true;
    }
    return false;
}

void PianoRollCtrl::ApplyHistory(bool undo) {
    auto snapshot = undo ? m_history.undo(m_map) : m_history.redo(m_map);
    if (!snapshot) {
        if (onStatus)
            onStatus(wxString::FromUTF8(
                undo ? "没有可撤销的键位修改" : "没有可重做的键位修改"));
        return;
    }
    m_map = std::move(*snapshot);
    CommitMapChanged();
    if (onStatus)
        onStatus(wxString::FromUTF8(
            undo ? "已撤销上一步键位修改" : "已重做键位修改"));
}

void PianoRollCtrl::CommitMapChanged() {
    m_selectedNote = -1;
    m_hoverNote = -1;
    m_mouseModifier = 0;
    UnsetToolTip();
    RecalcDisplayRange();
    UpdateSize();
    if (onMapChanged)
        onMapChanged(m_map);
    Refresh();
    Update();
}

void PianoRollCtrl::HandleUnselectedKey(wxKeyEvent& event) {
    if (HandleHistoryShortcut(event))
        return;
    const int key = event.GetKeyCode();
    if (event.GetModifiers() == wxMOD_NONE &&
        (key == WXK_LEFT || key == WXK_RIGHT)) {
        MoveSelection(key == WXK_RIGHT ? 1 : -1);
        return;
    }
    event.Skip();
}

void PianoRollCtrl::OnKeyDown(wxKeyEvent& event) {
    if (m_selectedNote < 0) {
        HandleUnselectedKey(event);
        return;
    }
    int kc = event.GetKeyCode();
    // 无真实键码的事件(如输入法 VK_PROCESSKEY)不能绑定, 否则会记成"键0";
    // 直接放行等待真正的按键事件, 且不要清除当前选中的琴键.
    if (kc == WXK_NONE) { event.Skip(); return; }
    if (kc == WXK_ESCAPE) {
        m_selectedNote = -1;
        if (onStatus) onStatus(m_lastHint);
        Refresh();
        Update();
        return;
    }
    // 忽略纯修饰键(等主键), 但不吞掉事件(#9)
    if (kc == WXK_SHIFT || kc == WXK_CONTROL || kc == WXK_ALT) { event.Skip(); return; }

    int vk = vk_from_wx(kc);
    // 位掩码修饰键: Ctrl=2 Shift=1 Alt=4, 可组合
    int modifier = CurrentModifier(event);

    BindKey(vk, modifier);
}

void PianoRollCtrl::OnCharHook(wxKeyEvent& event) {
    // 对话框的默认按钮处理会吞掉 Enter, 这里在 CHAR_HOOK 阶段截获回车/删除.
    // 只接管这两个键, 不影响 Tab/方向键的对话框导航.
    if (m_selectedNote < 0 && HandleHistoryShortcut(event))
        return;
    if (m_selectedNote >= 0) {
        int kc = event.GetKeyCode();
        if (kc == WXK_RETURN || kc == WXK_NUMPAD_ENTER ||
            kc == WXK_DELETE || kc == WXK_NUMPAD_DELETE) {
            BindKey(vk_from_wx(kc), CurrentModifier(event));
            return;
        }
    }
    event.Skip();
}

int PianoRollCtrl::CurrentModifier(const wxKeyEvent& event) const {
    // 位掩码修饰键: Ctrl=2 Shift=1 Alt=4, 可组合
    int modifier = 0;
    int mods = event.GetModifiers();
    if (mods & wxMOD_CONTROL) modifier |= Util::kModCtrl;
    if (mods & wxMOD_SHIFT) modifier |= Util::kModShift;
    if (mods & wxMOD_ALT) modifier |= Util::kModAlt;
    // 只接受本控件显式捕获且当前仍按住的鼠标键, 避免全局鼠标状态误入绑定。
    const wxMouseState mouse = wxGetMouseState();
    if ((m_mouseModifier & Util::kModMouseL) && mouse.LeftIsDown())
        modifier |= Util::kModMouseL;
    if ((m_mouseModifier & Util::kModMouseM) && mouse.MiddleIsDown())
        modifier |= Util::kModMouseM;
    if ((m_mouseModifier & Util::kModMouseR) && mouse.RightIsDown())
        modifier |= Util::kModMouseR;
    return modifier;
}

bool PianoRollCtrl::BindKey(int vk, int modifier) {
    if (m_selectedNote < 0) return false;
    // 0 不是合法虚拟键码, 防御性拦截(避免把无效键写进键位映射)
    if (vk <= 0) return false;

    const int note = m_selectedNote;
    const Util::KeyMapping mapping{ vk, modifier };
    const auto existing = m_map.find(note);
    if (existing != m_map.end() &&
        existing->second.vk_code == vk &&
        existing->second.modifier == modifier) {
        if (onStatus)
            onStatus(wxString::FromUTF8("该音符已使用此键位, 无需重复绑定"));
        m_selectedNote = -1;
        m_mouseModifier = 0;
        Refresh();
        return true;
    }

    m_history.record(m_map);
    m_map[note] = mapping;
    CommitMapChanged();
    wxString msg = wxString::FromUTF8("已绑定: ") + vk_name(vk, modifier);
    if (m_analysis.physical_conflicts.contains(note))
        msg += wxString::Format(
            wxString::FromUTF8(" · 警告: %d 个音符存在物理主键冲突"),
            static_cast<int>(m_analysis.physical_conflicts.size()));
    else if (m_analysis.logical_duplicates.contains(note))
        msg += wxString::Format(
            wxString::FromUTF8(" · 警告: %d 个音符存在逻辑键位重复"),
            static_cast<int>(m_analysis.logical_duplicates.size()));
    msg += wxString::FromUTF8(" · Ctrl+Z 撤销");
    if (onStatus) onStatus(msg);
    return true;
}

} // namespace UI
