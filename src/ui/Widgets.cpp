#include "Widgets.h"
#include <wx/dcbuffer.h>

// Define Events
wxDEFINE_EVENT(wxEVT_MODERN_SLIDER_CHANGE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MODERN_SLIDER_THUMBTRACK, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MODERN_SLIDER_THUMBRELEASE, wxCommandEvent);

// ================= ModernSlider =================

wxBEGIN_EVENT_TABLE(ModernSlider, wxControl)
    EVT_PAINT(ModernSlider::OnPaint)
    EVT_SIZE(ModernSlider::OnSize)
    EVT_LEFT_DOWN(ModernSlider::OnLeftDown)
    EVT_LEFT_UP(ModernSlider::OnLeftUp)
    EVT_MOTION(ModernSlider::OnMotion)
    EVT_ENTER_WINDOW(ModernSlider::OnEnter)
    EVT_LEAVE_WINDOW(ModernSlider::OnLeave)
    EVT_ERASE_BACKGROUND(ModernSlider::OnEraseBackground)
wxEND_EVENT_TABLE()

ModernSlider::ModernSlider(wxWindow* parent, wxWindowID id, int value, int minValue, int maxValue,
                           const wxPoint& pos, const wxSize& size, long style)
    : wxControl(parent, id, pos, size, style | wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
      m_value(value), m_minValue(minValue), m_maxValue(maxValue),
      m_isDragging(false), m_isHovering(false)
{
    // Style configuration (matching Python version)
    m_trackColor = wxColour(200, 200, 200);
    m_progressColor = wxColour(0, 120, 215); // Windows Blue
    m_thumbColor = wxColour(255, 255, 255);
    m_thumbBorderColor = wxColour(180, 180, 180);

    m_trackHeight = 4.0;
    m_thumbRadius = 6.0;
    m_thumbRadiusHover = 8.0;

    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(100, 24));
}

ModernSlider::~ModernSlider() {}

int ModernSlider::GetValue() const {
    return m_value;
}

void ModernSlider::SetValue(int value) {
    if (value < m_minValue) value = m_minValue;
    if (value > m_maxValue) value = m_maxValue;

    if (m_value != value) {
        m_value = value;
        Refresh();
    }
}

void ModernSlider::SetRange(int minValue, int maxValue) {
    m_minValue = minValue;
    m_maxValue = maxValue;
    SetValue(m_value); // Re-clamp
}

void ModernSlider::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);

    if (!gc) return;

    // Enable Antialiasing
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    int width, height;
    GetClientSize(&width, &height);

    // Fill Background
    wxColour bgColor = GetParent()->GetBackgroundColour();
    dc.SetBackground(wxBrush(bgColor));
    dc.Clear();

    // Calculate coordinates
    double padding = m_thumbRadiusHover + 2;
    double trackWidth = width - 2 * padding;
    double trackY = std::floor(height / 2.0);

    // Draw Track Background
    gc->SetBrush(wxBrush(m_trackColor));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(padding, trackY - m_trackHeight / 2.0, trackWidth, m_trackHeight, m_trackHeight / 2.0);

    // Calculate Progress Width
    double rangeVal = m_maxValue - m_minValue;
    double pct = 0.0;
    if (rangeVal > 0) {
        pct = (double)(m_value - m_minValue) / rangeVal;
    }
    double progressWidth = trackWidth * pct;

    // Draw Progress
    if (progressWidth > 0) {
        gc->SetBrush(wxBrush(m_progressColor));
        gc->DrawRoundedRectangle(padding, trackY - m_trackHeight / 2.0, progressWidth, m_trackHeight, m_trackHeight / 2.0);
    }

    // Draw Thumb
    // Snap to pixel grid for crisp rendering
    double thumbX = std::round(padding + progressWidth); 
    double rawRadius = (m_isHovering || m_isDragging) ? m_thumbRadiusHover : m_thumbRadius;
    
    // Use x.5 radius for sharp 1px borders on integer coordinates
    // This ensures the 1px border is drawn exactly within a pixel, preventing blur
    double drawRadius = std::floor(rawRadius) + 0.5;

    // Shadow (Simple simulation)
    gc->SetBrush(wxBrush(wxColour(0, 0, 0, 30)));
    gc->DrawEllipse(thumbX - drawRadius + 1, trackY - drawRadius + 1, drawRadius * 2, drawRadius * 2);

    // Thumb Body
    gc->SetBrush(wxBrush(m_thumbColor));
    gc->SetPen(wxPen(m_thumbBorderColor, 1));
    gc->DrawEllipse(thumbX - drawRadius, trackY - drawRadius, drawRadius * 2, drawRadius * 2);

    delete gc;
}

