#pragma once

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/slider.h>
#include <wx/spinctrl.h>
#include <wx/choice.h>
#include <wx/tglbtn.h>
#include <wx/statline.h>
#include <wx/confbase.h>
#include <wx/dnd.h>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>
#include <future>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <random>

#include "../core/PlaybackEngine.h"
#include "Widgets.h"
#include "PlaybackState.h"
#include "../util/PlaylistManager.h"

// Forward declaration
class MainFrame;

// File drop target for drag & drop support
class MidiFileDropTarget : public wxFileDropTarget {
public:
    MidiFileDropTarget(MainFrame* frame);
    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString& filenames) override;
private:
    MainFrame* m_frame;
};

// Define Control IDs
enum {
    ID_IMPORT_BTN = 1001,
    ID_REMOVE_BTN,
    ID_CLEAR_BTN,
    ID_SEARCH_CTRL,
    ID_PLAYLIST_CTRL,
    
    // 多播放列表控件ID
    ID_PLAYLIST_CHOICE,
    ID_ADD_PLAYLIST_BTN,
    ID_DELETE_PLAYLIST_BTN,
    ID_RENAME_PLAYLIST_BTN,
    
    ID_PREV_BTN,
    ID_PLAY_BTN,
    ID_STOP_BTN,
    ID_NEXT_BTN,
    ID_MODE_BTN,
    ID_DECOMPOSE_BTN,
    
    ID_PROGRESS_SLIDER,
    ID_SPEED_CTRL,
    ID_MIN_PITCH_CTRL,
    ID_MAX_PITCH_CTRL,
    
    ID_LOAD_KEYMAP_BTN,
    ID_SAVE_KEYMAP_BTN,
    ID_RESET_KEYMAP_BTN,
    ID_SCHEDULE_BTN,

    
    // Timer IDs
    ID_PLAYBACK_TIMER = 2001,
    ID_STATUS_TIMER,
    ID_NTP_TIMER,
    ID_SCHEDULE_TRIGGER
};

// Structure to hold controls for a single channel
struct ChannelControls {
    wxToggleButton* enableBtn;
    wxChoice* windowChoice;
    wxSpinCtrl* transposeCtrl;

    wxChoice* trackChoice;
    int channelIndex;
};

class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame();
    
    // 添加拖放的文件到播放列表
    void AddDroppedFiles(const wxArrayString& files);

