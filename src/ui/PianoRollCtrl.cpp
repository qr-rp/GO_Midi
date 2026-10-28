#include "PianoRollCtrl.h"

#include <algorithm>

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>

#include <windows.h>  // VK_* 常量
#include <imm.h>      // ImmAssociateContext: 键位捕获时禁用输入法

namespace UI {

namespace {

    wxString modifier_code(int mod)
    {
        wxString code;
        if (mod & Util::kModCtrl) code += L"C";
        if (mod & Util::kModShift) code += L"S";
        if (mod & Util::kModAlt) code += L"A";
        if (mod & Util::kModMouseL) code += L"L";
        if (mod & Util::kModMouseM) code += L"M";
        if (mod & Util::kModMouseR) code += L"R";
        return code;
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

    wxColour key_label_colour(bool in_range, bool black,
                              bool logical, bool physical)
    {
        if (!in_range)
            return black ? wxColour(0xa0, 0x5a, 0x5a) : wxColour(0x8a, 0x4a, 0x4a);
        if (physical)
            return wxColour(0xe0, 0x6a, 0x6a);
        if (logical)
            return wxColour(0xd6, 0xa5, 0x45);
        return black ? wxColour(0xff, 0x9a, 0x9a) : wxColour(0xd0, 0x5a, 0x5a);
    }

} // namespace

wxBEGIN_EVENT_TABLE(PianoRollCtrl, wxWindow)
    EVT_PAINT(PianoRollCtrl::OnPaint)
    EVT_LEFT_DOWN(PianoRollCtrl::OnMouse)
    EVT_LEFT_UP(PianoRollCtrl::OnMouse)
    EVT_MIDDLE_DOWN(PianoRollCtrl::OnMouse)
    EVT_RIGHT_DOWN(PianoRollCtrl::OnMouse)
    EVT_SIZE(PianoRollCtrl::OnSize)
    EVT_MOTION(PianoRollCtrl::OnMouseMove)
    EVT_MOUSEWHEEL(PianoRollCtrl::OnMouseWheel)
    EVT_KEY_DOWN(PianoRollCtrl::OnKeyDown)
    EVT_CHAR_HOOK(PianoRollCtrl::OnCharHook)
wxEND_EVENT_TABLE()

PianoRollCtrl::PianoRollCtrl(wxWindow* parent, int minPitch, int maxPitch)
    : wxWindow(parent, wxID_ANY),
      m_whiteW(FromDIP(30)), m_whiteH(FromDIP(92)),
      m_blackW(FromDIP(20)), m_blackH(FromDIP(56)), m_scrollH(FromDIP(6)),
      m_minPitch(minPitch), m_maxPitch(maxPitch),
      m_displayMin(minPitch), m_displayMax(maxPitch),
      m_preferredViewW(FromDIP(700)), m_viewW(FromDIP(700)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    // 键位捕获必须拿到物理按键: 输入法激活时数字/字母键会先变成 VK_PROCESSKEY,
    // wxKeyEvent::GetKeyCode() 返回 WXK_NONE(0), 导致所有键都被绑定成"键0".
    // 该控件只做按键采集, 不需要文本输入, 直接解除输入法上下文.
    ImmAssociateContext(static_cast<HWND>(GetHWND()), nullptr);
    m_lastHint = wxString::FromUTF8("点击琴键 → 按下新键即绑定 (可组合 Ctrl/Shift/Alt/鼠标左中右, Esc 取消)");
    RefreshLayout();
    SetMinSize(wxSize(FromDIP(240), m_whiteH));
    SetFocus();
}

void PianoRollCtrl::RefreshLayout() {
    m_layout.configure({
        m_displayMin,
        m_displayMax,
        m_whiteW,
        m_blackW,
        m_whiteH,
        m_blackH
    });
    m_analysis = analyze_key_bindings(m_map, m_minPitch, m_maxPitch);
}

void PianoRollCtrl::UpdateSize() {
    RefreshLayout();
    int actualWidth = GetClientSize().x;
    m_viewW = actualWidth > 1
        ? actualWidth
        : std::min(m_preferredViewW, ContentWidth());
    if (m_viewW < 1) m_viewW = 1;
    ClampScroll();
    int needScroll = ContentWidth() > m_viewW;
    int h = needScroll ? m_whiteH + m_scrollH + 2 : m_whiteH;
    SetMinSize(wxSize(FromDIP(240), h));
    InvalidateBestSize();
    Refresh();
}

void PianoRollCtrl::OnSize(wxSizeEvent& event) {
    m_viewW = std::max(1, event.GetSize().x);
    ClampScroll();
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
    return wxRect(2, m_whiteH + 2, m_viewW - 4, m_scrollH);
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
    RecalcDisplayRange();
    UpdateSize();
}

void PianoRollCtrl::SetPitchRange(int minPitch, int maxPitch) {
    m_minPitch = minPitch;
    m_maxPitch = maxPitch;
    if (m_minPitch > m_maxPitch) std::swap(m_minPitch, m_maxPitch);
    m_selectedNote = -1;
    RecalcDisplayRange();
    UpdateSize();
}

void PianoRollCtrl::RecalcDisplayRange() {
    m_displayMin = m_minPitch;
    m_displayMax = m_maxPitch;
    for (const auto& pair : m_map) {
        if (pair.first < m_displayMin) m_displayMin = pair.first;
        if (pair.first > m_displayMax) m_displayMax = pair.first;
    }
}

int PianoRollCtrl::HitTest(const wxPoint& pos) const {
    return m_layout.hit_test(pos.x + m_scrollX, pos.y);
}

wxString PianoRollCtrl::NoteLabel(int note) const {
    static const wxChar* kTones[] = { L"1", L"♯1", L"2", L"♯2", L"3",
                                      L"4", L"♯4", L"5", L"♯5", L"6", L"♯6", L"7" };
    int pc = (note % 12 + 12) % 12;
    return wxString(kTones[pc]);
}

wxString PianoRollCtrl::VkName(int vk, int mod) const {
    wxString name;
    if (mod & Util::kModCtrl) name += L"Ctrl+";
    if (mod & Util::kModShift) name += L"Shift+";
    if (mod & Util::kModAlt) name += L"Alt+";
    // 鼠标修饰键
    if (mod & Util::kModMouseL) name += L"鼠标左键+";
    if (mod & Util::kModMouseM) name += L"鼠标中键+";
    if (mod & Util::kModMouseR) name += L"鼠标右键+";

    if (vk >= 'A' && vk <= 'Z') { name += wxString::Format(L"%c", vk); }
    else if (vk >= '0' && vk <= '9') { name += wxString::Format(L"%c", vk); }
    else {
        switch (vk) {
            case VK_SPACE: name += L"Space"; break;
            case VK_RETURN: name += L"Enter"; break;
            case VK_TAB: name += L"Tab"; break;
            case VK_BACK: name += L"Backspace"; break;
            case VK_ESCAPE: name += L"Esc"; break;
            case VK_DELETE: name += L"Del"; break;
            case VK_INSERT: name += L"Ins"; break;
            case VK_HOME: name += L"Home"; break;
            case VK_END: name += L"End"; break;
            case VK_PRIOR: name += L"PgUp"; break;
            case VK_NEXT: name += L"PgDn"; break;
            case VK_LEFT: name += L"←"; break;
            case VK_RIGHT: name += L"→"; break;
            case VK_UP: name += L"↑"; break;
            case VK_DOWN: name += L"↓"; break;
            case VK_SNAPSHOT: name += L"PrtSc"; break;
            case VK_SCROLL: name += L"ScrLk"; break;
            case VK_PAUSE: name += L"Pause"; break;
            case VK_CAPITAL: name += L"CapsLk"; break;
            case VK_NUMLOCK: name += L"NumLk"; break;
            case VK_LWIN: name += L"Win"; break;
            case VK_RWIN: name += L"Win"; break;
            case VK_APPS: name += L"Menu"; break;
            case VK_OEM_1: name += L";"; break;
            case VK_OEM_PLUS: name += L"+"; break;
            case VK_OEM_COMMA: name += L","; break;
            case VK_OEM_MINUS: name += L"-"; break;
            case VK_OEM_PERIOD: name += L"."; break;
            case VK_OEM_2: name += L"/"; break;
            case VK_OEM_3: name += L"`"; break;
            case VK_OEM_4: name += L"["; break;
            case VK_OEM_5: name += L"\\"; break;
            case VK_OEM_6: name += L"]"; break;
            case VK_OEM_7: name += L"'"; break;
            case VK_ADD: name += L"小+"; break;
            case VK_SUBTRACT: name += L"小-"; break;
            case VK_MULTIPLY: name += L"小*"; break;
            case VK_DIVIDE: name += L"小/"; break;
            case VK_DECIMAL: name += L"小."; break;
            default:
                if (vk >= VK_F1 && vk <= VK_F24) name += wxString::Format(L"F%d", vk - VK_F1 + 1);
                else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) name += wxString::Format(L"小%d", vk - VK_NUMPAD0);
                else name += wxString::Format(L"键%d", vk);
        }
    }
    return name;
}

int PianoRollCtrl::VkFromWx(int kc) const {
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
    const wxString modifiers = modifier_code(it->second.modifier);
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
    const wxColour border = selected
        ? wxColour(0x4f, 0x8c, 0xff)
        : (key.black ? wxColour(0x55, 0x5c, 0x6a) : wxColour(0x3a, 0x42, 0x52));
    const wxColour fill = selected
        ? (key.black ? wxColour(0x3a, 0x55, 0x8a) : wxColour(0xcf, 0xe0, 0xff))
        : (in
            ? (key.black ? wxColour(0x1a, 0x1a, 0x1e) : wxColour(0xf4, 0xf4, 0xf4))
            : (key.black ? wxColour(0x14, 0x17, 0x1c) : wxColour(0x2e, 0x34, 0x3e)));
    dc.SetPen(wxPen(border, selected ? 2 : 1));
    dc.SetBrush(wxBrush(fill));
    dc.DrawRectangle(rect);
    PaintKeyLabels(dc, rect, note, in);
}

void PianoRollCtrl::PaintKeyLabels(wxDC& dc, const wxRect& rect, int note, bool in_range) {
    const auto key = m_layout.key_geometry(note);
    const wxColour note_colour = key.black
        ? (in_range ? wxColour(0xcc, 0xd2, 0xdc) : wxColour(0x6a, 0x72, 0x80))
        : (in_range ? wxColour(0x30, 0x36, 0x42) : wxColour(0x5c, 0x64, 0x72));
    const int label_width = std::max(0, rect.width - 4);
    dc.SetTextForeground(note_colour);
    dc.DrawText(fit_label(dc, NoteLabel(note), label_width), rect.x + 2, rect.y + 3);

    const wxString key_label = CompactKeyLabel(note);
    if (key_label.IsEmpty())
        return;

    const bool logical = m_analysis.logical_duplicates.contains(note);
    const bool physical = m_analysis.physical_conflicts.contains(note);
    dc.SetTextForeground(key_label_colour(in_range, key.black, logical, physical));
    const int key_y = rect.y + rect.height - (key.black ? 13 : 14);
    dc.DrawText(fit_label(dc, key_label, label_width), rect.x + 2, key_y);
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
    wxPoint pos = event.GetPosition();

    if (event.LeftDown()) {
        if (pos.y >= m_whiteH) {
            // 底部滚动条: 点击滑块开始拖动, 否则跳转
            wxRect thumb = ScrollThumbRect();
            if (thumb.Contains(pos)) {
                m_draggingScroll = true;
                m_dragOffset = pos.x - thumb.x;
                CaptureMouse();
            } else {
                int content = ContentWidth();
                int maxScroll = content - m_viewW;
                if (maxScroll > 0) {
                    wxRect bar = ScrollBarRect();
                    double ratio = (double)(pos.x - bar.x) / bar.width;
                    m_scrollX = static_cast<int>(ratio * maxScroll);
                    ClampScroll();
                    Refresh();
                    Update();
                }
            }
            return;
        }
        int note = HitTest(pos);
        if (note >= 0) {
            if (!InAudioRange(note)) {
                // 音域外已绑定键: 只读, 需先扩展音域才能重绑
                m_selectedNote = -1;
                if (onStatus) onStatus(wxString::FromUTF8("该音符在目标音域外, 请先扩展音域再绑定"));
                Refresh();
                Update();
                return;
            }
            m_selectedNote = note;
            SetFocus();
            // #10: 记录选中提示, 供 Esc 取消时恢复
            m_lastHint = wxString::FromUTF8("已选中,按下新键绑定该音符 (可组合 Ctrl/Shift/Alt/鼠标键, Esc 取消)");
            if (onStatus) onStatus(m_lastHint);
            Refresh();
            Update();
        }
    } else if (event.LeftUp()) {
        if (m_draggingScroll) {
            m_draggingScroll = false;
            if (HasCapture()) ReleaseMouse();
        }
    } else if (event.RightDown()) {
        if (pos.y >= m_whiteH) return;   // 滚动条区忽略右键
        int note = HitTest(pos);
        if (note >= 0) {
            m_map.erase(note);
            if (onBind) onBind(note, -1, -1);
            if (onStatus) onStatus(wxString::FromUTF8("已清除该音符绑定"));
            Refresh();
            Update();
        }
    }
    event.Skip();
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

    wxString tip = NoteLabel(note);
    const auto it = m_map.find(note);
    if (it == m_map.end()) {
        SetToolTip(tip);
        return;
    }

    tip += wxString::FromUTF8(" · ") +
           VkName(it->second.vk_code, it->second.modifier);
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

void PianoRollCtrl::OnKeyDown(wxKeyEvent& event) {
    if (m_selectedNote < 0) { event.Skip(); return; }

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

    int vk = VkFromWx(kc);
    // 位掩码修饰键: Ctrl=2 Shift=1 Alt=4, 可组合
    int modifier = CurrentModifier(event);

    BindKey(vk, modifier);
}

void PianoRollCtrl::OnCharHook(wxKeyEvent& event) {
    // 对话框的默认按钮处理会吞掉 Enter, 这里在 CHAR_HOOK 阶段截获回车/删除.
    // 只接管这两个键, 不影响 Tab/方向键的对话框导航.
    if (m_selectedNote >= 0) {
        int kc = event.GetKeyCode();
        if (kc == WXK_RETURN || kc == WXK_NUMPAD_ENTER ||
            kc == WXK_DELETE || kc == WXK_NUMPAD_DELETE) {
            BindKey(VkFromWx(kc), CurrentModifier(event));
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
    // 鼠标修饰键: 按住鼠标左/中/右键 + 按键盘主键 → 组合绑定
    wxMouseState ms = wxGetMouseState();
    if (ms.LeftIsDown()) modifier |= Util::kModMouseL;
    if (ms.MiddleIsDown()) modifier |= Util::kModMouseM;
    if (ms.RightIsDown()) modifier |= Util::kModMouseR;
    return modifier;
}

bool PianoRollCtrl::BindKey(int vk, int modifier) {
    if (m_selectedNote < 0) return false;
    // 0 不是合法虚拟键码, 防御性拦截(避免把无效键写进键位映射)
    if (vk <= 0) return false;

    // #6: 检测重复键绑定(另一音符已用同一按键), 提示但不阻止
    // (引擎按 vk+hwnd 引用计数可正常处理同键多音)
    wxString dupNote;
    for (const auto& pair : m_map) {
        if (pair.first != m_selectedNote && pair.second.vk_code == vk && pair.second.modifier == modifier) {
            dupNote = NoteLabel(pair.first);
            break;
        }
    }

    m_map[m_selectedNote] = Util::KeyMapping{ vk, modifier };
    if (onBind) onBind(m_selectedNote, vk, modifier);
    wxString msg = wxString::FromUTF8("已绑定: ") + VkName(vk, modifier);
    if (!dupNote.IsEmpty()) msg += wxString::FromUTF8(" (注意: 音符 ") + dupNote + wxString::FromUTF8(" 已使用该键)");
    if (onStatus) onStatus(msg);
    m_lastHint = wxString::FromUTF8("点击琴键 → 按下新键即绑定 (可组合 Ctrl/Shift/Alt/鼠标左中右, Esc 取消)");
    m_selectedNote = -1;
    Refresh();
    Update();
    return true;
}

} // namespace UI
