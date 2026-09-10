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

#include "UIHelpers.h"
#include "KeymapEditorDialog.h"
#include "../core/PlaybackEngine.h"
#include "Widgets.h"
#include "PlaybackState.h"
#include "../util/PlaylistManager.h"

// Forward declaration
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
    
    ID_KEYMAP_CHOICE,
    ID_KEYMAP_EDITOR_BTN,   // 打开键位编辑弹窗(钢琴卷)
    ID_SCHEDULE_BTN,

    
    // Timer IDs
    ID_PLAYBACK_TIMER = 2001,
    ID_STATUS_TIMER,
    ID_NTP_TIMER,
    ID_SCHEDULE_TRIGGER,
    ID_HELP_SCROLL_TIMER,
    ID_CONFIG_SAVE_TIMER
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
    friend class MidiDropTarget;
    
private:
    // 配置写入类型（B1 去抖用）
    enum class ConfigSaveKind {
        File,      // 通道配置（SaveFileConfig）
        Global,    // 全局配置（SaveGlobalConfig）
    };

    // UI Initialization
    void InitUI();
    void InitPlaylistPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    void InitControlPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    void InitChannelPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    void InitKeymapPanel(wxPanel* parent, wxBoxSizer* mainSizer);
    
    wxPanel* CreateChannelConfig(wxPanel* parent, int index);

    // Event Handlers
    void OnClose(wxCloseEvent& event);
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
    void OnDecomposeClick(wxCommandEvent& event);
    
    void OnSliderTrack(wxCommandEvent& event);
    void OnSliderRelease(wxCommandEvent& event);
    void OnSliderChange(wxCommandEvent& event);

    // AB Point Event Handlers
    void OnABPointSetA(wxCommandEvent& event);
    void OnABPointSetB(wxCommandEvent& event);
    void OnABPointClear(wxCommandEvent& event);
    void OnABPointDrag(wxCommandEvent& event);
    
    void OnSpeedChange(wxSpinDoubleEvent& event);
    
    void OnKeymapChoice(wxCommandEvent& event);
    void OnOpenKeymapEditor(wxCommandEvent& event);
    void OnSchedule(wxCommandEvent& event);

    // Custom event handlers
    void OnNtpSyncComplete(wxCommandEvent& event);
    void OnScheduleTrigger(wxCommandEvent& event);
    void OnDPIChanged(wxDPIChangedEvent& event);
    
    // Global Hook
    void InstallGlobalHook();
    void UninstallGlobalHook();

    // Thread management
    void StartBackgroundTask(std::function<void()> task);
    void CleanupFinishedThreads();
    
    // Channel Events

    // Timer Events
    void OnTimer(wxTimerEvent& event);
    void OnStatusTimer(wxTimerEvent& event);
    void OnHelpScrollTimer(wxTimerEvent& event);

    // 配置写入去抖（B1）
    void RequestConfigSave(ConfigSaveKind kind);
    void OnConfigSaveTimer(wxTimerEvent& event);
    void FlushConfigSave();

    // Helpers
    void UpdateStatusText(const wxString& text);
    bool PlayIndex(int viewIndex, bool autoPlay = true, bool showDialog = true);
    bool SkipToNextValid(int startIndex, int direction, int maxRetries);
    void UpdateChannelUI(int channelIndex, bool enabled);
    void UpdateWindowList();
    void UpdateTrackList(); // Updates track choices in all channel configs
    void ImportFiles(const wxArrayString& paths);
    
    // Help text scrolling
    void StartHelpScroll();
    void StopHelpScroll();
    void InitHelpMessages();
    
    // 多播放列表辅助函数
    void RefreshPlaylistUI();                           // 刷新文件列表UI
    void UpdatePlaylistChoice();                        // 更新播放列表下拉框
    void SwitchToPlaylist(int index);                   // 切换播放列表
    
    // Enhanced Random Playback
    void InitializeRandomShuffle();
    int GetNextRandomIndex();
    void ResetRandomSequence();

    // Window Recovery
    void TryRecoverWindows();  // 定时扫描并恢复窗口选择
    int FindWindowByTitle(const wxString& title);  // 按标题查找窗口索引，返回 -1 表示未找到
    int FindWindowByTitleAndProcess(const wxString& title, const wxString& processName, long pid = 0);  // 按标题+进程名(+PID)查找窗口索引

    // State Machine Callbacks
    void OnStateChange(UI::PlaybackStatus oldState, UI::PlaybackStatus newState);

    // AI State


    // Config
    void LoadFileConfig(const wxString& filename);
    void SaveFileConfig();
    void LoadGlobalConfig();
    void SaveGlobalConfig();
    void SaveWindowGeometry();
    void LoadPlaylistConfig();
    void SavePlaylistConfig();
    void LoadKeymapConfig();
    void SaveKeymapConfig();
    void UpdateKeymapChoice();
    void LoadKeymapScheme(const wxString& name);   // 从 config 读方案 map 到 KeyManager
    long FindKeymapSchemeIndex(const wxString& name) const;  // 返回 -1 表示不存在
    bool ReadKeymapSchemeMap(const wxString& name, std::map<int, Util::KeyMapping>& out) const; // 从 config 读方案 map
    void load_builtin_preset(int idx);
    bool ReadSchemePitch(const wxString& name, int& minP, int& maxP) const;  // 读方案音域(空=FF14, @builtin_=燕云, 其他=自定义)
    void ApplySchemePitch(const wxString& name);   // 应用方案音域到引擎+UI
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
    wxButton* m_decomposeBtn;
    
    wxStaticText* m_currentTimeLabel;
    wxStaticText* m_totalTimeLabel;
    ModernSlider* m_progressSlider;
    
    wxSpinCtrlDouble* m_speedCtrl;
    ScrollingText* m_currentFileLabel;

    // UI Members - Channels
    std::vector<ChannelControls> m_channelConfigs;
    
    // UI Members - Keymap & NTP
    wxChoice* m_keymapChoice;
    wxButton* m_keymapEditorBtn = nullptr;   // 打开钢琴卷键位编辑器
    wxStaticText* m_ntpLabel;

    // 键位映射管理
    std::vector<wxString> m_keymapFiles;  // 自定义键位方案名列表(数据存 config /KeymapSchemes)
    wxString m_currentKeymapPath;          // 当前方案标识（空=内置FF14, @builtin_N=内置预设, 或方案名）
    int m_minPitch = 48;                   // 当前方案音域(跟方案走)
    int m_maxPitch = 84;
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
    wxTimer m_helpScrollTimer;
    wxTimer m_configSaveTimer;
    bool m_configSavePending = false;
    ConfigSaveKind m_configSaveKind = ConfigSaveKind::File;
    std::vector<wxString> m_helpMessages;
    size_t m_helpMessageIndex = 0;
    bool m_helpScrollActive = false;
    

    
    // Latency compensation control
    wxSpinCtrl* m_latencyCompCtrl = nullptr;
    std::atomic<long long> m_latency_comp_us{0};
    
    // State
    bool m_is_dragging_slider = false;
    bool m_is_programmatic_selection = false;
    bool m_is_dragging_playlist = false;
    wxString m_lastHoverTooltip;  // 上次悬停的完整路径，避免重复设置 tooltip

    // AB Point Loop State
    double m_abPointA_ms = -1.0;       // A点位置（毫秒），-1表示未设置
    double m_abPointB_ms = -1.0;       // B点位置（毫秒），-1表示未设置
    bool m_abLoopEnabled = false;      // AB点循环是否启用
    long m_drag_source_view = -1;
    std::vector<wxString> m_playlist_files; // Stores full paths
    int m_current_play_index = -1;
    wxString m_play_mode = UIConstants::MODE_SINGLE;
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
