#include "PlaybackState.h"

namespace UI {

PlaybackStateMachine::PlaybackStateMachine() 
    : m_currentState(PlaybackStatus::Idle)
    , m_previousState(PlaybackStatus::Idle)
    , m_cachedState(PlaybackStatus::Error) // 无效状态强制初始化缓存
{
}

void PlaybackStateMachine::TransitionTo(PlaybackStatus newState) {
    if (m_currentState == newState) return;
    
    m_previousState = m_currentState;
    m_currentState = newState;
    
    // 清除缓存
    m_cachedState = PlaybackStatus::Error;
    
    if (m_stateChangeCallback) {
        m_stateChangeCallback(m_previousState, m_currentState);
    }
}

wxString PlaybackStateMachine::GetStateText() const {
    // 缓存优化：如果状态未改变，返回缓存的字符串
    if (m_cachedState == m_currentState && !m_cachedStateText.IsEmpty()) {
        return m_cachedStateText;
    }
    
    m_cachedState = m_currentState;
    
    switch (m_currentState) {
        case PlaybackStatus::Idle:
        case PlaybackStatus::Playing:
        case PlaybackStatus::Stopped:
            m_cachedStateText = wxString::FromUTF8("By:最终幻想14水晶世界_黄金谷_吸溜");
            break;
        case PlaybackStatus::Loading:
            m_cachedStateText = wxString::FromUTF8("加载中...");
            break;
        case PlaybackStatus::Paused:
            m_cachedStateText = wxString::FromUTF8("已暂停");
            break;
        case PlaybackStatus::Scheduled:
            m_cachedStateText = wxString::FromUTF8("定时: ") + m_contextInfo;
            break;
        case PlaybackStatus::Error:
            m_cachedStateText = wxString::FromUTF8("错误: ") + m_contextInfo;
            break;
        default:
            m_cachedStateText = wxString::FromUTF8("未知状态");
    }
    
    return m_cachedStateText;
}

wxString PlaybackStateMachine::GetPlayButtonText() const {
    // 缓存优化
    if (m_cachedState == m_currentState && !m_cachedButtonText.IsEmpty()) {
        return m_cachedButtonText;
    }
    
    switch (m_currentState) {
        case PlaybackStatus::Playing:
            m_cachedButtonText = wxString::FromUTF8("暂停");
            break;
        case PlaybackStatus::Paused:
            m_cachedButtonText = wxString::FromUTF8("继续");
            break;
        default:
            m_cachedButtonText = wxString::FromUTF8("播放");
    }
    
    return m_cachedButtonText;
}

void PlaybackStateMachine::SetStateChangeCallback(
    std::function<void(PlaybackStatus, PlaybackStatus)> callback
) {
    m_stateChangeCallback = callback;
}

// PlaybackStateUpdater Implementation

PlaybackStateUpdater::PlaybackStateUpdater(
    PlaybackStateMachine& stateMachine,
    UIComponents components
)
    : m_stateMachine(stateMachine)
    , m_components(components)
{
}

void PlaybackStateUpdater::UpdateUI() {
    UpdatePlayButton();
    UpdateStatusBar();
}

void PlaybackStateUpdater::UpdatePlayButton() {
    if (m_components.playBtn) {
        m_components.playBtn->SetLabel(m_stateMachine.GetPlayButtonText());
    }
}

void PlaybackStateUpdater::UpdateStatusBar() {
    if (m_components.statusBar) {
        m_components.statusBar->SetStatusText(m_stateMachine.GetStateText(), 0);
    }
}

void PlaybackStateUpdater::UpdateTimeLabels(double currentTime, double totalTime) {
    if (m_components.currentTimeLabel) {
        int sec = static_cast<int>(currentTime);
        m_components.currentTimeLabel->SetLabel(
            wxString::Format("%02d:%02d", sec / 60, sec % 60)
        );
    }
    
    if (m_components.totalTimeLabel) {
        int sec = static_cast<int>(totalTime);
        m_components.totalTimeLabel->SetLabel(
            wxString::Format("%02d:%02d", sec / 60, sec % 60)
        );
    }
}

} // namespace UI
