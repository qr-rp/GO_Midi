#pragma once

#include <wx/wx.h>
#include <wx/graphics.h>
#include <wx/timer.h>

// Custom Events for ModernSlider
wxDECLARE_EVENT(wxEVT_MODERN_SLIDER_CHANGE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MODERN_SLIDER_THUMBTRACK, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MODERN_SLIDER_THUMBRELEASE, wxCommandEvent);

#define EVT_MODERN_SLIDER_CHANGE(id, fn) \
    wx__DECLARE_EVT1(wxEVT_MODERN_SLIDER_CHANGE, id, wxCommandEventHandler(fn))

#define EVT_MODERN_SLIDER_THUMBTRACK(id, fn) \
    wx__DECLARE_EVT1(wxEVT_MODERN_SLIDER_THUMBTRACK, id, wxCommandEventHandler(fn))

#define EVT_MODERN_SLIDER_THUMBRELEASE(id, fn) \
    wx__DECLARE_EVT1(wxEVT_MODERN_SLIDER_THUMBRELEASE, id, wxCommandEventHandler(fn))

class ModernSlider : public wxControl {
public:
    ModernSlider(wxWindow* parent, wxWindowID id, int value, int minValue, int maxValue,
                 const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = 0);
    ~ModernSlider();

    int GetValue() const;
    void SetValue(int value);
    void SetRange(int minValue, int maxValue);

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnEnter(wxMouseEvent& event);
    void OnLeave(wxMouseEvent& event);
    void OnEraseBackground(wxEraseEvent& event);

    int ValueFromPos(int x);

    int m_value;
    int m_minValue;
    int m_maxValue;

    bool m_isDragging;
    bool m_isHovering;

    // Style
    wxColour m_trackColor;
    wxColour m_progressColor;
    wxColour m_thumbColor;
    wxColour m_thumbBorderColor;

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
