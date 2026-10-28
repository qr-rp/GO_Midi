#pragma once

#include <wx/wx.h>
#include <map>
#include <functional>
#include "../util/KeyManager.h"
#include "PianoRollModel.h"

namespace UI {

/// 横向钢琴卷键位编辑器。
class PianoRollCtrl : public wxWindow {
public:
    PianoRollCtrl(wxWindow* parent, int minPitch, int maxPitch);

    void SetMap(const std::map<int, Util::KeyMapping>& map);
    void SetPitchRange(int minPitch, int maxPitch);

    std::function<void(const std::map<int, Util::KeyMapping>&)> onMapChanged;
    std::function<void(const wxString&)> onStatus;

    int ContentWidth() const { return m_layout.content_width(); }
    const PianoRollBindingAnalysis& GetBindingAnalysis() const { return m_analysis; }
    bool InAudioRange(int note) const { return m_pitch_range.contains(note); }

private:
    PianoPitchRange m_pitch_range;
    PianoRollLayoutConfig m_layout_config;
    std::map<int, Util::KeyMapping> m_map;
    PianoRollLayout m_layout;
    PianoRollBindingAnalysis m_analysis;
    PianoRollEditHistory m_history;
    int m_selectedNote = -1;   // 当前选中等待绑定
    int m_navigationAnchor = -1;
    int m_hoverNote = -1;
    int m_mouseModifier = 0;
    wxString m_lastHint;

    int m_viewW;
    int m_scrollX = 0;          // 横向滚动偏移(像素)
    bool m_draggingScroll = false;  // 拖动底部滚动条中
    int m_dragOffset = 0;       // 拖动滑块时指针相对滑块左缘的偏移

    void ClampScroll();
    void RefreshLayout();
    void UpdateSize();          // 重算最小高度/滚动状态(内容变化后调用)
    wxRect ScrollBarRect() const;   // 底部滚动条轨道区域
    wxRect ScrollThumbRect() const; // 滑块区域(按内容/可视比例)

    void OnSize(wxSizeEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);

    int HitTest(const wxPoint& pos) const;
    void UpdateHoverTooltip(const wxPoint& pos);
    bool HandleHistoryShortcut(const wxKeyEvent& event);
    void HandleUnselectedKey(wxKeyEvent& event);
    void SelectNote(int note);
    void MoveSelection(int direction);
    void ApplyHistory(bool undo);
    void CommitMapChanged();

    void RecalcDisplayRange();

    void OnPaint(wxPaintEvent& event);
    void PaintKeys(wxDC& dc, bool paint_black);
    void PaintKey(wxDC& dc, int note);
    void PaintScrollBar(wxDC& dc);
    void OnMouse(wxMouseEvent& event);
    void HandlePrimaryDown(const wxPoint& pos);
    void HandleSecondaryDown(const wxPoint& pos);
    void BeginScrollInteraction(const wxPoint& pos);
    bool SetMouseModifierFromButton(int modifier, const wxPoint& pos);
    void OnKeyDown(wxKeyEvent& event);
    void OnCharHook(wxKeyEvent& event);

    int CurrentModifier(const wxKeyEvent& event) const;
    bool BindKey(int vk, int modifier);

    wxString CompactKeyLabel(int note) const;

    wxDECLARE_EVENT_TABLE();
};

} // namespace UI