void ModernSlider::OnSize(wxSizeEvent& event) {
    Refresh();
    event.Skip();
}

int ModernSlider::ValueFromPos(int x) {
    int width, height;
    GetClientSize(&width, &height);

    double padding = m_thumbRadiusHover + 2;
    double trackWidth = width - 2 * padding;

    if (trackWidth <= 0) return m_minValue;

    double relX = x - padding;
    double pct = relX / trackWidth;
    if (pct < 0.0) pct = 0.0;
    if (pct > 1.0) pct = 1.0;

    return (int)(m_minValue + pct * (m_maxValue - m_minValue));
}

void ModernSlider::OnLeftDown(wxMouseEvent& event) {
    CaptureMouse();
    m_isDragging = true;

    int val = ValueFromPos(event.GetX());
    SetValue(val);

    wxCommandEvent evt(wxEVT_MODERN_SLIDER_THUMBTRACK, GetId());
    evt.SetEventObject(this);
    evt.SetInt(m_value); // Pass value via Int
    GetEventHandler()->ProcessEvent(evt);

    Refresh();
}

void ModernSlider::OnLeftUp(wxMouseEvent& event) {
    if (HasCapture()) {
        ReleaseMouse();
    }
    m_isDragging = false;

    wxCommandEvent evt(wxEVT_MODERN_SLIDER_THUMBRELEASE, GetId());
    evt.SetEventObject(this);
    evt.SetInt(m_value);
    GetEventHandler()->ProcessEvent(evt);

    wxCommandEvent evtChange(wxEVT_MODERN_SLIDER_CHANGE, GetId());
    evtChange.SetEventObject(this);
    evtChange.SetInt(m_value);
    GetEventHandler()->ProcessEvent(evtChange);

    Refresh();
}

void ModernSlider::OnMotion(wxMouseEvent& event) {
    if (m_isDragging && event.Dragging() && event.LeftIsDown()) {
        int val = ValueFromPos(event.GetX());
        if (val != m_value) {
            SetValue(val);

            // Send Track Event
            wxCommandEvent evt(wxEVT_MODERN_SLIDER_THUMBTRACK, GetId());
            evt.SetEventObject(this);
            evt.SetInt(m_value);
            GetEventHandler()->ProcessEvent(evt);

            // Also send Change event during drag (to update label)
            wxCommandEvent evtChange(wxEVT_MODERN_SLIDER_CHANGE, GetId());
            evtChange.SetEventObject(this);
            evtChange.SetInt(m_value);
            GetEventHandler()->ProcessEvent(evtChange);
        }
    } else if (!m_isDragging) {
        m_isHovering = true;
        Refresh();
    }
}

void ModernSlider::OnEnter(wxMouseEvent& event) {
    m_isHovering = true;
    Refresh();
}

void ModernSlider::OnLeave(wxMouseEvent& event) {
    m_isHovering = false;
    Refresh();
}

void ModernSlider::OnEraseBackground(wxEraseEvent& event) {
    // Do nothing to prevent flicker
}


// ================= ScrollingText =================

wxBEGIN_EVENT_TABLE(ScrollingText, wxControl)
    EVT_PAINT(ScrollingText::OnPaint)
    EVT_SIZE(ScrollingText::OnSize)
wxEND_EVENT_TABLE()

