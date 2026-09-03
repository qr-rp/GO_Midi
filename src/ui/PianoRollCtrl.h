#pragma once

#include <wx/wx.h>
#include <map>
#include <functional>
#include "../util/KeyManager.h"

namespace UI {

/// 钢琴卷键位编辑控件(对齐 next 分支设计):
/// 横向标准钢琴,点击琴键后按下新键即绑定,支持 Ctrl/Shift 修饰。
class PianoRollCtrl : public wxWindow {
public:
    PianoRollCtrl(wxWindow* parent, int minPitch, int maxPitch);

    void SetMap(const std::map<int, Util::KeyMapping>& map);
    void SetPitchRange(int minPitch, int maxPitch);

    /// 完成一次绑定时回调(note, vk, modifier);清除回调(note, -1, -1)
    std::function<void(int, int, int)> onBind;
    /// 状态提示回调(底部提示文字)
    std::function<void(const wxString&)> onStatus;

    /// 内容总尺寸(供外部参考; 实际滚动在控件内部)
    wxSize GetTotalSize() const { return wxSize(ContentWidth(), kWhiteH); }
    /// 内容宽度(显示范围白键数 × 键宽)
    int ContentWidth() const { return WhiteCount() * kWhiteW; }
    /// 设置可视宽度(固定, 超出部分内部横向滚动)
    void SetViewWidth(int w);

    /// 当前显示范围(音域 ∪ 已绑定键;缩小音域不隐藏已绑定键, 扩大可滚动查看)
    int DisplayMin() const { return m_displayMin; }
    int DisplayMax() const { return m_displayMax; }
    /// 该音符是否在目标音域内(音域外的已绑定键灰显, 重绑需先扩展音域)
    bool InAudioRange(int note) const { return note >= m_minPitch && note <= m_maxPitch; }

private:
    static constexpr int kWhiteW = 30;   // 白键宽
    static constexpr int kWhiteH = 92;   // 白键高
    static constexpr int kBlackW = 20;   // 黑键宽
    static constexpr int kBlackH = 56;   // 黑键高
    static constexpr int kScrollH = 6;   // 底部自绘滚动条高度

    int m_minPitch;
    int m_maxPitch;
    int m_displayMin;   // 显示范围 = 音域 ∪ 已绑定键
    int m_displayMax;
    std::map<int, Util::KeyMapping> m_map;
    int m_selectedNote = -1;   // 当前选中等待绑定
    wxString m_lastHint;

    int m_maxViewW = 700;       // 用户设定的最大可视宽(内容超出时滚动)
    int m_viewW = 700;          // 实际控件宽度 = min(内容宽, maxViewW)
    int m_scrollX = 0;          // 横向滚动偏移(像素)
    bool m_draggingScroll = false;  // 拖动底部滚动条中
    int m_dragOffset = 0;       // 拖动滑块时指针相对滑块左缘的偏移

    // 滚动辅助
    void ClampScroll();
    void UpdateSize();          // 按内容宽重算控件宽/滚动(内容变化后调用)
    wxRect ScrollBarRect() const;   // 底部滚动条轨道区域
    wxRect ScrollThumbRect() const; // 滑块区域(按内容/可视比例)

    void OnMouseWheel(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);

    int WhiteCount() const;
    bool IsBlackKey(int note) const;
    int NoteToWhiteIndex(int note) const;  // 白键序号(从 0)
    wxRect KeyRect(int note) const;
    int HitTest(const wxPoint& pos) const;

    /// 重算显示范围(音域 ∪ 已绑定键), 供 SetMap/SetPitchRange 调用
    void RecalcDisplayRange();

    void OnPaint(wxPaintEvent& event);
    void OnMouse(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    wxString NoteLabel(int note) const;   // 简谱(♯1..7)
    wxString KeyLabel(int note) const;    // 已绑定按键名(空=未绑定)
    wxString VkName(int vk, int mod) const;
    int VkFromWx(int keyCode) const;

    wxDECLARE_EVENT_TABLE();
};

} // namespace UI
