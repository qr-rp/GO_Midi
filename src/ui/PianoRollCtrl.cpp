#include "PianoRollCtrl.h"

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>

#include <windows.h>  // VK_* 常量

namespace UI {

wxBEGIN_EVENT_TABLE(PianoRollCtrl, wxWindow)
    EVT_PAINT(PianoRollCtrl::OnPaint)
    EVT_LEFT_DOWN(PianoRollCtrl::OnMouse)
    EVT_LEFT_UP(PianoRollCtrl::OnMouse)
    EVT_MIDDLE_DOWN(PianoRollCtrl::OnMouse)
    EVT_RIGHT_DOWN(PianoRollCtrl::OnMouse)
    EVT_MOTION(PianoRollCtrl::OnMouseMove)
    EVT_MOUSEWHEEL(PianoRollCtrl::OnMouseWheel)
    EVT_KEY_DOWN(PianoRollCtrl::OnKeyDown)
wxEND_EVENT_TABLE()

PianoRollCtrl::PianoRollCtrl(wxWindow* parent, int minPitch, int maxPitch)
    : wxWindow(parent, wxID_ANY),
      m_whiteW(FromDIP(30)), m_whiteH(FromDIP(92)),
      m_blackW(FromDIP(20)), m_blackH(FromDIP(56)), m_scrollH(FromDIP(6)),
      m_minPitch(minPitch), m_maxPitch(maxPitch),
      m_displayMin(minPitch), m_displayMax(maxPitch),
      m_viewW(FromDIP(700)) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    m_lastHint = wxString::FromUTF8("点击琴键 → 按下新键即绑定 (可组合 Ctrl/Shift/Alt/鼠标左中右, Esc 取消)");
    SetMinSize(wxSize(m_viewW, m_whiteH));
    SetFocus();
}

void PianoRollCtrl::SetViewWidth(int w) {
    if (w < 100) w = 100;
    m_maxViewW = w;
    UpdateSize();
}