ScrollingText::ScrollingText(wxWindow* parent, wxWindowID id, const wxString& text,
                             const wxPoint& pos, const wxSize& size)
    : wxControl(parent, id, pos, size, wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
      m_text(text), m_offset(0.0), m_spacing(0.0), m_speed(0.5), m_fps(60),
      m_timer(this), m_delayTimer(this)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetMinSize(wxSize(-1, 26));
    
    // Bind timers using their specific IDs
    this->Bind(wxEVT_TIMER, &ScrollingText::OnTimer, this, m_timer.GetId());
    this->Bind(wxEVT_TIMER, &ScrollingText::OnDelayTimer, this, m_delayTimer.GetId());
}

ScrollingText::~ScrollingText() {
    m_timer.Stop();
    m_delayTimer.Stop();
}

void ScrollingText::SetLabel(const wxString& text) {
    if (m_text == text) return;
    m_text = text;
    m_offset = 0.0;
    m_timer.Stop();
    m_delayTimer.Stop();
    CheckScrolling();
    Refresh();
}

wxString ScrollingText::GetLabel() const {
    return m_text;
}

void ScrollingText::CheckScrolling() {
    wxClientDC dc(this);
    wxFont font = GetParent()->GetFont();
    SetFont(font);
    dc.SetFont(font);

    wxSize textSize = dc.GetTextExtent(m_text);
    wxSize spaceSize = dc.GetTextExtent("    ");
    m_spacing = spaceSize.GetWidth();

    int clientW = GetClientSize().GetWidth();

    // Ensure min height
    SetMinSize(wxSize(-1, textSize.GetHeight() + 4));

    if (textSize.GetWidth() > clientW && clientW > 10) {
        if (!m_timer.IsRunning() && !m_delayTimer.IsRunning()) {
            m_delayTimer.Start(1000, wxTIMER_ONE_SHOT);
        }
    } else {
        m_timer.Stop();
        m_delayTimer.Stop();
        m_offset = 0.0;
    }
}

void ScrollingText::OnDelayTimer(wxTimerEvent& event) {
    if (!m_timer.IsRunning()) {
        m_timer.Start(1000 / m_fps);
    }
}

void ScrollingText::OnTimer(wxTimerEvent& event) {
    if (event.GetTimer().GetId() == m_delayTimer.GetId()) {
        // Should be handled by OnDelayTimer but just in case
        return;
    }

    m_offset -= m_speed;

    wxClientDC dc(this);
    dc.SetFont(GetFont());
    wxSize textSize = dc.GetTextExtent(m_text);
    double totalW = textSize.GetWidth() + m_spacing;

    if (m_offset <= -totalW) {
        m_offset += totalW;
    }
    Refresh();
}

void ScrollingText::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    
    // Background
    wxColour bgColor = GetParent()->GetBackgroundColour();
    dc.SetBackground(wxBrush(bgColor));
    dc.Clear();

    dc.SetFont(GetFont());
    dc.SetTextForeground(GetForegroundColour());

    wxSize textSize = dc.GetTextExtent(m_text);
    int clientW = GetClientSize().GetWidth();
    int clientH = GetClientSize().GetHeight();
    int y = (clientH - textSize.GetHeight()) / 2;

    if (m_timer.IsRunning()) {
        // Draw scrolling text
        dc.SetClippingRegion(GetClientRect());
        
        double x = m_offset;
        while (x < clientW) {
            dc.DrawText(m_text, (int)x, y);
            x += textSize.GetWidth() + m_spacing;
        }
        
        dc.DestroyClippingRegion();
    } else {
        // Draw static text (ellipsized if too long but timer not running yet/disabled)
        // Actually for static, usually we center or left align. 
        // If it was too long, CheckScrolling would have started timer.
        // If it fits, just draw it.
        // If it doesn't fit but logic says no scroll (e.g. very small), clip it.
        
        // Let's implement left align with ellipsis if we wanted to match wxStaticText behavior, 
        // but here we just draw it at 0.
        dc.DrawText(m_text, 0, y);
    }
}

void ScrollingText::OnSize(wxSizeEvent& event) {
    CheckScrolling();
    Refresh();
    event.Skip();
}