private:
    // UI Initialization
    void InitUI();
    void InitPlaylistPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    void InitControlPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    void InitChannelPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    void InitKeymapPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    
    wxPanel* CreateChannelConfig(wxPanel* parent, int index);

    // Event Handlers
    void OnImportFile(wxCommandEvent& event);
    void OnRemoveFile(wxCommandEvent& event);
    void OnClearList(wxCommandEvent& event);
    void OnSearch(wxCommandEvent& event);
    void OnPlaylistSelected(wxListEvent& event);
    void OnPlaylistActivated(wxListEvent& event);
    void OnPlaylistBeginDrag(wxListEvent& event);
    void OnPlaylistEndDrag(wxMouseEvent& event);
    
    // 多播放列表事件处理
    void OnPlaylistChoice(wxCommandEvent& event);
    void OnAddPlaylist(wxCommandEvent& event);
    void OnDeletePlaylist(wxCommandEvent& event);
    void OnRenamePlaylist(wxCommandEvent& event);
    
    void OnPlay(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnPrev(wxCommandEvent& event);
    void OnNext(wxCommandEvent& event);
    void OnModeClick(wxCommandEvent& event);
    void OnDecomposeToggle(wxCommandEvent& event);
    
    void OnSliderTrack(wxCommandEvent& event);
    void OnSliderRelease(wxCommandEvent& event);
    void OnSliderChange(wxCommandEvent& event);

    // AB Point Event Handlers
    void OnABPointSetA(wxCommandEvent& event);
    void OnABPointSetB(wxCommandEvent& event);
    void OnABPointClear(wxCommandEvent& event);
    void OnABPointDrag(wxCommandEvent& event);
    
    void OnSpeedChange(wxSpinDoubleEvent& event);
    void OnPitchRangeChange(wxSpinEvent& event);
    
    void OnLoadKeymap(wxCommandEvent& event);
    void OnSaveKeymap(wxCommandEvent& event);
    void OnResetKeymap(wxCommandEvent& event);
    void OnSchedule(wxCommandEvent& event);

    
    /**
     * @brief 处理状态栏延迟补偿输入框数值变化（微调箭头）。
     */
    void OnLatencyCompSpin(wxSpinDoubleEvent& event);
    
    /**
     * @brief 处理状态栏延迟补偿输入框文本变化（输入时实时生效）。
     */
    void OnLatencyCompText(wxCommandEvent& event);
    
    // Custom event handlers
    void OnNtpSyncComplete(wxCommandEvent& event);
    void OnScheduleTrigger(wxCommandEvent& event);
    
    // Global Hook
    void InstallGlobalHook();
    void UninstallGlobalHook();

    // Thread management
    void StartBackgroundTask(std::function<void()> task);
    void CleanupFinishedThreads();
    
    /**
     * @brief 重新布局状态栏内的嵌入控件（延迟补偿输入框）。
     */
    void LayoutStatusBarControls();
    
    // Channel Events
    void OnWindowChoiceDropdown(wxMouseEvent& event); // For updating window list on click

    // Timer Events
    void OnTimer(wxTimerEvent& event);
    void OnStatusTimer(wxTimerEvent& event);

    // Helpers
    void UpdateStatusText(const wxString& text);
    void PlayIndex(int viewIndex, bool autoPlay = true);
    void UpdateChannelUI(int channelIndex, bool enabled);
    void UpdateWindowList();
    void UpdateTrackList(); // Updates track choices in all channel configs
    
    // 多播放列表辅助函数
    void RefreshPlaylistUI();                           // 刷新文件列表UI
    void UpdatePlaylistChoice();                        // 更新播放列表下拉框
    void SwitchToPlaylist(int index);                   // 切换播放列表
    
    // Enhanced Random Playback
    void InitializeRandomShuffle();
    int GetNextRandomIndex();
    void ResetRandomSequence();
    
    // Window Title Processing
    wxString RemoveParenthesesContent(const wxString& title);
    bool CompareWindowTitleAndProcess(const wxString& configTitle, const wxString& configProcess, 
                                    const Core::KeyboardSimulator::WindowInfo& windowInfo);
    
    // State Machine Callbacks
    void OnStateChange(UI::PlaybackStatus oldState, UI::PlaybackStatus newState);

    // AI State


    // Config
    void LoadFileConfig(const wxString& filename);
    void SaveFileConfig();
    void LoadGlobalConfig();
    void SaveGlobalConfig();
    void LoadPlaylistConfig();
    void SavePlaylistConfig();
    void LoadKeymapConfig();
    void SaveKeymapConfig();
    void LoadLastSelectedFile();
    void SaveLastSelectedFile();
    std::unique_ptr<wxConfigBase> m_config;

    // UI Members - Playlist
    wxButton* m_importBtn;
    wxButton* m_removeBtn;
    wxButton* m_clearBtn;
    wxTextCtrl* m_searchCtrl;
    wxListView* m_playlistCtrl;
    
    // 多播放列表UI成员
    wxChoice* m_playlistChoice;
    wxButton* m_addPlaylistBtn;
    wxButton* m_deletePlaylistBtn;
    wxButton* m_renamePlaylistBtn;
    
    // 播放列表管理器
    Util::PlaylistManager m_playlistManager;

    // UI Members - Controls
    wxButton* m_prevBtn;
    wxButton* m_playBtn;
    wxButton* m_stopBtn;
    wxButton* m_nextBtn;
    wxButton* m_modeBtn;
    wxToggleButton* m_decomposeBtn;
    
    wxStaticText* m_currentTimeLabel;
    wxStaticText* m_totalTimeLabel;
    ModernSlider* m_progressSlider;
    
    wxSpinCtrlDouble* m_speedCtrl;
    wxSpinCtrl* m_minPitchCtrl;
    wxSpinCtrl* m_maxPitchCtrl;
    ScrollingText* m_currentFileLabel;

    // UI Members - Channels
    std::vector<ChannelControls> m_channelConfigs;
    
    // UI Members - Keymap & NTP
    wxButton* m_loadKeymapBtn;
    wxButton* m_saveKeymapBtn;
    wxButton* m_resetKeymapBtn;
    wxStaticText* m_ntpLabel;
    wxSpinCtrl* m_schedMin;
    wxSpinCtrl* m_schedSec;
    wxButton* m_scheduleBtn;

    // Core Components
    Core::PlaybackEngine m_engine;
    std::unique_ptr<Midi::MidiFile> m_current_midi;
    std::vector<Core::KeyboardSimulator::WindowInfo> m_windowList; // Cache window list
    wxString m_current_path;
    wxTimer m_timer;
    wxTimer m_statusTimer;
    

    
    // StatusBar embedded control (latency compensation)
    wxStaticText* m_latencyCompLabel = nullptr;
    wxSpinCtrlDouble* m_latencyCompCtrl = nullptr;
    std::atomic<long long> m_latency_comp_us{0};
    
    // State
    bool m_is_dragging_slider = false;
    bool m_is_programmatic_selection = false;
    bool m_is_dragging_playlist = false;

    // AB Point Loop State
    double m_abPointA_ms = -1.0;       // A点位置（毫秒），-1表示未设置
    double m_abPointB_ms = -1.0;       // B点位置（毫秒），-1表示未设置
    bool m_abLoopEnabled = false;      // AB点循环是否启用
    long m_drag_source_view = -1;
    std::vector<wxString> m_playlist_files; // Stores full paths
    int m_current_play_index = -1;
    wxString m_play_mode = wxString::FromUTF8("单曲播放"); // "单曲播放", "单曲循环", "列表播放", "列表循环", "随机播放"
    bool m_decompose_chords = false;
    
    // Enhanced Random Playback Variables
    std::vector<int> m_shuffle_indices;
    size_t m_current_shuffle_index = 0;
    std::mt19937 m_random_engine;
    bool m_need_shuffle_reset = true;
    
    // Schedule
    bool m_is_scheduled = false;
    std::atomic<long long> m_schedule_target_epoch_us{0};
    std::atomic<unsigned long long> m_schedule_token{0};
    std::atomic<unsigned long long> m_active_schedule_token{0};
    
    // State Machine
    UI::PlaybackStateMachine m_stateMachine;
    std::unique_ptr<UI::PlaybackStateUpdater> m_stateUpdater;
    
    // Thread Management
    std::vector<std::future<void>> m_backgroundThreads;
    mutable std::mutex m_threadMutex;
    std::atomic<bool> m_isShuttingDown{false};
    
    wxDECLARE_EVENT_TABLE();
};