void PianoRollCtrl::UpdateSize() {
    // 控件宽 = min(内容宽, 最大可视宽); 内容窄则收缩不留右侧空隙, 内容超则滚动
    int content = ContentWidth();
    m_viewW = std::min(m_maxViewW, content);
    if (m_viewW < 100) m_viewW = 100;
    ClampScroll();
    // 高度 = 琴键高 + (仅当需要滚动时才 + 滚动条高, 否则不留深色预留条)
    int needScroll = content > m_viewW;
    int h = needScroll ? m_whiteH + m_scrollH + 2 : m_whiteH;
    SetMinSize(wxSize(m_viewW, h));
    SetSize(wxSize(m_viewW, h));
    InvalidateBestSize();
    Refresh();
    Update();
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

int PianoRollCtrl::WhiteCount() const {
    int c = 0;
    for (int n = m_displayMin; n <= m_displayMax; ++n)
        if (!IsBlackKey(n)) ++c;
    return c;
}

bool PianoRollCtrl::IsBlackKey(int note) const {
    int pc = (note % 12 + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

int PianoRollCtrl::NoteToWhiteIndex(int note) const {
    int wi = 0;
    for (int n = m_displayMin; n < note; ++n)
        if (!IsBlackKey(n)) ++wi;
    return wi;
}

wxRect PianoRollCtrl::KeyRect(int note) const {
    if (!IsBlackKey(note)) {
        return wxRect(NoteToWhiteIndex(note) * m_whiteW, 0, m_whiteW, m_whiteH);
    }
    // 黑键: 位于左侧白键(前一音)的右边缘
    int leftWhite = note - 1;
    int wi = NoteToWhiteIndex(leftWhite);
    return wxRect(wi * m_whiteW + m_whiteW - m_blackW / 2, 0, m_blackW, m_blackH);
}

int PianoRollCtrl::HitTest(const wxPoint& pos) const {
    if (pos.y < 0 || pos.y >= m_whiteH) return -1;
    // 可视坐标 → 内容坐标(加回滚动偏移)
    int cx = pos.x + m_scrollX;
    if (cx < 0) return -1;
    // 黑键优先(上层)
    for (int n = m_displayMin; n <= m_displayMax; ++n) {
        if (IsBlackKey(n) && KeyRect(n).Contains(cx, pos.y)) return n;
    }
    for (int n = m_displayMin; n <= m_displayMax; ++n) {
        if (!IsBlackKey(n) && KeyRect(n).Contains(cx, pos.y)) return n;
    }
    return -1;
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
        case WXK_RETURN: return VK_RETURN;
        case WXK_TAB: return VK_TAB;
        case WXK_BACK: return VK_BACK;
        case WXK_ESCAPE: return VK_ESCAPE;
        case WXK_DELETE: return VK_DELETE;
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

wxString PianoRollCtrl::KeyLabel(int note) const {
    auto it = m_map.find(note);
    if (it == m_map.end()) return wxString();
    return VkName(it->second.vk_code, it->second.modifier);
}

void PianoRollCtrl::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(this);
    wxSize sz = GetClientSize();
    dc.SetBackground(wxBrush(wxColour(0x10, 0x14, 0x1a)));
    dc.Clear();

    // MSVC 下 wxFont small(FromDIP(8), ...) 触发最烦人解析(C2628), 先存变量消除歧义
    int smallSize = FromDIP(8);
    wxFont small(smallSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    dc.SetFont(small);

    const wxColour kSelBorder(0x4f, 0x8c, 0xff);
    const wxColour kWhite(0xf4, 0xf4, 0xf4);
    const wxColour kBlack(0x1a, 0x1a, 0x1e);
    const wxColour kBind(0xd0, 0x5a, 0x5a);

    // 白键(音域外已绑定键灰显; 内容坐标平移 m_scrollX, 裁剪可视区)
    for (int n = m_displayMin; n <= m_displayMax; ++n) {
        if (IsBlackKey(n)) continue;
        wxRect r = KeyRect(n);
        r.x -= m_scrollX;
        if (r.x + r.width < 0 || r.x > m_viewW) continue;
        bool in = InAudioRange(n);
        bool sel = (n == m_selectedNote);
        dc.SetPen(wxPen(sel ? kSelBorder : wxColour(0x3a, 0x42, 0x52), sel ? 2 : 1));
        dc.SetBrush(wxBrush(sel ? wxColour(0xcf, 0xe0, 0xff)
                               : (in ? kWhite : wxColour(0x2e, 0x34, 0x3e))));
        dc.DrawRectangle(r);
        dc.SetTextForeground(in ? wxColour(0x30, 0x36, 0x42) : wxColour(0x5c, 0x64, 0x72));
        dc.DrawText(NoteLabel(n), r.x + 2, r.y + 3);
        wxString kl = KeyLabel(n);
        if (!kl.IsEmpty()) {
            dc.SetTextForeground(in ? kBind : wxColour(0x8a, 0x4a, 0x4a));
            dc.DrawText(kl, r.x + 2, r.y + r.height - 14);
        }
    }
    // 黑键(音域外已绑定键灰显)
    for (int n = m_displayMin; n <= m_displayMax; ++n) {
        if (!IsBlackKey(n)) continue;
        wxRect r = KeyRect(n);
        r.x -= m_scrollX;
        if (r.x + r.width < 0 || r.x > m_viewW) continue;
        bool in = InAudioRange(n);
        bool sel = (n == m_selectedNote);
        dc.SetPen(wxPen(sel ? kSelBorder : wxColour(0x55, 0x5c, 0x6a), sel ? 2 : 1));
        dc.SetBrush(wxBrush(sel ? wxColour(0x3a, 0x55, 0x8a)
                               : (in ? kBlack : wxColour(0x14, 0x17, 0x1c))));
        dc.DrawRectangle(r);
        dc.SetTextForeground(in ? wxColour(0xcc, 0xd2, 0xdc) : wxColour(0x6a, 0x72, 0x80));
        dc.DrawText(NoteLabel(n), r.x + 2, r.y + 3);
        wxString kl = KeyLabel(n);
        if (!kl.IsEmpty()) {
            dc.SetTextForeground(in ? wxColour(0xff, 0x9a, 0x9a) : wxColour(0xa0, 0x5a, 0x5a));
            dc.DrawText(kl, r.x + 2, r.y + r.height - 13);
        }
    }

    // 底部横向滚动条(内容超出可视区才显示)
    if (ContentWidth() > m_viewW) {
        wxRect bar = ScrollBarRect();
        dc.SetPen(wxPen(wxColour(0x2a, 0x30, 0x3a)));
        dc.SetBrush(wxBrush(wxColour(0x1c, 0x22, 0x2a)));
        dc.DrawRoundedRectangle(bar, 3);
        wxRect thumb = ScrollThumbRect();
        dc.SetBrush(wxBrush(wxColour(0x4a, 0x54, 0x64)));
        dc.DrawRoundedRectangle(thumb, 3);
    }
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
    }
    event.Skip();
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

    BindKey(vk, modifier);
}

bool PianoRollCtrl::BindKey(int vk, int modifier) {
    if (m_selectedNote < 0) return false;

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
