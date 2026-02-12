#pragma once

#include <wx/wx.h>
#include <string>
#include <functional>

namespace UI {

// 播放状态枚举
enum class PlaybackStatus {
    Idle,           // 空闲
    Loading,        // 加载中
    Playing,        // 播放中
    Paused,         // 暂停
    Stopped,        // 停止
    Scheduled,      // 定时中
    Error           // 错误
};

// 状态机类
class PlaybackStateMachine {
public:
    PlaybackStateMachine();
    
    // 状态转换
    void TransitionTo(PlaybackStatus newState);
    
    // 获取当前状态
    PlaybackStatus GetCurrentState() const { return m_currentState; }
    
    // 获取状态对应的文本（缓存优化）
    wxString GetStateText() const;
    
    // 获取状态对应的按钮文本（缓存优化）
    wxString GetPlayButtonText() const;
    
    // 设置状态变化回调
    void SetStateChangeCallback(std::function<void(PlaybackStatus, PlaybackStatus)> callback);
    
    // 设置额外的状态信息（如文件名、错误信息）
    void SetContextInfo(const wxString& info) { m_contextInfo = info; }
    
    // 状态查询便捷方法
    bool IsPlaying() const { return m_currentState == PlaybackStatus::Playing; }
    bool IsPaused() const { return m_currentState == PlaybackStatus::Paused; }
    bool IsIdle() const { return m_currentState == PlaybackStatus::Idle; }
    bool IsActive() const { 
        return m_currentState == PlaybackStatus::Playing || 
               m_currentState == PlaybackStatus::Paused; 
    }

private:
    PlaybackStatus m_currentState;
    PlaybackStatus m_previousState;
    wxString m_contextInfo;
    std::function<void(PlaybackStatus, PlaybackStatus)> m_stateChangeCallback;
    
    // 缓存字符串以减少分配
    mutable wxString m_cachedStateText;
    mutable wxString m_cachedButtonText;
    mutable PlaybackStatus m_cachedState;
};

// 状态更新器 - 协调UI更新
class PlaybackStateUpdater {
public:
    struct UIComponents {
        wxButton* playBtn = nullptr;
        wxWindow* currentFileLabel = nullptr;  // 可以是wxStaticText*或ScrollingText*
        wxStaticText* currentTimeLabel = nullptr;
        wxStaticText* totalTimeLabel = nullptr;
        wxWindow* progressSlider = nullptr;    // 可以是wxSlider*或ModernSlider*
        wxStatusBar* statusBar = nullptr;
    };
    
    PlaybackStateUpdater(PlaybackStateMachine& stateMachine, UIComponents components);
    
    // 根据状态机更新所有UI
    void UpdateUI();
    
    // 更新特定组件
    void UpdatePlayButton();
    void UpdateStatusBar();
    void UpdateTimeLabels(double currentTime, double totalTime);
    
private:
    PlaybackStateMachine& m_stateMachine;
    UIComponents m_components;
};

} // namespace UI
