#include "Widgets.h"
#include <wx/dcbuffer.h>

// Define Events
wxDEFINE_EVENT(wxEVT_MODERN_SLIDER_CHANGE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MODERN_SLIDER_THUMBTRACK, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MODERN_SLIDER_THUMBRELEASE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_AB_POINT_SET_A, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_AB_POINT_SET_B, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_AB_POINT_CLEAR, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_AB_POINT_DRAG, wxCommandEvent);

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
    EVT_RIGHT_DOWN(ModernSlider::OnRightDown)
    EVT_RIGHT_UP(ModernSlider::OnRightUp)
wxEND_EVENT_TABLE()

ModernSlider::ModernSlider(wxWindow* parent, wxWindowID id, int value, int minValue, int maxValue,
                           const wxPoint& pos, const wxSize& size, long style)
    : wxControl(parent, id, pos, size, style | wxBORDER_NONE | wxFULL_REPAINT_ON_RESIZE),
      m_value(value), m_minValue(minValue), m_maxValue(maxValue),
      m_isDragging(false), m_isHovering(false),
      m_aPoint(-1), m_bPoint(-1), m_abState(0),
      m_isDraggingA(false), m_isDraggingB(false)
{
    // Style configuration (matching Python version)
    m_trackColor = wxColour(200, 200, 200);
    m_progressColor = wxColour(0, 120, 215); // Windows Blue
    m_thumbColor = wxColour(255, 255, 255);
    m_thumbBorderColor = wxColour(180, 180, 180);

    // AB Point colors
    m_aPointColor = wxColour(255, 100, 100);    // 红色
    m_bPointColor = wxColour(100, 255, 100);    // 绿色
    m_abRangeColor = wxColour(255, 200, 100, 80); // 半透明橙色

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

// AB Point methods
void ModernSlider::SetABPoints(int aPoint, int bPoint) {
    m_aPoint = aPoint;
    m_bPoint = bPoint;
    m_abState = (aPoint >= 0 && bPoint >= 0) ? 2 : (aPoint >= 0 ? 1 : 0);
    Refresh();
}

void ModernSlider::ClearABPoints() {
    m_aPoint = -1;
    m_bPoint = -1;
    m_abState = 0;
    m_isDraggingA = false;
    m_isDraggingB = false;
    Refresh();
}

bool ModernSlider::HasABPoints() const {
    return m_aPoint >= 0 && m_bPoint >= 0;
}

int ModernSlider::PosFromValue(int value) {
    int width, height;
    GetClientSize(&width, &height);

    double padding = m_thumbRadiusHover + 2;
    double trackWidth = width - 2 * padding;
    double rangeVal = m_maxValue - m_minValue;

    if (rangeVal <= 0) return (int)padding;

    double pct = (double)(value - m_minValue) / rangeVal;
    return (int)(padding + pct * trackWidth);
}

bool ModernSlider::IsNearAPoint(int x) {
    if (m_aPoint < 0) return false;
    int aPos = PosFromValue(m_aPoint);
    return std::abs(x - aPos) < 10;
}

bool ModernSlider::IsNearBPoint(int x) {
    if (m_bPoint < 0) return false;
    int bPos = PosFromValue(m_bPoint);
    return std::abs(x - bPos) < 10;
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

    // Draw AB Range (between A and B points)
    if (m_aPoint >= 0 && m_bPoint >= 0) {
        int minAB = std::min(m_aPoint, m_bPoint);
        int maxAB = std::max(m_aPoint, m_bPoint);
        double aPct = (double)(minAB - m_minValue) / rangeVal;
        double bPct = (double)(maxAB - m_minValue) / rangeVal;
        double aX = padding + trackWidth * aPct;
        double bX = padding + trackWidth * bPct;
        double rangeWidth = bX - aX;

        if (rangeWidth > 0) {
            gc->SetBrush(wxBrush(m_abRangeColor));
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->DrawRoundedRectangle(aX, trackY - m_trackHeight, rangeWidth, m_trackHeight * 2, m_trackHeight / 2.0);
        }
    }

    // Draw A Point marker
    if (m_aPoint >= 0) {
        double aPct = (double)(m_aPoint - m_minValue) / rangeVal;
        double aX = std::round(padding + trackWidth * aPct);
        double markerRadius = 5.0;

        // A point triangle marker (pointing down)
        gc->SetBrush(wxBrush(m_aPointColor));
        gc->SetPen(wxPen(*wxWHITE, 1));
        wxGraphicsPath path = gc->CreatePath();
        path.MoveToPoint(aX, trackY - m_trackHeight - 2);
        path.AddLineToPoint(aX - markerRadius, trackY - m_trackHeight - 2 - markerRadius * 1.5);
        path.AddLineToPoint(aX + markerRadius, trackY - m_trackHeight - 2 - markerRadius * 1.5);
        path.CloseSubpath();
        gc->FillPath(path);
        gc->StrokePath(path);

        // Draw "A" label
        wxFont font(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        gc->SetFont(font, *wxWHITE);
        double textW, textH;
        gc->GetTextExtent("A", &textW, &textH);
        gc->DrawText("A", aX - textW / 2, trackY - m_trackHeight - 2 - markerRadius * 1.5 - textH - 2);
    }

    // Draw B Point marker
    if (m_bPoint >= 0) {
        double bPct = (double)(m_bPoint - m_minValue) / rangeVal;
        double bX = std::round(padding + trackWidth * bPct);
        double markerRadius = 5.0;

        // B point triangle marker (pointing down)
        gc->SetBrush(wxBrush(m_bPointColor));
        gc->SetPen(wxPen(*wxWHITE, 1));
        wxGraphicsPath path = gc->CreatePath();
        path.MoveToPoint(bX, trackY - m_trackHeight - 2);
        path.AddLineToPoint(bX - markerRadius, trackY - m_trackHeight - 2 - markerRadius * 1.5);
        path.AddLineToPoint(bX + markerRadius, trackY - m_trackHeight - 2 - markerRadius * 1.5);
        path.CloseSubpath();
        gc->FillPath(path);
        gc->StrokePath(path);

        // Draw "B" label
        wxFont font(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        gc->SetFont(font, *wxWHITE);
        double textW, textH;
        gc->GetTextExtent("B", &textW, &textH);
        gc->DrawText("B", bX - textW / 2, trackY - m_trackHeight - 2 - markerRadius * 1.5 - textH - 2);
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
    } else if (m_isDraggingA && event.Dragging() && event.RightIsDown()) {
        // 拖动A点
        int val = ValueFromPos(event.GetX());
        if (val != m_aPoint) {
            m_aPoint = val;
            Refresh();

            // Send AB Point drag event
            wxCommandEvent evt(wxEVT_AB_POINT_DRAG, GetId());
            evt.SetEventObject(this);
            evt.SetInt(m_aPoint);
            evt.SetClientData(reinterpret_cast<void*>(static_cast<intptr_t>(1))); // 1 = A point
            GetEventHandler()->ProcessEvent(evt);
        }
    } else if (m_isDraggingB && event.Dragging() && event.RightIsDown()) {
        // 拖动B点
        int val = ValueFromPos(event.GetX());
        if (val != m_bPoint) {
            m_bPoint = val;
            Refresh();

            // Send AB Point drag event
            wxCommandEvent evt(wxEVT_AB_POINT_DRAG, GetId());
            evt.SetEventObject(this);
            evt.SetInt(m_bPoint);
            evt.SetClientData(reinterpret_cast<void*>(static_cast<intptr_t>(2))); // 2 = B point
            GetEventHandler()->ProcessEvent(evt);
        }
    } else if (!m_isDragging) {
        m_isHovering = true;
        // 设置鼠标光标提示
        if (IsNearAPoint(event.GetX()) || IsNearBPoint(event.GetX())) {
            SetCursor(wxCursor(wxCURSOR_SIZEWE));
        } else {
            SetCursor(*wxSTANDARD_CURSOR);
        }
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

void ModernSlider::OnRightDown(wxMouseEvent& event) {
    int x = event.GetX();

    // 如果AB点都已设置，检查是否在A或B点附近开始拖动
    if (m_abState == 2) {
        if (IsNearAPoint(x)) {
            CaptureMouse();
            m_isDraggingA = true;
            SetCursor(wxCursor(wxCURSOR_SIZEWE));
            return;
        } else if (IsNearBPoint(x)) {
            CaptureMouse();
            m_isDraggingB = true;
            SetCursor(wxCursor(wxCURSOR_SIZEWE));
            return;
        }
    }

    // 不跳过事件，让 OnRightUp 处理
    event.Skip();
}

void ModernSlider::OnRightUp(wxMouseEvent& event) {
    if (m_isDraggingA || m_isDraggingB) {
        // 结束拖动
        if (HasCapture()) {
            ReleaseMouse();
        }
        m_isDraggingA = false;
        m_isDraggingB = false;
        SetCursor(*wxSTANDARD_CURSOR);
        Refresh();
        return;
    }

    // 不是拖动，处理点击设置AB点
    int x = event.GetX();

    // 状态机处理
    if (m_abState == 0) {
        // 设置A点
        m_aPoint = ValueFromPos(x);
        m_abState = 1;
        Refresh();

        wxCommandEvent evt(wxEVT_AB_POINT_SET_A, GetId());
        evt.SetEventObject(this);
        evt.SetInt(m_aPoint);
        GetEventHandler()->ProcessEvent(evt);
    }
    else if (m_abState == 1) {
        // 设置B点
        m_bPoint = ValueFromPos(x);
        m_abState = 2;
        Refresh();

        wxCommandEvent evt(wxEVT_AB_POINT_SET_B, GetId());
        evt.SetEventObject(this);
        evt.SetInt(m_bPoint);
        GetEventHandler()->ProcessEvent(evt);
    }
    else if (m_abState == 2) {
        // 如果不是在A或B点附近，则清除AB点
        if (!IsNearAPoint(x) && !IsNearBPoint(x)) {
            m_aPoint = -1;
            m_bPoint = -1;
            m_abState = 0;
            Refresh();

            wxCommandEvent evt(wxEVT_AB_POINT_CLEAR, GetId());
            evt.SetEventObject(this);
            GetEventHandler()->ProcessEvent(evt);
        }
    }
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
