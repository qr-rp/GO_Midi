#pragma once

#include <wx/wx.h>
#include <wx/graphics.h>
#include <wx/timer.h>

// Custom Events for ModernSlider
wxDECLARE_EVENT(wxEVT_MODERN_SLIDER_CHANGE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MODERN_SLIDER_THUMBTRACK, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MODERN_SLIDER_THUMBRELEASE, wxCommandEvent);

// AB Point Events
wxDECLARE_EVENT(wxEVT_AB_POINT_SET_A, wxCommandEvent);      // 设置A点
wxDECLARE_EVENT(wxEVT_AB_POINT_SET_B, wxCommandEvent);      // 设置B点
wxDECLARE_EVENT(wxEVT_AB_POINT_CLEAR, wxCommandEvent);      // 清除AB点
wxDECLARE_EVENT(wxEVT_AB_POINT_DRAG, wxCommandEvent);       // 拖动AB点

#define EVT_MODERN_SLIDER_CHANGE(id, fn) \
    wx__DECLARE_EVT1(wxEVT_MODERN_SLIDER_CHANGE, id, wxCommandEventHandler(fn))

#define EVT_MODERN_SLIDER_THUMBTRACK(id, fn) \
    wx__DECLARE_EVT1(wxEVT_MODERN_SLIDER_THUMBTRACK, id, wxCommandEventHandler(fn))

#define EVT_MODERN_SLIDER_THUMBRELEASE(id, fn) \
    wx__DECLARE_EVT1(wxEVT_MODERN_SLIDER_THUMBRELEASE, id, wxCommandEventHandler(fn))

#define EVT_AB_POINT_SET_A(id, fn) \
    wx__DECLARE_EVT1(wxEVT_AB_POINT_SET_A, id, wxCommandEventHandler(fn))

#define EVT_AB_POINT_SET_B(id, fn) \
    wx__DECLARE_EVT1(wxEVT_AB_POINT_SET_B, id, wxCommandEventHandler(fn))

#define EVT_AB_POINT_CLEAR(id, fn) \
    wx__DECLARE_EVT1(wxEVT_AB_POINT_CLEAR, id, wxCommandEventHandler(fn))

#define EVT_AB_POINT_DRAG(id, fn) \
    wx__DECLARE_EVT1(wxEVT_AB_POINT_DRAG, id, wxCommandEventHandler(fn))

class ModernSlider : public wxControl {
public:
    ModernSlider(wxWindow* parent, wxWindowID id, int value, int minValue, int maxValue,
                 const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);
    ~ModernSlider();

    int GetValue() const;
    void SetValue(int value);
    void SetRange(int minValue, int maxValue);

    // AB Point methods
    void SetABPoints(int aPoint, int bPoint);  // 设置AB点，-1表示未设置
    void ClearABPoints();                       // 清除AB点
    bool HasABPoints() const;                   // 是否已设置AB点
    int GetAPoint() const { return m_aPoint; }
    int GetBPoint() const { return m_bPoint; }

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnEnter(wxMouseEvent& event);
    void OnLeave(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void OnRightDown(wxMouseEvent& event);
    void OnRightUp(wxMouseEvent& event);
    void OnContextMenu(wxContextMenuEvent& event);

    int ValueFromPos(int x);
    int PosFromValue(int value);
    bool IsNearAPoint(int x);
    bool IsNearBPoint(int x);

    int m_value;
    int m_minValue;
    int m_maxValue;

    bool m_isDragging;
    bool m_isHovering;

    // AB Point state
    int m_aPoint;           // A点值，-1表示未设置
    int m_bPoint;           // B点值，-1表示未设置
    int m_abState;          // 0=无, 1=已设置A点等待B, 2=AB点已设置
    bool m_isDraggingA;     // 正在拖动A点
    bool m_isDraggingB;     // 正在拖动B点

    // Style
    wxColour m_trackColor;
    wxColour m_progressColor;
    wxColour m_thumbColor;
    wxColour m_thumbBorderColor;
    wxColour m_aPointColor;
    wxColour m_bPointColor;
    wxColour m_abRangeColor;

    double m_trackHeight;
    double m_thumbRadius;
    double m_thumbRadiusHover;

    wxDECLARE_EVENT_TABLE();
};

class ScrollingText : public wxControl {
public:
    ScrollingText(wxWindow* parent, wxWindowID id, const wxString& text = "",
                  const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~ScrollingText();

    void SetLabel(const wxString& text) override;
    wxString GetLabel() const override;

private:
    void OnPaint(wxPaintEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnDelayTimer(wxTimerEvent& event);
    void OnSize(wxSizeEvent& event);

    void CheckScrolling();

    wxString m_text;
    double m_offset;
    double m_spacing;
    double m_speed;
    int m_fps;

    wxTimer m_timer;
    wxTimer m_delayTimer;

    wxDECLARE_EVENT_TABLE();
};
