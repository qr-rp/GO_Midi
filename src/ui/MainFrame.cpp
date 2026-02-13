#include <wx/tglbtn.h>
#include "MainFrame.h"
#include "UIHelpers.h"
#include "../util/Logger.h"
#include "../util/NtpClient.h"

#include <wx/filedlg.h>
#include <wx/aboutdlg.h>
#include <thread>
#include <random>
#include <sstream>
#include <wx/msgdlg.h>
#include <wx/wfstream.h>
#include <wx/artprov.h>
#include <wx/statline.h>
#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <map>
#include <cmath>
#include "../util/KeyManager.h"
#include "../core/KeyboardSimulator.h"

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_BUTTON(ID_IMPORT_BTN, MainFrame::OnImportFile)
    EVT_BUTTON(ID_REMOVE_BTN, MainFrame::OnRemoveFile)
    EVT_BUTTON(ID_CLEAR_BTN, MainFrame::OnClearList)
    EVT_TEXT(ID_SEARCH_CTRL, MainFrame::OnSearch)
    EVT_LIST_ITEM_SELECTED(ID_PLAYLIST_CTRL, MainFrame::OnPlaylistSelected)
    EVT_LIST_ITEM_ACTIVATED(ID_PLAYLIST_CTRL, MainFrame::OnPlaylistActivated)
    EVT_LIST_BEGIN_DRAG(ID_PLAYLIST_CTRL, MainFrame::OnPlaylistBeginDrag)
    
    // 多播放列表事件
    EVT_CHOICE(ID_PLAYLIST_CHOICE, MainFrame::OnPlaylistChoice)
    EVT_BUTTON(ID_ADD_PLAYLIST_BTN, MainFrame::OnAddPlaylist)
    EVT_BUTTON(ID_DELETE_PLAYLIST_BTN, MainFrame::OnDeletePlaylist)
    EVT_BUTTON(ID_RENAME_PLAYLIST_BTN, MainFrame::OnRenamePlaylist)
    
    EVT_BUTTON(ID_PREV_BTN, MainFrame::OnPrev)
    EVT_BUTTON(ID_PLAY_BTN, MainFrame::OnPlay)
    EVT_BUTTON(ID_STOP_BTN, MainFrame::OnStop)
    EVT_BUTTON(ID_NEXT_BTN, MainFrame::OnNext)
    EVT_BUTTON(ID_MODE_BTN, MainFrame::OnModeClick)
    EVT_TOGGLEBUTTON(ID_DECOMPOSE_BTN, MainFrame::OnDecomposeToggle)
    
    // Slider events
    EVT_MODERN_SLIDER_THUMBTRACK(ID_PROGRESS_SLIDER, MainFrame::OnSliderTrack)
    EVT_MODERN_SLIDER_THUMBRELEASE(ID_PROGRESS_SLIDER, MainFrame::OnSliderRelease)
    EVT_MODERN_SLIDER_CHANGE(ID_PROGRESS_SLIDER, MainFrame::OnSliderChange)
    
    EVT_SPINCTRLDOUBLE(ID_SPEED_CTRL, MainFrame::OnSpeedChange)
    EVT_SPINCTRL(ID_MIN_PITCH_CTRL, MainFrame::OnPitchRangeChange)
    EVT_SPINCTRL(ID_MAX_PITCH_CTRL, MainFrame::OnPitchRangeChange)
    
    EVT_BUTTON(ID_LOAD_KEYMAP_BTN, MainFrame::OnLoadKeymap)
    EVT_BUTTON(ID_SAVE_KEYMAP_BTN, MainFrame::OnSaveKeymap)
    EVT_BUTTON(ID_RESET_KEYMAP_BTN, MainFrame::OnResetKeymap)
    EVT_BUTTON(ID_SCHEDULE_BTN, MainFrame::OnSchedule)
    
    // Custom events
    EVT_COMMAND(ID_NTP_TIMER, wxEVT_COMMAND_BUTTON_CLICKED, MainFrame::OnNtpSyncComplete)
    EVT_COMMAND(ID_SCHEDULE_TRIGGER, wxEVT_COMMAND_BUTTON_CLICKED, MainFrame::OnScheduleTrigger)
    
    EVT_TIMER(ID_PLAYBACK_TIMER, MainFrame::OnTimer)
    EVT_TIMER(ID_STATUS_TIMER, MainFrame::OnStatusTimer)
wxEND_EVENT_TABLE()

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "GO_Midi!", wxDefaultPosition, wxSize(500, 650), wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX))
{
    wxIcon icon("APP_ICON", wxBITMAP_TYPE_ICO_RESOURCE);
    if (icon.IsOk()) {
        SetIcon(icon);
    }

    // Initialize Config
    wxFileName exePath(wxStandardPaths::Get().GetExecutablePath());
    wxString configPath = wxFileName(exePath.GetPath(), "config.ini").GetFullPath();
    m_config = std::make_unique<wxFileConfig>("wx_GO_MIDI", "wx_GO_MIDI", configPath, "", wxCONFIG_USE_LOCAL_FILE);

    // Set system default font for modern look
    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    font.SetPointSize(9);
    font.SetFaceName("Microsoft YaHei UI");
    SetFont(font);

    InitUI();

    LoadGlobalConfig();
    LoadPlaylistConfig();
    LoadKeymapConfig();
    
    // 加载最后选中的文件（在播放列表加载之后）
    LoadLastSelectedFile();

    // Timer for UI updates
    m_timer.SetOwner(this, ID_PLAYBACK_TIMER);
    m_timer.Start(100);
    
    m_statusTimer.SetOwner(this, ID_STATUS_TIMER);

    Util::NtpClient::StartAutoSync();
    UpdateStatusText(wxString::FromUTF8("时间同步中..."));
    
    // Initialize State Machine
    m_stateMachine.SetStateChangeCallback([this](UI::PlaybackStatus oldState, UI::PlaybackStatus newState) {
        OnStateChange(oldState, newState);
    });
    
    // Initialize State Updater
    UI::PlaybackStateUpdater::UIComponents uiComponents;
    uiComponents.playBtn = m_playBtn;
    uiComponents.statusBar = GetStatusBar();
    uiComponents.currentFileLabel = m_currentFileLabel;
    uiComponents.currentTimeLabel = m_currentTimeLabel;
    uiComponents.totalTimeLabel = m_totalTimeLabel;
    uiComponents.progressSlider = m_progressSlider;
    m_stateUpdater = std::make_unique<UI::PlaybackStateUpdater>(m_stateMachine, uiComponents);
    
    // Initialize enhanced random playback
    m_random_engine.seed(std::random_device{}());
    m_need_shuffle_reset = true;
    
    m_stateMachine.TransitionTo(UI::PlaybackStatus::Idle);

    // Initialize Global Hook
    InstallGlobalHook();
}

MainFrame::~MainFrame() {
    // Unregister Global Hook
    UninstallGlobalHook();

    // 设置关闭标志
    m_isShuttingDown = true;

    // 强制关闭NTP客户端以确保快速关闭
    Util::NtpClient::ForceShutdown();
    
    // 保存最后选中的文件
    SaveLastSelectedFile();
    
    // 停止定时器
    m_timer.Stop();
    m_statusTimer.Stop();
    
    // 停止播放引擎
    m_engine.stop();
    
    // 等待所有后台线程完成
    std::vector<std::future<void>> pending;
    {
        std::lock_guard<std::mutex> lock(m_threadMutex);
        pending = std::move(m_backgroundThreads);
    }
    for (auto& fut : pending) {
        if (fut.valid()) {
            fut.wait();
        }
    }
}

void MainFrame::InitUI() {
    // Main Panel
    wxPanel* mainPanel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. Playlist Panel
    InitPlaylistPanel(mainPanel, mainSizer);

    // 2. Control Panel
    InitControlPanel(mainPanel, mainSizer);

    // 3. Channel Panel
    InitChannelPanel(mainPanel, mainSizer);

    // 4. Keymap Panel
    InitKeymapPanel(mainPanel, mainSizer);

    mainPanel->SetSizer(mainSizer);
    
    // Status Bar
    wxStatusBar* statusBar = CreateStatusBar(3);
    UpdateStatusText(wxString::FromUTF8("By:最终幻想14水晶世界_黄金谷_吸溜"));
    statusBar->SetStatusText("BPM: --", 2);
    
    m_latencyCompLabel = new wxStaticText(statusBar, wxID_ANY, wxString::FromUTF8("延迟补偿:"));
    m_latencyCompCtrl = new wxSpinCtrlDouble(statusBar, wxID_ANY, "0.0", wxDefaultPosition, wxDefaultSize,
                                             wxSP_ARROW_KEYS | wxTE_CENTRE, -9999.0, 9999.0, 0.0, 1.0);
    m_latencyCompCtrl->SetDigits(1);
    m_latencyCompCtrl->SetToolTip(wxString::FromUTF8("正值推迟发送(增加延迟)，负值提前发送(抵消Ping)"));
    m_latencyCompCtrl->Bind(wxEVT_SPINCTRLDOUBLE, &MainFrame::OnLatencyCompSpin, this);
    m_latencyCompCtrl->Bind(wxEVT_TEXT, &MainFrame::OnLatencyCompText, this);

    // 重新设计状态栏：第一字段自动调整，第二三字段紧靠右边
    // 第二字段固定90px用于延迟补偿控件，第三字段固定75px用于BPM显示
    const int LATENCY_FIELD_WIDTH = 90;   // 延迟补偿字段宽度
    const int BPM_FIELD_WIDTH = 75;       // BPM字段宽度
    
    m_latencyCompCtrl->SetMinSize(wxSize(70, -1));   // 控件本身70px以适应90px字段
    m_latencyCompCtrl->SetSize(wxSize(70, -1));
    
    // 设置状态栏字段宽度：第一字段自动(-1)，第二三字段固定
    int widths[] = {-1, LATENCY_FIELD_WIDTH, BPM_FIELD_WIDTH};
    statusBar->SetStatusWidths(3, widths);

    LayoutStatusBarControls();
    statusBar->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        LayoutStatusBarControls();
        evt.Skip();
    });
    
    // Initial window list update
    UpdateWindowList();
}

void MainFrame::StartBackgroundTask(std::function<void()> task) {
    if (m_isShuttingDown.load()) {
        return; // 应用正在关闭，不启动新任务
    }
    
    std::lock_guard<std::mutex> lock(m_threadMutex);
    CleanupFinishedThreads();
    m_backgroundThreads.emplace_back(std::async(std::launch::async, [task]() {
        try {
            task();
        } catch (const std::exception& e) {
            LOG("Background thread exception: " + std::string(e.what()));
        } catch (...) {
            LOG("Unknown background thread exception");
        }
    }));
}

void MainFrame::CleanupFinishedThreads() {
    m_backgroundThreads.erase(
        std::remove_if(m_backgroundThreads.begin(), m_backgroundThreads.end(),
                      [](std::future<void>& fut) {
                          if (!fut.valid()) {
                              return true;
                          }
                          return fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
                      }),
        m_backgroundThreads.end()
    );
}

/**
 * @brief 重新布局状态栏内嵌入的控件（延迟补偿输入框）。
 */
void MainFrame::LayoutStatusBarControls() {
    if (!m_latencyCompCtrl || !m_latencyCompLabel) {
        return;
    }

    wxStatusBar* statusBar = GetStatusBar();
    if (!statusBar) {
        return;
    }

    // 获取状态栏字段矩形
    wxRect rect;
    if (!statusBar->GetFieldRect(1, rect)) {
        return;
    }

    // 右对齐布局：控件紧靠字段右边界
    const int padY = 1;      // 垂直padding
    const int rightPad = 2;  // 右侧padding
    const int gap = 6;       // 标签和控件间间隙
    
    // 获取标签和控件尺寸
    const wxSize labelSize = m_latencyCompLabel->GetBestSize();
    const int ctrlW = m_latencyCompCtrl->GetMinSize().GetWidth();
    
    // 从右侧开始定位控件
    const int fieldRight = rect.GetX() + rect.GetWidth();
    const int ctrlX = fieldRight - ctrlW - rightPad;
    const int labelX = ctrlX - gap - labelSize.GetWidth();
    
    // 垂直居中定位
    const int centerY = rect.GetY() + rect.GetHeight() / 2;
    const int labelY = centerY - labelSize.GetHeight() / 2;
    const int ctrlY = centerY - m_latencyCompCtrl->GetSize().GetHeight() / 2;
    
    // 设置控件位置和大小
    m_latencyCompLabel->SetSize(labelX, labelY, labelSize.GetWidth(), labelSize.GetHeight());
    m_latencyCompCtrl->SetSize(ctrlX, ctrlY, ctrlW, m_latencyCompCtrl->GetSize().GetHeight());
}

/**
 * @brief 处理状态栏延迟补偿输入框数值变化（微调箭头）。
 */
void MainFrame::OnLatencyCompSpin(wxSpinDoubleEvent& event) {
    m_latency_comp_us.store((long long)std::llround(event.GetValue() * 1000.0));
    event.Skip();
}

/**
 * @brief 处理状态栏延迟补偿输入框文本变化（输入时实时生效）。
 */
void MainFrame::OnLatencyCompText(wxCommandEvent& event) {
    if (m_latencyCompCtrl) {
        m_latency_comp_us.store((long long)std::llround(m_latencyCompCtrl->GetValue() * 1000.0));
    }
    event.Skip();
}

void MainFrame::InitPlaylistPanel(wxPanel* parent, wxBoxSizer* mainSizer) {
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // 播放列表选择器行
    wxBoxSizer* playlistSizer = new wxBoxSizer(wxHORIZONTAL);
    
    wxStaticText* playlistLabel = new wxStaticText(panel, wxID_ANY, wxString::FromUTF8("播放列表:"));
    m_playlistChoice = new wxChoice(panel, ID_PLAYLIST_CHOICE);
    m_playlistChoice->SetMinSize(wxSize(120, -1));
    
    m_addPlaylistBtn = new wxButton(panel, ID_ADD_PLAYLIST_BTN, wxString::FromUTF8("新建"));
    m_addPlaylistBtn->SetMinSize(wxSize(45, -1));
    m_deletePlaylistBtn = new wxButton(panel, ID_DELETE_PLAYLIST_BTN, wxString::FromUTF8("删除"));
    m_deletePlaylistBtn->SetMinSize(wxSize(45, -1));
    m_renamePlaylistBtn = new wxButton(panel, ID_RENAME_PLAYLIST_BTN, wxString::FromUTF8("重命名"));
    m_renamePlaylistBtn->SetMinSize(wxSize(55, -1));
    
    playlistSizer->Add(playlistLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    playlistSizer->Add(m_playlistChoice, 1, wxALL | wxEXPAND, 2);
    playlistSizer->Add(m_addPlaylistBtn, 0, wxALL, 2);
    playlistSizer->Add(m_deletePlaylistBtn, 0, wxALL, 2);
    playlistSizer->Add(m_renamePlaylistBtn, 0, wxALL, 2);
    
    sizer->Add(playlistSizer, 0, wxEXPAND | wxALL, 2);
    
    // Toolbar
    wxBoxSizer* toolbarSizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_importBtn = new wxButton(panel, ID_IMPORT_BTN, wxString::FromUTF8("导入文件"));
    m_removeBtn = new wxButton(panel, ID_REMOVE_BTN, wxString::FromUTF8("移除选中"));
    m_clearBtn = new wxButton(panel, ID_CLEAR_BTN, wxString::FromUTF8("清空列表"));
    m_searchCtrl = new wxTextCtrl(panel, ID_SEARCH_CTRL, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_searchCtrl->SetHint(wxString::FromUTF8("搜索..."));
    m_searchCtrl->SetMinSize(wxSize(-1, 26));
    
    toolbarSizer->Add(m_importBtn, 0, wxALL, 2);
    toolbarSizer->Add(m_removeBtn, 0, wxALL, 2);
    toolbarSizer->Add(m_clearBtn, 0, wxALL, 2);
    toolbarSizer->Add(m_searchCtrl, 1, wxALL | wxEXPAND, 2);
    
    sizer->Add(toolbarSizer, 0, wxEXPAND | wxALL, 2);
    
    // List Control
    // Hide header and allow auto-resize
    m_playlistCtrl = new wxListView(panel, ID_PLAYLIST_CTRL, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    m_playlistCtrl->InsertColumn(0, wxString::FromUTF8("文件名"), wxLIST_FORMAT_LEFT);
    
    // Bind size event to auto-resize column
    m_playlistCtrl->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        if (m_playlistCtrl && m_playlistCtrl->GetColumnCount() > 0) {
            // Use GetClientSize to account for borders and scrollbars correctly
            int width = m_playlistCtrl->GetClientSize().GetWidth();
            // Subtract a small padding to ensure no horizontal scrollbar appears
            // This forces the column to be slightly smaller than the view, triggering native ellipsis
            m_playlistCtrl->SetColumnWidth(0, width - 2);
        }
        event.Skip();
    });

    m_playlistCtrl->Bind(wxEVT_LEFT_UP, &MainFrame::OnPlaylistEndDrag, this);
    
    // Adjust font size
    wxFont font = m_playlistCtrl->GetFont();
    font.SetPointSize(10);
    m_playlistCtrl->SetFont(font);
    
    m_playlistCtrl->SetMinSize(wxSize(-1, 145)); // 略微减小高度以容纳播放列表选择器
    
    sizer->Add(m_playlistCtrl, 1, wxALL | wxEXPAND, 2);
    
    panel->SetSizer(sizer);
    mainSizer->Add(panel, 1, wxEXPAND | wxALL, 2);
}

void MainFrame::InitControlPanel(wxPanel* parent, wxBoxSizer* mainSizer) {
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Buttons Row
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_prevBtn = new wxButton(panel, ID_PREV_BTN, wxString::FromUTF8("上一曲"));
    m_playBtn = new wxButton(panel, ID_PLAY_BTN, wxString::FromUTF8("播放"));
    m_stopBtn = new wxButton(panel, ID_STOP_BTN, wxString::FromUTF8("停止"));
    m_nextBtn = new wxButton(panel, ID_NEXT_BTN, wxString::FromUTF8("下一曲"));
    m_modeBtn = new wxButton(panel, ID_MODE_BTN, wxString::FromUTF8("单曲播放"));
    m_decomposeBtn = new wxToggleButton(panel, ID_DECOMPOSE_BTN, wxString::FromUTF8("单音模式"));
    m_decomposeBtn->SetMinSize(wxSize(80, 25));
    
    btnSizer->Add(m_prevBtn, 0, wxALL, 2);
    btnSizer->Add(m_playBtn, 0, wxALL, 2);
    btnSizer->Add(m_stopBtn, 0, wxALL, 2);
    btnSizer->Add(m_nextBtn, 0, wxALL, 2);
    btnSizer->Add(m_modeBtn, 0, wxALL, 2);
    btnSizer->Add(m_decomposeBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    sizer->Add(btnSizer, 0, wxEXPAND | wxALL, 2);
    
    // Progress Bar
    wxBoxSizer* progressSizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_currentTimeLabel = new wxStaticText(panel, wxID_ANY, "00:00");
    m_totalTimeLabel = new wxStaticText(panel, wxID_ANY, "00:00");
    m_progressSlider = new ModernSlider(panel, ID_PROGRESS_SLIDER, 0, 0, 1000);
    
    progressSizer->Add(m_currentTimeLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    progressSizer->Add(m_progressSlider, 1, wxALL | wxEXPAND, 2);
    progressSizer->Add(m_totalTimeLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    sizer->Add(progressSizer, 0, wxEXPAND | wxALL, 2);
    
    // Config Area
    wxBoxSizer* configSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // Speed
    wxPanel* speedPanel = new wxPanel(panel);
    wxBoxSizer* speedSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* speedLabel = new wxStaticText(speedPanel, wxID_ANY, wxString::FromUTF8("倍速:"));
    
    // Use SpinCtrlDouble for unlimited speed adjustment
    m_speedCtrl = new wxSpinCtrlDouble(speedPanel, ID_SPEED_CTRL, "1.0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0.1, 100.0, 1.0, 0.1);
    m_speedCtrl->SetDigits(2);
    m_speedCtrl->SetMinSize(wxSize(60, 22));
    
    speedSizer->Add(speedLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    speedSizer->Add(m_speedCtrl, 0, wxALL, 2);
    speedPanel->SetSizer(speedSizer);
    
    configSizer->Add(speedPanel, 0, wxALL, 2);
    
    // Pitch Range
    wxPanel* rangePanel = new wxPanel(panel);
    wxBoxSizer* rangeSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* rangeLabel = new wxStaticText(rangePanel, wxID_ANY, wxString::FromUTF8("目标音域:"));
    
    m_minPitchCtrl = new wxSpinCtrl(rangePanel, ID_MIN_PITCH_CTRL, "48", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS | wxTE_CENTRE, 0, 127, 48);
    m_maxPitchCtrl = new wxSpinCtrl(rangePanel, ID_MAX_PITCH_CTRL, "84", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS | wxTE_CENTRE, 0, 127, 84);
    
    m_minPitchCtrl->SetMinSize(wxSize(50, 20));
    m_maxPitchCtrl->SetMinSize(wxSize(50, 20));
    
    rangeSizer->Add(rangeLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    rangeSizer->Add(m_minPitchCtrl, 0, wxALL, 2);
    rangeSizer->Add(new wxStaticText(rangePanel, wxID_ANY, "-"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    rangeSizer->Add(m_maxPitchCtrl, 0, wxALL, 2);
    rangePanel->SetSizer(rangeSizer);
    
    configSizer->Add(rangePanel, 0, wxALL, 2);
    
    // Current File Label
    wxPanel* filePanel = new wxPanel(panel);
    wxBoxSizer* fileSizer = new wxBoxSizer(wxHORIZONTAL);
    m_currentFileLabel = new ScrollingText(filePanel, wxID_ANY, wxString::FromUTF8("未选择文件"));
    fileSizer->Add(m_currentFileLabel, 1, wxALL | wxEXPAND, 2);
    filePanel->SetSizer(fileSizer);
    
    configSizer->Add(filePanel, 1, wxALL | wxEXPAND, 2);
    
    sizer->Add(configSizer, 0, wxEXPAND | wxALL, 2);
    
    panel->SetSizer(sizer);
    mainSizer->Add(panel, 0, wxEXPAND | wxALL, 2);
}

void MainFrame::InitChannelPanel(wxPanel* parent, wxBoxSizer* mainSizer) {
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    wxGridSizer* gridSizer = new wxGridSizer(4, 2, 2, 2);
    
    m_channelConfigs.clear();
    for (int i = 0; i < 8; ++i) {
        wxPanel* p = CreateChannelConfig(panel, i);
        gridSizer->Add(p, 0, wxEXPAND);
    }
    
    sizer->Add(gridSizer, 1, wxEXPAND | wxALL, 2);
    panel->SetSizer(sizer);
    mainSizer->Add(panel, 1, wxEXPAND | wxALL, 2);
}

wxPanel* MainFrame::CreateChannelConfig(wxPanel* parent, int index) {
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Row 1: Enable + Window
    wxBoxSizer* row1 = new wxBoxSizer(wxHORIZONTAL);
    
    wxString label = wxString::Format(wxString::FromUTF8("通道 %d"), index + 1);
    wxToggleButton* enableBtn = new wxToggleButton(panel, wxID_ANY, label);
    enableBtn->SetMinSize(wxSize(50, -1));
    
    wxChoice* windowChoice = new wxChoice(panel, wxID_ANY);
    windowChoice->Append(wxString::FromUTF8("未选择"));
    windowChoice->SetSelection(0);
    
    row1->Add(enableBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    row1->Add(windowChoice, 1, wxALL | wxEXPAND, 2);
    
    sizer->Add(row1, 0, wxEXPAND, 2);
    
    // Row 2: Transpose + Track
    wxBoxSizer* row2 = new wxBoxSizer(wxHORIZONTAL);
    
    wxSpinCtrl* transposeCtrl = new wxSpinCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS | wxTE_CENTRE, -24, 24, 0);
    transposeCtrl->SetMinSize(wxSize(50, -1));
    

    
    wxChoice* trackChoice = new wxChoice(panel, wxID_ANY);
    trackChoice->Append(wxString::FromUTF8("全部音轨"));
    trackChoice->SetSelection(0);
    
    row2->Add(transposeCtrl, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);

    row2->Add(trackChoice, 1, wxALL | wxEXPAND, 2);
    
    sizer->Add(row2, 0, wxEXPAND, 2);
    
    panel->SetSizer(sizer);
    
    // Store controls
    ChannelControls controls = { enableBtn, windowChoice, transposeCtrl, trackChoice, index };
    m_channelConfigs.push_back(controls);
    
    // Bind events using lambdas to capture index
    enableBtn->Bind(wxEVT_TOGGLEBUTTON, [this, index](wxCommandEvent& e) {
        // Toggle UI state
        bool enabled = e.IsChecked();
        UpdateChannelUI(index, enabled);
        m_engine.set_channel_enable(index, enabled);
        SaveFileConfig();
    });


    
    windowChoice->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
         OnWindowChoiceDropdown(e);
         e.Skip(); // Allow default processing
    });
    
    windowChoice->Bind(wxEVT_CHOICE, [this, index](wxCommandEvent& e) {
        int sel = e.GetSelection();
        if (sel != wxNOT_FOUND && sel != 0) {
            wxChoice* c = (wxChoice*)e.GetEventObject();
            void* clientData = c->GetClientData(sel);
            if (clientData != nullptr) {
                m_engine.set_channel_window(index, static_cast<void*>(clientData));
            } else {
                m_engine.set_channel_window(index, nullptr);
            }
        } else {
            m_engine.set_channel_window(index, nullptr);
        }
        SaveFileConfig();
    });
    
    transposeCtrl->Bind(wxEVT_SPINCTRL, [this, index, transposeCtrl](wxSpinEvent& e) {
        int val = e.GetValue();
        m_engine.set_channel_transpose(index, val);
        SaveFileConfig();
        if (val > 0) {
            transposeCtrl->SetValue(wxString::Format("+%d", val));
        }
    });
    
    transposeCtrl->Bind(wxEVT_TEXT_ENTER, [this, index, transposeCtrl](wxCommandEvent& e) {
        int val = transposeCtrl->GetValue();
        m_engine.set_channel_transpose(index, val);
        SaveFileConfig();
        if (val > 0) {
            transposeCtrl->SetValue(wxString::Format("+%d", val));
        }
    });
    
    trackChoice->Bind(wxEVT_CHOICE, [this, index](wxCommandEvent& e) {
        // Update engine mapping immediately
        int sel = e.GetSelection();
        if (sel != wxNOT_FOUND) {
             wxChoice* c = (wxChoice*)e.GetEventObject();
             void* clientData = c->GetClientData(sel);
             if (clientData != nullptr) {
                 int trackIdx = static_cast<int>(reinterpret_cast<intptr_t>(clientData));
                 m_engine.set_channel_track(index, trackIdx);
             }
        }
        SaveFileConfig();
    });
    
    // Initialize state
    bool initialEnable = (index == 0);
    enableBtn->SetValue(initialEnable);
    UpdateChannelUI(index, initialEnable);
    
    return panel;
}

void MainFrame::UpdateChannelUI(int index, bool enabled) {
    if (index >= 0 && index < m_channelConfigs.size()) {
        ChannelControls& c = m_channelConfigs[index];
        c.windowChoice->Enable(enabled);
        c.transposeCtrl->Enable(enabled);
        c.trackChoice->Enable(enabled);
    }
}

void MainFrame::InitKeymapPanel(wxPanel* parent, wxBoxSizer* mainSizer) {
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_loadKeymapBtn = new wxButton(panel, ID_LOAD_KEYMAP_BTN, wxString::FromUTF8("加载键位"));
    m_saveKeymapBtn = new wxButton(panel, ID_SAVE_KEYMAP_BTN, wxString::FromUTF8("保存键位"));
    m_resetKeymapBtn = new wxButton(panel, ID_RESET_KEYMAP_BTN, wxString::FromUTF8("重置键位"));
    
    sizer->Add(m_loadKeymapBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    sizer->Add(m_saveKeymapBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    sizer->Add(m_resetKeymapBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    sizer->AddStretchSpacer();
    
    // NTP Area
    wxBoxSizer* ntpSizer = new wxBoxSizer(wxHORIZONTAL);
    
    ntpSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxSize(2, 20), wxLI_VERTICAL), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 2);
    
    m_ntpLabel = new wxStaticText(panel, wxID_ANY, "--:--", wxDefaultPosition, wxSize(45, -1), wxALIGN_CENTER);
    ntpSizer->Add(m_ntpLabel, 0, wxALIGN_CENTER_VERTICAL, 0);
    
    ntpSizer->Add(new wxStaticLine(panel, wxID_ANY, wxDefaultPosition, wxSize(2, 20), wxLI_VERTICAL), 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 2);
    
    ntpSizer->Add(new wxStaticText(panel, wxID_ANY, wxString::FromUTF8("定时:")), 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    m_schedMin = new wxSpinCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxSize(45, -1), wxSP_ARROW_KEYS | wxTE_CENTRE, 0, 59, 0);
    ntpSizer->Add(m_schedMin, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    ntpSizer->Add(new wxStaticText(panel, wxID_ANY, ":"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);
    
    m_schedSec = new wxSpinCtrl(panel, wxID_ANY, "0", wxDefaultPosition, wxSize(45, -1), wxSP_ARROW_KEYS | wxTE_CENTRE, 0, 59, 0);
    ntpSizer->Add(m_schedSec, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    m_scheduleBtn = new wxButton(panel, ID_SCHEDULE_BTN, wxString::FromUTF8("定时"));
    ntpSizer->Add(m_scheduleBtn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    sizer->Add(ntpSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    
    panel->SetSizer(sizer);
    mainSizer->Add(panel, 0, wxEXPAND | wxALL, 2);
}

// ================= Event Stubs =================

void MainFrame::OnImportFile(wxCommandEvent& event) {
    if (!m_playlistCtrl) return;
    
    wxFileDialog openFileDialog(this, wxString::FromUTF8("选择MIDI文件"), "", "",
                                wxString::FromUTF8("MIDI文件 (*.mid;*.midi)|*.mid;*.midi|所有文件 (*.*)|*.*"),
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

    if (openFileDialog.ShowModal() == wxID_CANCEL)
        return;

    wxArrayString paths;
    openFileDialog.GetPaths(paths);
    
    wxString keyword = m_searchCtrl->GetValue().Lower();
    bool hasSearch = !keyword.IsEmpty();
    bool added = false;
    
    m_playlistCtrl->Freeze();

    for (const auto& path : paths) {
        // 使用 PlaylistManager 添加文件
        if (m_playlistManager.AddFile(path)) {
            // 同步更新本地缓存
            m_playlist_files.push_back(path);
            long newModelIndex = static_cast<long>(m_playlist_files.size() - 1);
            
            // Incremental Update: Add to view if matches search (or no search)
            wxString name = path.AfterLast('\\');
            if (!hasSearch || name.Lower().Contains(keyword)) {
                long viewIdx = m_playlistCtrl->GetItemCount();
                m_playlistCtrl->InsertItem(viewIdx, name);
                m_playlistCtrl->SetItemData(viewIdx, newModelIndex);
            }
            added = true;
        }
    }
    
    m_playlistCtrl->Thaw();

    if (added) {
        // No full refresh needed
        SavePlaylistConfig();
    }
}

void MainFrame::OnRemoveFile(wxCommandEvent& event) {
    if (!m_playlistCtrl) return;
    long viewIndex = m_playlistCtrl->GetFirstSelected();
    if (viewIndex == -1) return;

    long modelIndex = m_playlistCtrl->GetItemData(viewIndex);
    if (modelIndex < 0 || modelIndex >= static_cast<long>(m_playlist_files.size())) return;

    wxString removedPath = m_playlist_files[modelIndex];
    
    // 使用 PlaylistManager 移除文件
    m_playlistManager.RemoveFile(static_cast<size_t>(modelIndex));
    
    // 同步更新本地缓存
    m_playlist_files.erase(m_playlist_files.begin() + modelIndex);

    // Remove from view
    m_playlistCtrl->DeleteItem(viewIndex);
    
    // Incremental Update: Fix indices for remaining items
    // Any item pointing to a model index > modelIndex must be decremented
    long count = m_playlistCtrl->GetItemCount();
    for (long i = 0; i < count; ++i) {
        long d = m_playlistCtrl->GetItemData(i);
        if (d > modelIndex) {
            m_playlistCtrl->SetItemData(i, d - 1);
        }
    }

    bool removedCurrent = (removedPath == m_current_path);
    if (removedCurrent) {
        m_engine.stop();
        m_current_path = "";
        m_current_midi.reset();
        
        m_playBtn->SetLabel(wxString::FromUTF8("播放"));
        GetStatusBar()->SetStatusText("BPM: --", 2);
        m_currentFileLabel->SetLabel(wxString::FromUTF8("未选择文件"));
        m_totalTimeLabel->SetLabel("00:00");
        m_currentTimeLabel->SetLabel("00:00");
        m_progressSlider->SetValue(0);

        if (count > 0) {
            int newViewIndex = static_cast<int>(viewIndex);
            if (newViewIndex >= count) newViewIndex = count - 1;
            
            m_is_programmatic_selection = true;
            m_playlistCtrl->SetItemState(newViewIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            m_playlistCtrl->EnsureVisible(newViewIndex);
            m_is_programmatic_selection = false;
            PlayIndex(newViewIndex, false);
        } else {
            m_current_play_index = -1;
        }
    } else {
        // If we removed a file above the current one in the view, adjust current play index
        if (m_current_play_index > viewIndex) {
            m_current_play_index--;
        }
    }

    SavePlaylistConfig();
}

void MainFrame::OnClearList(wxCommandEvent& event) {
    if (!m_playlistCtrl) return;
    
    if (m_engine.is_playing()) {
        wxCommandEvent dummy;
        OnStop(dummy);
    }

    m_playlistCtrl->DeleteAllItems();
    
    // 使用 PlaylistManager 清空文件
    m_playlistManager.ClearFiles();
    m_playlist_files.clear();
    
    m_current_path = "";
    m_current_midi.reset();
    
    GetStatusBar()->SetStatusText("BPM: --", 2);
    m_currentFileLabel->SetLabel(wxString::FromUTF8("未选择文件"));
    m_totalTimeLabel->SetLabel("00:00");
    m_currentTimeLabel->SetLabel("00:00");
    m_progressSlider->SetValue(0);
    
    SavePlaylistConfig();

    // Also clear file-specific configs since list is empty
    m_config->SetPath("/");
    m_config->DeleteGroup("Files");
    m_config->Flush();
}

void MainFrame::OnSearch(wxCommandEvent& event) {
    if (!m_playlistCtrl) return;
    
    wxString keyword = m_searchCtrl->GetValue().Lower();
    m_playlistCtrl->DeleteAllItems();
    
    long idx = 0;
    for (size_t i = 0; i < m_playlist_files.size(); ++i) {
        wxString path = m_playlist_files[i];
        wxString name = path.AfterLast('\\');
        
        if (keyword.IsEmpty() || name.Lower().Contains(keyword)) {
            m_playlistCtrl->InsertItem(idx, name);
            m_playlistCtrl->SetItemData(idx, i);
            
            // Highlight if currently playing
            // We need to track current playing file by path
            if (path == m_current_path) {
                m_playlistCtrl->SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                m_current_play_index = idx; // Update view index
            }
            
            idx++;
        }
    }
}

void MainFrame::OnPlaylistSelected(wxListEvent& event) {
    if (!m_playlistCtrl) return;
    if (m_is_programmatic_selection) return;

    long viewIndex = event.GetIndex();
    if (viewIndex < 0 || viewIndex >= m_playlistCtrl->GetItemCount()) return;
    if (m_engine.is_playing()) {
        m_current_play_index = static_cast<int>(viewIndex);
        return;
    }
    PlayIndex(static_cast<int>(viewIndex), false);
}

void MainFrame::OnPlaylistActivated(wxListEvent& event) {
    if (!m_playlistCtrl) return;
    long viewIndex = event.GetIndex();
    PlayIndex(viewIndex);
}

void MainFrame::OnPlaylistBeginDrag(wxListEvent& event) {
    if (!m_playlistCtrl) return;
    long viewIndex = event.GetIndex();
    if (viewIndex < 0 || viewIndex >= m_playlistCtrl->GetItemCount()) return;
    m_is_dragging_playlist = true;
    m_drag_source_view = viewIndex;
    if (!m_playlistCtrl->HasCapture()) {
        m_playlistCtrl->CaptureMouse();
    }
}

void MainFrame::OnPlaylistEndDrag(wxMouseEvent& event) {
    if (!m_is_dragging_playlist) {
        event.Skip();
        return;
    }
    m_is_dragging_playlist = false;
    if (m_playlistCtrl && m_playlistCtrl->HasCapture()) {
        m_playlistCtrl->ReleaseMouse();
    }

    if (!m_playlistCtrl) {
        m_drag_source_view = -1;
        event.Skip();
        return;
    }

    long srcView = m_drag_source_view;
    m_drag_source_view = -1;
    if (srcView < 0 || srcView >= m_playlistCtrl->GetItemCount()) {
        event.Skip();
        return;
    }

    wxPoint pt = event.GetPosition();
    int flags = 0;
    long targetView = m_playlistCtrl->HitTest(pt, flags);
    if (targetView == wxNOT_FOUND) {
        int count = m_playlistCtrl->GetItemCount();
        if (count <= 0) return;
        targetView = count - 1;
    }
    if (targetView == srcView) {
        event.Skip();
        return;
    }

    long srcModel = m_playlistCtrl->GetItemData(srcView);
    long dstModel = m_playlistCtrl->GetItemData(targetView);
    if (srcModel < 0 || srcModel >= static_cast<long>(m_playlist_files.size())) {
        event.Skip();
        return;
    }
    if (dstModel < 0 || dstModel >= static_cast<long>(m_playlist_files.size())) {
        event.Skip();
        return;
    }

    wxString movedPath = m_playlist_files[srcModel];
    m_playlist_files.erase(m_playlist_files.begin() + srcModel);
    if (srcModel < dstModel) {
        dstModel -= 1;
    }
    m_playlist_files.insert(m_playlist_files.begin() + dstModel, movedPath);

    wxCommandEvent dummy;
    OnSearch(dummy);

    long count = m_playlistCtrl->GetItemCount();
    for (long i = 0; i < count; ++i) {
        long modelIndex = m_playlistCtrl->GetItemData(i);
        if (modelIndex < 0 || modelIndex >= static_cast<long>(m_playlist_files.size())) continue;
        if (m_playlist_files[modelIndex] == movedPath) {
            m_is_programmatic_selection = true;
            m_playlistCtrl->SetItemState(i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
            m_playlistCtrl->EnsureVisible(i);
            m_is_programmatic_selection = false;
            break;
        }
    }

    event.Skip();
    SavePlaylistConfig();
}

void MainFrame::PlayIndex(int viewIndex, bool autoPlay) {
    LOG("PlayIndex called with viewIndex: " + std::to_string(viewIndex));
    
    if (!m_playlistCtrl) {
        LOG("ERROR: m_playlistCtrl is null");
        return;
    }

    int count = m_playlistCtrl->GetItemCount();
    LOG("Playlist item count: " + std::to_string(count));

    if (viewIndex < 0 || viewIndex >= count) {
        LOG("Invalid viewIndex: " + std::to_string(viewIndex));
        return;
    }
    
    long modelIndex = m_playlistCtrl->GetItemData(viewIndex);
    LOG("Model index: " + std::to_string(modelIndex));

    if (modelIndex < 0 || modelIndex >= m_playlist_files.size()) {
        LOG("Invalid modelIndex. Files size: " + std::to_string(m_playlist_files.size()));
        return;
    }
    
    wxString path = m_playlist_files[modelIndex];
    // LOG("Path: " + path.ToStdString()); // Commented out to avoid potential crash in logging if string is weird
    
    m_current_play_index = viewIndex; 
    
    LOG("Setting item state...");
    // Select in UI
    m_is_programmatic_selection = true;
    m_playlistCtrl->SetItemState(viewIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    m_playlistCtrl->EnsureVisible(viewIndex);
    m_is_programmatic_selection = false;
    LOG("Item state set.");
    
    // Reload if different
    if (path != m_current_path) {
         try {
            LOG("Loading file from path...");
            // Use c_str() to be safer or verify string first? 
            // path.ToStdString() usually safe.
            LOG("Path to load: " + path.ToStdString());
            
            m_current_path = path;
            
            // Stop engine first
            LOG("Stopping engine...");
            m_engine.stop();
            LOG("Engine stopped.");
            
#ifdef _WIN32
            LOG("Creating MidiFile (Win32)...");
            m_current_midi = std::make_unique<Midi::MidiFile>(path.ToStdWstring());
#else
            LOG("Creating MidiFile...");
            m_current_midi = std::make_unique<Midi::MidiFile>(path.ToStdString());
#endif
            if (!m_current_midi) {
                LOG("ERROR: Failed to create MidiFile object (null)");
                throw std::runtime_error("Failed to create MidiFile object");
            }

            LOG("Midi parsed successfully. Length: " + std::to_string(m_current_midi->length));
            
            LOG("Loading midi into engine...");
            m_engine.load_midi(*m_current_midi);
            LOG("Engine loaded midi.");


            
            // 优化：引擎已复制所有音符数据，释放 MidiFile 中的冗余副本
            m_current_midi->raw_notes_by_track.clear();
            m_current_midi->raw_notes_by_track.shrink_to_fit();
            
            m_progressSlider->SetRange(0, static_cast<int>(m_current_midi->length * 1000));
            
            wxString filename = path.AfterLast('\\');
            m_currentFileLabel->SetLabel(filename);
            
            int totalSec = static_cast<int>(m_current_midi->length);
            m_totalTimeLabel->SetLabel(UI::UIHelpers::FormatTime(totalSec));
            
            // First update track list to populate choices with correct data
            // LOG("Updating track list...");
            UpdateTrackList(); 
            // LOG("Track list updated.");

            // LOG("Loading file config...");
            LoadFileConfig(filename);
            // LOG("Config loaded.");

            // Update BPM and Time Signature
            double bpm = m_current_midi->get_initial_bpm();
            std::pair<int, int> timeSig = m_current_midi->get_initial_time_signature();
            
            wxString statusText;
            if (bpm > 0) {
                statusText = wxString::Format("BPM: %.0f", bpm);
            } else {
                statusText = "BPM: --";
            }
            
            if (timeSig.first > 0) {
                statusText += wxString::Format(" | %d/%d", timeSig.first, timeSig.second);
            } else {
                statusText += " | 4/4";
            }
            GetStatusBar()->SetStatusText(statusText, 2);

            UpdateStatusText(wxString::FromUTF8("已加载"));
            m_stateMachine.SetContextInfo(filename);
            m_stateMachine.TransitionTo(UI::PlaybackStatus::Idle);
        } catch (const std::exception& e) {
            wxString msg = wxString::Format(wxString::FromUTF8("加载失败: %s"), e.what());
            LOG("Exception in PlayIndex: " + std::string(e.what()));
            wxMessageBox(msg, wxString::FromUTF8("错误"), wxICON_ERROR);
            m_current_path = ""; // Reset current path on failure
            m_stateMachine.SetContextInfo(msg);
            m_stateMachine.TransitionTo(UI::PlaybackStatus::Error);
            return;
        } catch (...) {
            LOG("Unknown error during loading in PlayIndex");
            wxMessageBox(wxString::FromUTF8("加载失败: 未知错误"), wxString::FromUTF8("错误"), wxICON_ERROR);
            m_current_path = ""; // Reset current path on failure
            m_stateMachine.SetContextInfo(wxString::FromUTF8("未知错误"));
            m_stateMachine.TransitionTo(UI::PlaybackStatus::Error);
            return;
        }
    } else {
        // LOG("Path is same as current, skipping load.");
    }
    
    // Apply current settings
    // LOG("Applying settings...");
    m_engine.set_speed(m_speedCtrl->GetValue());
    
    wxSpinEvent dummySpin;
    OnPitchRangeChange(dummySpin);
    
    if (autoPlay) {
        // LOG("Auto-play requested.");
        wxCommandEvent dummy;
        OnPlay(dummy);
    }
    // LOG("PlayIndex finished.");
}

void MainFrame::OnPlay(wxCommandEvent& event) {
    if (m_current_midi) {
        if (m_engine.is_playing()) {
            if (m_engine.is_paused()) {
                m_engine.play();
                m_stateMachine.TransitionTo(UI::PlaybackStatus::Playing);
            } else {
                m_engine.pause();
                m_stateMachine.TransitionTo(UI::PlaybackStatus::Paused);
            }
        } else {
            m_engine.play();
            m_stateMachine.TransitionTo(UI::PlaybackStatus::Playing);
        }
    }
}

void MainFrame::OnStop(wxCommandEvent& event) {
    bool wasActive = m_engine.is_playing() || m_engine.is_paused();

    m_engine.stop();
    m_stateMachine.TransitionTo(UI::PlaybackStatus::Stopped);
    m_progressSlider->SetValue(0);
    m_currentTimeLabel->SetLabel("00:00");

    // Fix: If selection changed while playing/paused, load the new file now
    if (wasActive && m_current_play_index != -1 && m_playlistCtrl) {
        // Ensure index is valid
        if (m_current_play_index < m_playlistCtrl->GetItemCount()) {
             long modelIndex = m_playlistCtrl->GetItemData(m_current_play_index);
             if (modelIndex >= 0 && modelIndex < static_cast<long>(m_playlist_files.size())) {
                 if (m_playlist_files[modelIndex] != m_current_path) {
                     PlayIndex(m_current_play_index, false);
                 }
             }
        }
    }
}

void MainFrame::InitializeRandomShuffle() {
    int itemCount = m_playlistCtrl->GetItemCount();
    if (itemCount <= 0) return;
    
    // Create shuffled indices
    m_shuffle_indices.clear();
    m_shuffle_indices.reserve(itemCount);
    for (int i = 0; i < itemCount; ++i) {
        m_shuffle_indices.push_back(i);
    }
    
    // Fisher-Yates shuffle algorithm for better randomness
    std::shuffle(m_shuffle_indices.begin(), m_shuffle_indices.end(), m_random_engine);
    m_current_shuffle_index = 0;
    m_need_shuffle_reset = false;
}

int MainFrame::GetNextRandomIndex() {
    int itemCount = m_playlistCtrl->GetItemCount();
    if (itemCount <= 0) return -1;
    
    // Reset shuffle sequence if needed
    if (m_need_shuffle_reset || m_current_shuffle_index >= m_shuffle_indices.size()) {
        InitializeRandomShuffle();
    }
    
    // Get next index from shuffled sequence
    int nextIndex = m_shuffle_indices[m_current_shuffle_index];
    m_current_shuffle_index++;
    
    // Mark that we need to reset when we reach the end
    if (m_current_shuffle_index >= m_shuffle_indices.size()) {
        m_need_shuffle_reset = true;
    }
    
    return nextIndex;
}

void MainFrame::ResetRandomSequence() {
    m_need_shuffle_reset = true;
    m_current_shuffle_index = 0;
    m_shuffle_indices.clear();
}

void MainFrame::OnPrev(wxCommandEvent& event) {
    if (m_playlistCtrl->GetItemCount() == 0) return;
    
    int nextIndex = -1;
    
    if (m_play_mode == wxString::FromUTF8("随机播放")) {
        // For previous in random mode, we reset and get a new random song
        ResetRandomSequence();
        nextIndex = GetNextRandomIndex();
    } else {
        nextIndex = m_current_play_index - 1;
        if (nextIndex < 0) {
            if (m_play_mode == wxString::FromUTF8("列表循环")) {
                nextIndex = m_playlistCtrl->GetItemCount() - 1;
            } else {
                nextIndex = 0; // Stop or stay at start?
            }
        }
    }
    
    if (nextIndex >= 0) {
        PlayIndex(nextIndex);
    }
}

void MainFrame::OnNext(wxCommandEvent& event) {
    if (m_playlistCtrl->GetItemCount() == 0) return;
    
    int nextIndex = -1;
    
    if (m_play_mode == wxString::FromUTF8("随机播放")) {
        nextIndex = GetNextRandomIndex();
    } else {
        nextIndex = m_current_play_index + 1;
        if (nextIndex >= m_playlistCtrl->GetItemCount()) {
            if (m_play_mode == wxString::FromUTF8("列表循环")) {
                nextIndex = 0;
            } else {
                // End of list, stop if playing
                if (m_engine.is_playing()) {
                    wxCommandEvent dummy;
                    OnStop(dummy);
                }
                return;
            }
        }
    }
    
    if (nextIndex >= 0) {
        PlayIndex(nextIndex);
    }
}

void MainFrame::OnModeClick(wxCommandEvent& event) {
    // Cycle modes: "单曲播放", "单曲循环", "列表播放", "列表循环", "随机播放"
    if (m_play_mode == wxString::FromUTF8("单曲播放")) m_play_mode = wxString::FromUTF8("单曲循环");
    else if (m_play_mode == wxString::FromUTF8("单曲循环")) m_play_mode = wxString::FromUTF8("列表播放");
    else if (m_play_mode == wxString::FromUTF8("列表播放")) m_play_mode = wxString::FromUTF8("列表循环");
    else if (m_play_mode == wxString::FromUTF8("列表循环")) m_play_mode = wxString::FromUTF8("随机播放");
    else m_play_mode = wxString::FromUTF8("单曲播放");
    
    m_modeBtn->SetLabel(m_play_mode);
    
    // Reset random sequence when entering/exiting random mode
    if (m_play_mode == wxString::FromUTF8("随机播放")) {
        ResetRandomSequence();
    }
    
    SaveGlobalConfig();
}

void MainFrame::OnDecomposeToggle(wxCommandEvent& event) {
    m_decompose_chords = m_decomposeBtn->GetValue();
    m_engine.set_decompose(m_decompose_chords);
    SaveGlobalConfig();
}

void MainFrame::OnSliderTrack(wxCommandEvent& event) {
    m_is_dragging_slider = true;
}

void MainFrame::OnSliderRelease(wxCommandEvent& event) {
    m_is_dragging_slider = false;
    if (m_current_midi && m_current_midi->length > 0) {
        int val = m_progressSlider->GetValue();
        // Slider value is in milliseconds (set in PlayIndex)
        double time = static_cast<double>(val) / 1000.0;
        m_engine.seek(time);
        
        // If not playing, start playing
        if (!m_engine.is_playing()) {
             m_engine.play();
             m_playBtn->SetLabel(wxString::FromUTF8("暂停"));
             m_stateMachine.TransitionTo(UI::PlaybackStatus::Playing);
        }
    }
}

void MainFrame::OnSliderChange(wxCommandEvent& event) {
    if (m_is_dragging_slider && m_current_midi && m_current_midi->length > 0) {
        int val = m_progressSlider->GetValue();
        // Slider value is in milliseconds
        double current_time = static_cast<double>(val) / 1000.0;
        int currentSec = static_cast<int>(current_time);
        m_currentTimeLabel->SetLabel(wxString::Format("%02d:%02d", currentSec / 60, currentSec % 60));
    }
}

void MainFrame::OnSpeedChange(wxSpinDoubleEvent& event) {
    m_engine.set_speed(event.GetValue());
}

void MainFrame::OnPitchRangeChange(wxSpinEvent& event) {
    int minP = m_minPitchCtrl->GetValue();
    int maxP = m_maxPitchCtrl->GetValue();
    
    // Ensure min <= max
    if (minP > maxP) {
        if (event.GetId() == ID_MIN_PITCH_CTRL) maxP = minP;
        else minP = maxP;
        
        m_minPitchCtrl->SetValue(minP);
        m_maxPitchCtrl->SetValue(maxP);
    }
    
    m_engine.set_pitch_range(minP, maxP);
    SaveGlobalConfig();
}

void MainFrame::OnLoadKeymap(wxCommandEvent& event) {
    wxFileDialog openFileDialog(this, wxString::FromUTF8("加载键位配置"), "", "",
                                wxString::FromUTF8("键位配置文件 (*.txt)|*.txt"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openFileDialog.ShowModal() == wxID_CANCEL) return;
    
    bool ok = m_engine.get_key_manager().load_config(openFileDialog.GetPath().ToStdString());
    if (ok) UpdateStatusText(wxString::FromUTF8("键位已加载"));
    else UpdateStatusText(wxString::FromUTF8("键位加载失败"));
    if (ok) {
        m_engine.notify_keymap_changed();
        SaveKeymapConfig();
    }
}

void MainFrame::OnSaveKeymap(wxCommandEvent& event) {
    wxFileDialog saveFileDialog(this, wxString::FromUTF8("保存键位配置"), "", "",
                                wxString::FromUTF8("键位配置文件 (*.txt)|*.txt"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (saveFileDialog.ShowModal() == wxID_CANCEL) return;
    
    bool ok = m_engine.get_key_manager().save_config(saveFileDialog.GetPath().ToStdString());
    if (ok) UpdateStatusText(wxString::FromUTF8("键位已保存"));
    else UpdateStatusText(wxString::FromUTF8("键位保存失败"));
    if (ok) SaveKeymapConfig();
}

void MainFrame::OnResetKeymap(wxCommandEvent& event) {
    m_engine.get_key_manager().reset_to_default();
    UpdateStatusText(wxString::FromUTF8("键位已重置"));
    m_engine.notify_keymap_changed();
    SaveKeymapConfig();
}
void MainFrame::OnSchedule(wxCommandEvent& event) {
    if (m_is_scheduled) {
        // Cancel schedule
        m_is_scheduled = false;
        m_active_schedule_token.store(0);
        m_schedule_token.fetch_add(1);
        m_schedule_target_epoch_us.store(0);
        m_scheduleBtn->SetLabel(wxString::FromUTF8("定时"));
        m_schedMin->Enable(true);
        m_schedSec->Enable(true);
        UpdateStatusText(wxString::FromUTF8("定时已取消"));
        if (m_stateMachine.GetCurrentState() == UI::PlaybackStatus::Scheduled) {
            m_stateMachine.TransitionTo(UI::PlaybackStatus::Idle);
        }
    } else {
        // Start schedule
        m_is_scheduled = true;
        m_scheduleBtn->SetLabel(wxString::FromUTF8("取消"));
        m_schedMin->Enable(false);
        m_schedSec->Enable(false);
        
        int mins = m_schedMin->GetValue();
        int secs = m_schedSec->GetValue();
        wxString schedInfo = wxString::Format(wxString::FromUTF8("目标: %02d:%02d"), mins, secs);
        m_stateMachine.SetContextInfo(schedInfo);
        m_stateMachine.TransitionTo(UI::PlaybackStatus::Scheduled);
        UpdateStatusText(wxString::Format(wxString::FromUTF8("定时已启动 (目标: %02d:%02d)"), mins, secs));

        const auto token = m_schedule_token.fetch_add(1) + 1;
        m_active_schedule_token.store(token);
        StartBackgroundTask([this, token, mins, secs]() {
            const bool success = Util::NtpClient::IsSynced();

            if (!m_isShuttingDown.load() && m_is_scheduled && m_active_schedule_token.load() == token) {
                wxCommandEvent* event = new wxCommandEvent(wxEVT_COMMAND_BUTTON_CLICKED, ID_NTP_TIMER);
                event->SetClientData(new std::pair<bool, wxString>(success,
                    wxString::Format(wxString::FromUTF8("定时已启动 (目标: %02d:%02d)"), mins, secs)));
                wxQueueEvent(this, event);
            }

            auto now = Util::NtpClient::GetNow();
            auto now_c = std::chrono::system_clock::to_time_t(now);
            struct tm parts;
            localtime_s(&parts, &now_c);

            struct tm target_parts = parts;
            target_parts.tm_min = mins;
            target_parts.tm_sec = secs;
            target_parts.tm_isdst = -1;

            auto target_c = mktime(&target_parts);
            if (target_c == (time_t)-1) {
                return;
            }

            auto target_tp = std::chrono::system_clock::from_time_t(target_c);
            if (target_tp <= now) {
                target_tp += std::chrono::hours(1);
            }

            // 在进入循环前，先存入原始目标时间（不含补偿）
            // 这样 OnScheduleTrigger 可以根据最新的 latency_comp 实时计算抖动
            m_schedule_target_epoch_us.store(
                (long long)std::chrono::duration_cast<std::chrono::microseconds>(target_tp.time_since_epoch()).count()
            );

            while (!m_isShuttingDown.load() && m_active_schedule_token.load() == token) {
                now = Util::NtpClient::GetNow();
                const auto latency_us = m_latency_comp_us.load();
                // 逻辑反转：正数表示推迟(增加延迟)，所以目标时间 = 原始目标 + 延迟值
                // 负数表示提前(减少延迟)，所以目标时间 = 原始目标 + (-延迟值)
                const auto effective_target_tp = target_tp + std::chrono::microseconds(latency_us);
                
                // 睡眠等待逻辑基于 effective_target_tp (含补偿)
                if (now >= effective_target_tp) {
                    break;
                }
                const auto remaining = effective_target_tp - now;

                if (remaining > std::chrono::microseconds(2000)) {
                    auto sleep_for = std::chrono::duration_cast<std::chrono::microseconds>(remaining) - std::chrono::microseconds(500);
                    if (sleep_for > std::chrono::milliseconds(50)) {
                        sleep_for = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::milliseconds(50));
                    } else if (sleep_for < std::chrono::microseconds(200)) {
                        sleep_for = std::chrono::microseconds(200);
                    }
                    std::this_thread::sleep_for(sleep_for);
                    continue;
                }

                const auto fine_latency_us = m_latency_comp_us.load();
                const auto fine_effective_target_tp = target_tp + std::chrono::microseconds(fine_latency_us);
                
                while (!m_isShuttingDown.load() && m_active_schedule_token.load() == token) {
                    now = Util::NtpClient::GetNow();
                    if (now >= fine_effective_target_tp) {
                        break;
                    }
                    const auto fine_remaining = fine_effective_target_tp - now;
                    if (fine_remaining > std::chrono::microseconds(200)) {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    } else {
                        std::this_thread::yield();
                    }
                }
                break; // 显式退出外层循环
            }

            if (m_isShuttingDown.load() || m_active_schedule_token.load() != token) {
                return;
            }

            auto* evt = new wxCommandEvent(wxEVT_COMMAND_BUTTON_CLICKED, ID_SCHEDULE_TRIGGER);
            evt->SetClientData(new unsigned long long(token));
            wxQueueEvent(this, evt);
        });
    }
}

void MainFrame::OnWindowChoiceDropdown(wxMouseEvent& event) {
    UpdateWindowList();
    event.Skip();
}

void MainFrame::OnNtpSyncComplete(wxCommandEvent& event) {
    // 安全地处理来自后台线程的NTP同步结果
    if (m_isShuttingDown.load()) {
        return;
    }
    
    std::pair<bool, wxString>* data = static_cast<std::pair<bool, wxString>*>(event.GetClientData());
    if (data) {
        bool success = data->first;
        wxString baseText = data->second;
        
        if (success) {
            UpdateStatusText(baseText + wxString::FromUTF8(" - 时间已同步"));
        } else {
            UpdateStatusText(baseText + wxString::FromUTF8(" - 时间同步中..."));
        }
        
        delete data; // 清理动态分配的数据
    }
}

void MainFrame::OnScheduleTrigger(wxCommandEvent& event) {
    if (m_isShuttingDown.load()) {
        return;
    }

    auto* token_ptr = static_cast<unsigned long long*>(event.GetClientData());
    const auto token = token_ptr ? *token_ptr : 0ull;
    if (token_ptr) {
        delete token_ptr;
    }

    if (!m_is_scheduled || m_active_schedule_token.load() != token) {
        return;
    }

    const auto target_us = m_schedule_target_epoch_us.load();

    UpdateStatusText(wxString::FromUTF8("定时任务触发"));

    m_is_scheduled = false;
    m_active_schedule_token.store(0);
    m_schedule_target_epoch_us.store(0);
    m_scheduleBtn->SetLabel(wxString::FromUTF8("定时"));
    m_schedMin->Enable(true);
    m_schedSec->Enable(true);

    wxCommandEvent dummy;
    OnPlay(dummy);
}

// Global Hook Implementation
static HHOOK g_hKeyboardHook = NULL;
static MainFrame* g_pMainFrame = NULL;

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKey = (KBDLLHOOKSTRUCT*)lParam;
        // Check for F12 (VK_F12 = 0x7B) and Key Release to avoid repeat
        if (wParam == WM_KEYUP && pKey->vkCode == VK_F12) {
            if (g_pMainFrame) {
                // Post event to MainFrame to handle in UI thread
                wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, ID_PLAY_BTN);
                wxPostEvent(g_pMainFrame, event);
            }
        }
    }
    return CallNextHookEx(g_hKeyboardHook, nCode, wParam, lParam);
}

void MainFrame::InstallGlobalHook() {
    g_pMainFrame = this;
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
    if (!g_hKeyboardHook) {
        LOG("Failed to install global keyboard hook");
    }
}

void MainFrame::UninstallGlobalHook() {
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = NULL;
    }
    g_pMainFrame = NULL;
}

void MainFrame::UpdateTrackList() {
    if (!m_current_midi) return;

    wxArrayString displayItems;
    std::vector<int> trackIndices;
    
    displayItems.Add(wxString::FromUTF8("全部音轨"));
    trackIndices.push_back(-1);
    
    int displayIdx = 1;
    for (size_t i = 0; i < m_current_midi->tracks.size(); ++i) {
        const auto& track = m_current_midi->tracks[i];
        if (track.note_count > 0) {
             wxString name = wxString::FromUTF8(track.name.c_str());
             if (name.IsEmpty() && !track.name.empty()) {
                name = wxString(track.name.c_str(), wxConvLocal);
             }
             if (name.IsEmpty()) name = wxString::Format("Track %d", (int)i);
             
             displayItems.Add(wxString::Format("%d: %s", displayIdx++, name));
             trackIndices.push_back((int)i);
        }
    }
    
    if (displayItems.GetCount() == 1) {
         displayItems.Add(wxString::FromUTF8("空音轨"));
         trackIndices.push_back(-1);
    }

    // Convert channel configs to UIHelpers format
    std::vector<UI::ChannelUIUpdater::ChannelUpdateInfo> channelInfos;
    for (auto& config : m_channelConfigs) {
        UI::ChannelUIUpdater::ChannelUpdateInfo info;
        info.windowChoice = config.windowChoice;
        info.trackChoice = config.trackChoice;
        info.enableBtn = config.enableBtn;
        info.transposeCtrl = config.transposeCtrl;
        info.channelIndex = config.channelIndex;
        channelInfos.push_back(info);
    }
    
    // Use helper to update all channels
    UI::ChannelUIUpdater::UpdateTrackLists(channelInfos, displayItems, trackIndices);
    
    // Sync engine mappings
    for (auto& config : m_channelConfigs) {
        int sel = config.trackChoice->GetSelection();
        if (sel != wxNOT_FOUND) {
            void* clientData = config.trackChoice->GetClientData(sel);
            if (clientData != nullptr) {
                int trackIdx = static_cast<int>(reinterpret_cast<intptr_t>(clientData));
                m_engine.set_channel_track(config.channelIndex, trackIdx);
            }
        }
    }
}

void MainFrame::OnTimer(wxTimerEvent& event) {
    auto now_ntp = Util::NtpClient::GetNow();
    const bool synced = Util::NtpClient::IsSynced();
    static bool last_synced = false;
    if (synced != last_synced) {
        UpdateStatusText(synced ? wxString::FromUTF8("时间已同步") : wxString::FromUTF8("时间同步中..."));
        last_synced = synced;
    }
    
    // Show NTP Time
    if (synced) {
         time_t now_c = std::chrono::system_clock::to_time_t(now_ntp);
         struct tm parts;
         localtime_s(&parts, &now_c);
         
         static int last_ntp_sec = -1;
         if (parts.tm_sec != last_ntp_sec) {
             m_ntpLabel->SetLabel(wxString::Format("%02d:%02d", parts.tm_min, parts.tm_sec));
             last_ntp_sec = parts.tm_sec;
         }
    } else {
         static bool last_synced_state = true;
         if (last_synced_state) {
             m_ntpLabel->SetLabel("--:--");
             last_synced_state = false;
         }
    }

    if (m_engine.is_playing()) {
        double t = m_engine.get_current_time();
        
        // 优化：减少UI更新频率
        static int updateCounter = 0;
        static double lastUpdateTime = -1.0;
        bool shouldUpdateUI = (updateCounter++ % 3 == 0); // 每50ms更新一次
        
        if (!m_is_dragging_slider && m_current_midi && m_current_midi->length > 0) {
             // Slider value is in milliseconds
             int newSliderVal = static_cast<int>(t * 1000);
             if (shouldUpdateUI && std::abs(m_progressSlider->GetValue() - newSliderVal) > 100) {
                 m_progressSlider->SetValue(newSliderVal);
             }
             
             // 仅在秒数变化时更新时间标签
             if (std::abs(t - lastUpdateTime) >= 1.0 || lastUpdateTime < 0) {
                 m_stateUpdater->UpdateTimeLabels(t, m_current_midi->length);
                 lastUpdateTime = t;
             }
        }
        
        // Ensure state is Playing (not Paused)
        if (!m_engine.is_paused() && !m_stateMachine.IsPlaying()) {
            m_stateMachine.TransitionTo(UI::PlaybackStatus::Playing);
        }
        
        // Check if finished
        if (m_current_midi && t >= m_current_midi->length && m_current_midi->length > 0) {
            if (m_play_mode == wxString::FromUTF8("单曲循环")) {
                m_engine.seek(0);
                m_engine.play();
            } else if (m_play_mode == wxString::FromUTF8("单曲播放")) {
                wxCommandEvent dummy;
                OnStop(dummy);
            } else {
                // List modes
                wxCommandEvent dummy;
                OnNext(dummy);
            }
        }
    } else if (m_stateMachine.IsActive()) {
        // Engine stopped but state still active - sync state
        m_stateMachine.TransitionTo(UI::PlaybackStatus::Stopped);
    }
}

void MainFrame::OnStatusTimer(wxTimerEvent& event) {
    // Revert status text to author signature when idle
    if (m_stateMachine.IsIdle()) {
        SetStatusText(wxString::FromUTF8("By:最终幻想14水晶世界_黄金谷_吸溜"), 0);
    } else {
        m_stateUpdater->UpdateStatusBar();
    }
}

void MainFrame::UpdateStatusText(const wxString& text) {
    SetStatusText(text, 0);
    // Auto revert after 3 seconds
    m_statusTimer.Start(3000, wxTIMER_ONE_SHOT);
}

void MainFrame::OnStateChange(UI::PlaybackStatus oldState, UI::PlaybackStatus newState) {
    // Update UI based on state change
    m_stateUpdater->UpdateUI();
    
    // Log state transition for debugging
    // LOG("State transition: " + std::to_string((int)oldState) + " -> " + std::to_string((int)newState));
}

void MainFrame::UpdateWindowList() {
    m_windowList = Core::KeyboardSimulator::GetWindowList();
    
    // Sort windows by title (case insensitive)
    std::sort(m_windowList.begin(), m_windowList.end(), [](const Core::KeyboardSimulator::WindowInfo& a, const Core::KeyboardSimulator::WindowInfo& b) {
#ifdef _WIN32
        return _stricmp(a.title.c_str(), b.title.c_str()) < 0;
#else
        return strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
#endif
    });
    
    // Convert channel configs to UIHelpers format
    std::vector<UI::ChannelUIUpdater::ChannelUpdateInfo> channelInfos;
    for (auto& config : m_channelConfigs) {
        UI::ChannelUIUpdater::ChannelUpdateInfo info;
        info.windowChoice = config.windowChoice;
        info.trackChoice = config.trackChoice;
        info.enableBtn = config.enableBtn;
        info.transposeCtrl = config.transposeCtrl;
        info.channelIndex = config.channelIndex;
        channelInfos.push_back(info);
    }
    
    // Use helper to update all channels
    UI::ChannelUIUpdater::UpdateWindowLists(channelInfos, m_windowList);
    
    // Sync engine
    for (size_t i = 0; i < m_channelConfigs.size(); ++i) {
        auto& config = m_channelConfigs[i];
        int sel = config.windowChoice->GetSelection();
        if (sel != wxNOT_FOUND && sel != 0) {
            void* clientData = config.windowChoice->GetClientData(sel);
            if (clientData != nullptr) {
                m_engine.set_channel_window(config.channelIndex, clientData);
            } else {
                m_engine.set_channel_window(config.channelIndex, nullptr);
            }
        } else {
            m_engine.set_channel_window(config.channelIndex, nullptr);
        }
    }
}

void MainFrame::SaveFileConfig() {
    if (m_current_path.IsEmpty()) return;
    
    wxString filename = m_current_path.AfterLast('\\');
    // Sanitize filename for config path (replace / and \)
    filename.Replace("/", "_");
    filename.Replace("\\", "_");
    
    wxString groupName = "/Files/" + filename;
    
    m_config->SetPath(groupName);
    
    bool fileHasConfig = false;

    for (const auto& c : m_channelConfigs) {
        wxString prefix = wxString::Format("Channel_%d/", c.channelIndex);
        bool channelHasConfig = false;

        // 1. Enabled
        bool defaultEnabled = (c.channelIndex == 0);
        bool currentEnabled = c.enableBtn->GetValue();
        if (currentEnabled != defaultEnabled) {
            m_config->Write(prefix + "Enabled", currentEnabled);
            channelHasConfig = true;
        } else {
            m_config->DeleteEntry(prefix + "Enabled");
        }
        
        // 2. Window - 只保存标题和进程名用于恢复
        wxString currentWindow = c.windowChoice->GetStringSelection();
        if (currentWindow.IsEmpty()) currentWindow = wxString::FromUTF8("未选择");
        
        if (currentWindow != wxString::FromUTF8("未选择")) {
             m_config->Write(prefix + "WindowTitle", currentWindow);
             channelHasConfig = true;
        } else {
             m_config->DeleteEntry(prefix + "WindowTitle");
        }
        
        // 3. WindowProcess - 保存进程名或 MIDI 设备类型用于恢复
        wxString processName = "";
        int selIdx = c.windowChoice->GetSelection();
        if (selIdx != wxNOT_FOUND) {
            void* clientData = c.windowChoice->GetClientData(selIdx);
            if (clientData) {
                HWND h = static_cast<HWND>(clientData);
                for (const auto& win : m_windowList) {
                    if (win.hwnd == h) {
                        processName = win.process_name;
                        break;
                    }
                }
            }
        }
        
        if (!processName.IsEmpty()) {
            m_config->Write(prefix + "WindowProcess", processName);
            channelHasConfig = true;
        } else {
            m_config->DeleteEntry(prefix + "WindowProcess");
        }

        // 4. Transpose
        int currentTranspose = c.transposeCtrl->GetValue();
        if (currentTranspose != 0) {
            m_config->Write(prefix + "Transpose", currentTranspose);
            channelHasConfig = true;
        } else {
            m_config->DeleteEntry(prefix + "Transpose");
        }
        
        // 5. Track
        wxString currentTrack = c.trackChoice->GetStringSelection();
        if (currentTrack.IsEmpty()) currentTrack = wxString::FromUTF8("全部音轨");
        
        if (currentTrack != wxString::FromUTF8("全部音轨")) {
            m_config->Write(prefix + "Track", currentTrack);
            channelHasConfig = true;
        } else {
            m_config->DeleteEntry(prefix + "Track");
        }

        if (channelHasConfig) {
            fileHasConfig = true;
        } else {
            m_config->DeleteGroup(wxString::Format("Channel_%d", c.channelIndex));
        }
    }
    
    m_config->SetPath("/"); // Reset path
    
    if (!fileHasConfig) {
        m_config->DeleteGroup(groupName);
    }

    m_config->Flush();
}

void MainFrame::LoadGlobalConfig() {
    m_config->SetPath("/Global");

    int minPitch = 48;
    int maxPitch = 84;
    m_config->Read("MinPitch", &minPitch, 48);
    m_config->Read("MaxPitch", &maxPitch, 84);

    wxString playMode = wxString::FromUTF8("单曲播放");
    m_config->Read("PlayMode", &playMode, wxString::FromUTF8("单曲播放"));

    bool decompose = false;
    m_config->Read("Decompose", &decompose, false);

    m_config->SetPath("/");

    m_minPitchCtrl->SetValue(minPitch);
    m_maxPitchCtrl->SetValue(maxPitch);

    m_play_mode = playMode;
    m_modeBtn->SetLabel(m_play_mode);

    m_decompose_chords = decompose;
    m_decomposeBtn->SetValue(decompose);
    m_engine.set_decompose(decompose);

    wxSpinEvent dummySpin;
    OnPitchRangeChange(dummySpin);
}

void MainFrame::SaveGlobalConfig() {
    m_config->SetPath("/Global");

    m_config->Write("MinPitch", m_minPitchCtrl->GetValue());
    m_config->Write("MaxPitch", m_maxPitchCtrl->GetValue());
    m_config->Write("PlayMode", m_play_mode);
    m_config->Write("Decompose", m_decompose_chords);

    m_config->SetPath("/");
    m_config->Flush();
}

void MainFrame::LoadPlaylistConfig() {
    m_playlistCtrl->DeleteAllItems();
    
    // 使用 PlaylistManager 加载配置
    m_playlistManager.LoadConfig(m_config.get());
    
    // 更新播放列表选择器
    UpdatePlaylistChoice();
    
    // 刷新文件列表UI
    RefreshPlaylistUI();
}

void MainFrame::SavePlaylistConfig() {
    // 同步当前文件列表到 PlaylistManager
    // 注意：m_playlist_files 现在是 PlaylistManager 的引用代理
    // 这里不需要额外同步，因为文件操作已经直接操作 PlaylistManager
    
    // 使用 PlaylistManager 保存配置
    m_playlistManager.SaveConfig(m_config.get());
}

void MainFrame::LoadKeymapConfig() {
    if (!m_config->HasGroup("/Keymap")) {
        return;
    }

    m_config->SetPath("/Keymap");

    std::map<int, Util::KeyMapping> map;
    wxString entry;
    long idx = 0;
    bool cont = m_config->GetFirstEntry(entry, idx);
    while (cont) {
        wxString value;
        if (m_config->Read(entry, &value)) {
            wxString vkStr = value.BeforeFirst(',');
            wxString modStr = value.AfterFirst(',');

            long pitch = 0;
            long vk = 0;
            long mod = 0;
            if (entry.ToLong(&pitch) && vkStr.ToLong(&vk) && modStr.ToLong(&mod)) {
                map[static_cast<int>(pitch)] = {static_cast<int>(vk), static_cast<int>(mod)};
            }
        }
        cont = m_config->GetNextEntry(entry, idx);
    }

    m_config->SetPath("/");

    if (!map.empty()) {
        m_engine.get_key_manager().set_map(map);
    }
}

void MainFrame::SaveKeymapConfig() {
    m_config->SetPath("/");
    m_config->DeleteGroup("Keymap");
    m_config->SetPath("/Keymap");

    const auto& map = m_engine.get_key_manager().get_map();
    for (const auto& pair : map) {
        wxString key = wxString::Format("%d", pair.first);
        wxString value = wxString::Format("%d,%d", pair.second.vk_code, pair.second.modifier);
        m_config->Write(key, value);
    }

    m_config->SetPath("/");
    m_config->Flush();
}

void MainFrame::LoadLastSelectedFile() {
    if (!m_config->HasGroup("/LastSelected")) {
        return;
    }
    
    m_config->SetPath("/LastSelected");
    
    wxString lastFilePath;
    if (m_config->Read("FilePath", &lastFilePath) && !lastFilePath.IsEmpty() && wxFileExists(lastFilePath)) {
        // 查找文件在播放列表中的位置
        for (size_t i = 0; i < m_playlist_files.size(); ++i) {
            if (m_playlist_files[i] == lastFilePath) {
                // 选中该文件但不自动播放
                PlayIndex(static_cast<int>(i), false);
                break;
            }
        }
    }
    
    m_config->SetPath("/");
}

void MainFrame::SaveLastSelectedFile() {
    m_config->SetPath("/");
    m_config->DeleteGroup("LastSelected");
    m_config->SetPath("/LastSelected");
    
    if (!m_current_path.IsEmpty()) {
        m_config->Write("FilePath", m_current_path);
    }
    
    m_config->SetPath("/");
    m_config->Flush();
}

wxString MainFrame::RemoveParenthesesContent(const wxString& title) {
    wxString result = title;
    
    // 找到第一个左括号的位置
    size_t startPos = result.Find('(');
    if (startPos != wxString::npos) {
        // 找到对应的右括号位置
        size_t endPos = result.find(')', startPos);
        if (endPos != wxString::npos) {
            // 移除括号及其内容
            result = result.substr(0, startPos) + result.substr(endPos + 1);
        }
    }
    
    // 去除前后空格
    result.Trim(true);  // 去除前导空格
    result.Trim(false); // 去除尾随空格
    
    return result;
}

bool MainFrame::CompareWindowTitleAndProcess(const wxString& configTitle, const wxString& configProcess, 
                                           const Core::KeyboardSimulator::WindowInfo& windowInfo) {
    // 去除配置标题中的括号内容
    wxString cleanConfigTitle = RemoveParenthesesContent(configTitle);
    wxString cleanWindowTitle = RemoveParenthesesContent(windowInfo.title);
    
    // 同时比较标题和进程名
    bool titleMatch = (cleanConfigTitle == cleanWindowTitle);
    bool processMatch = (configProcess.ToStdString() == windowInfo.process_name);
    
    return titleMatch && processMatch;
}

void MainFrame::LoadFileConfig(const wxString& filename) {
    LOG("LoadFileConfig start: " + filename.ToStdString());
    wxString safeName = filename;
    safeName.Replace("/", "_");
    safeName.Replace("\\", "_");
    
    wxString groupName = "/Files/" + safeName;
    
    // Ensure window list is up to date before loading selection
    LOG("Updating window list...");
    UpdateWindowList();
    LOG("Window list updated.");
    
    // Reset path first
    m_config->SetPath("/");
    
    if (m_config->HasGroup(groupName)) {
        LOG("Loading existing config group: " + groupName.ToStdString());
        m_config->SetPath(groupName);
    } else {
        LOG("No existing config, using defaults.");
    }
    
    bool hasConfig = m_config->GetPath() == groupName;

    for (auto& c : m_channelConfigs) {
        wxString prefix = wxString::Format("Channel_%d/", c.channelIndex);
        
        // 1. Enabled
        bool defaultEnabled = (c.channelIndex == 0);
        bool enabled = defaultEnabled;
        if (hasConfig) {
            m_config->Read(prefix + "Enabled", &enabled, defaultEnabled);
        }
        
        c.enableBtn->SetValue(enabled);
        UpdateChannelUI(c.channelIndex, enabled);
        m_engine.set_channel_enable(c.channelIndex, enabled);
        
        // 2. Window - 使用标题和进程名进行恢复
        wxString currentSel = c.windowChoice->GetStringSelection();
        wxString defaultWindow = wxString::FromUTF8("未选择");
        
        // Use current selection as default if available (prevents reset on new file load)
        if (!currentSel.IsEmpty()) {
            defaultWindow = currentSel;
        }
        
        wxString windowTitle = defaultWindow;
        wxString windowProcess = "";
        
        if (hasConfig) {
            m_config->Read(prefix + "WindowTitle", &windowTitle, defaultWindow);
            m_config->Read(prefix + "WindowProcess", &windowProcess, "");
        }
        
        bool found = false;
        
        // 1. 尝试精确匹配标题（去除括号内容后）
        if (c.windowChoice->FindString(windowTitle) != wxNOT_FOUND) {
            c.windowChoice->SetStringSelection(windowTitle);
            found = true;
        } 
        // 2. 尝试匹配 MIDI 设备
        // 3. 尝试按标题和进程名同时匹配（自动恢复）
        else if (!windowProcess.IsEmpty()) {
            for (unsigned int i = 0; i < c.windowChoice->GetCount(); ++i) {
                if (i == 0) continue;
                
                size_t winIdx = i - 1;
                if (winIdx < m_windowList.size()) {
                    if (CompareWindowTitleAndProcess(windowTitle, windowProcess, m_windowList[winIdx])) {
                         c.windowChoice->SetSelection(i);
                         found = true;
                         LOG("Recovered window by Title and Process: " + windowTitle.ToStdString() + 
                             " / " + windowProcess.ToStdString());
                         break;
                    }
                }
            }
        }
        // 4. 备选方案：仅按进程名匹配
        else if (!windowProcess.IsEmpty()) {
            for (unsigned int i = 0; i < c.windowChoice->GetCount(); ++i) {
                if (i == 0) continue;
                
                size_t winIdx = i - 1;
                if (winIdx < m_windowList.size()) {
                    if (m_windowList[winIdx].process_name == windowProcess.ToStdString()) {
                         c.windowChoice->SetSelection(i);
                         found = true;
                         LOG("Recovered window by Process Name: " + windowProcess.ToStdString());
                         break;
                    }
                }
            }
        }
        
        if (!found) {
             c.windowChoice->SetSelection(0);
        }
        
        int selWin = c.windowChoice->GetSelection();
        if (selWin != wxNOT_FOUND && selWin != 0) { // 0 is "None"
                void* clientData = c.windowChoice->GetClientData(selWin);
                if (clientData != nullptr) {
                    m_engine.set_channel_window(c.channelIndex, clientData);
                } else {
                    m_engine.set_channel_window(c.channelIndex, nullptr);
                }
        } else {
                m_engine.set_channel_window(c.channelIndex, nullptr);
        }
        
        // 3. Transpose
        int transpose = 0;
        if (hasConfig) {
            m_config->Read(prefix + "Transpose", &transpose, 0);
        }
        c.transposeCtrl->SetValue(transpose);
        if (transpose > 0) {
            c.transposeCtrl->SetValue(wxString::Format("+%d", transpose));
        }
        m_engine.set_channel_transpose(c.channelIndex, transpose);
        
        // 4. Track
        wxString defaultTrack = wxString::FromUTF8("全部音轨");
        wxString track = defaultTrack;
        if (hasConfig) {
            m_config->Read(prefix + "Track", &track, defaultTrack);
        }
        
        if (c.trackChoice->FindString(track) != wxNOT_FOUND) {
            c.trackChoice->SetStringSelection(track);
        } else {
            c.trackChoice->SetSelection(0);
        }
        
        // Set engine track index from client data
        int selTrack = c.trackChoice->GetSelection();
        if (selTrack != wxNOT_FOUND) {
            void* clientData = c.trackChoice->GetClientData(selTrack);
            if (clientData != nullptr) {
                int trackIdx = static_cast<int>(reinterpret_cast<intptr_t>(clientData));
                m_engine.set_channel_track(c.channelIndex, trackIdx);
            }
        } else {
            m_engine.set_channel_track(c.channelIndex, -1);
        }
    }
    
    if (hasConfig) {
        m_config->SetPath("/");
    }

    // Disable remaining channels (8-15) to match Python's 8-channel limit behavior
    for (int i = 8; i < 16; ++i) {
        m_engine.set_channel_enable(i, false);
    }

    // LOG("LoadFileConfig finished.");
}

// ================= 多播放列表功能实现 =================

void MainFrame::OnPlaylistChoice(wxCommandEvent& event) {
    int sel = m_playlistChoice->GetSelection();
    if (sel != wxNOT_FOUND && sel != m_playlistManager.GetCurrentPlaylistIndex()) {
        SwitchToPlaylist(sel);
    }
}

void MainFrame::OnAddPlaylist(wxCommandEvent& event) {
    // 弹出对话框输入新播放列表名称
    wxString name = wxGetTextFromUser(
        wxString::FromUTF8("请输入新播放列表的名称:"),
        wxString::FromUTF8("新建播放列表"),
        wxString::FromUTF8("新列表"),
        this
    );
    
    if (!name.IsEmpty()) {
        int newIndex = m_playlistManager.CreatePlaylist(name);
        UpdatePlaylistChoice();
        m_playlistChoice->SetSelection(newIndex);
        SwitchToPlaylist(newIndex);
        SavePlaylistConfig();
        UpdateStatusText(wxString::FromUTF8("已创建播放列表: ") + name);
    }
}

void MainFrame::OnDeletePlaylist(wxCommandEvent& event) {
    // 检查是否只有一个播放列表
    if (m_playlistManager.GetPlaylistCount() <= 1) {
        wxMessageBox(
            wxString::FromUTF8("至少需要保留一个播放列表"),
            wxString::FromUTF8("提示"),
            wxOK | wxICON_INFORMATION
        );
        return;
    }
    
    wxString currentName = m_playlistManager.GetCurrentPlaylist() 
        ? m_playlistManager.GetCurrentPlaylist()->name 
        : wxString::FromUTF8("当前列表");
    
    int result = wxMessageBox(
        wxString::Format(wxString::FromUTF8("确定要删除播放列表 \"%s\" 吗？\n该操作不可撤销。"), currentName),
        wxString::FromUTF8("删除播放列表"),
        wxYES_NO | wxICON_QUESTION
    );
    
    if (result == wxYES) {
        int currentIndex = m_playlistManager.GetCurrentPlaylistIndex();
        
        // 停止播放
        if (m_engine.is_playing()) {
            wxCommandEvent dummy;
            OnStop(dummy);
        }
        
        m_playlistManager.DeletePlaylist(currentIndex);
        UpdatePlaylistChoice();
        RefreshPlaylistUI();
        SavePlaylistConfig();
        UpdateStatusText(wxString::FromUTF8("已删除播放列表: ") + currentName);
    }
}

void MainFrame::OnRenamePlaylist(wxCommandEvent& event) {
    if (!m_playlistManager.GetCurrentPlaylist()) return;
    
    wxString oldName = m_playlistManager.GetCurrentPlaylist()->name;
    
    wxString newName = wxGetTextFromUser(
        wxString::FromUTF8("请输入新的播放列表名称:"),
        wxString::FromUTF8("重命名播放列表"),
        oldName,
        this
    );
    
    if (!newName.IsEmpty() && newName != oldName) {
        if (m_playlistManager.RenamePlaylist(m_playlistManager.GetCurrentPlaylistIndex(), newName)) {
            UpdatePlaylistChoice();
            SavePlaylistConfig();
            UpdateStatusText(wxString::FromUTF8("已重命名为: ") + newName);
        } else {
            wxMessageBox(
                wxString::FromUTF8("名称已存在或无效"),
                wxString::FromUTF8("重命名失败"),
                wxOK | wxICON_WARNING
            );
        }
    }
}

void MainFrame::UpdatePlaylistChoice() {
    m_playlistChoice->Clear();
    wxArrayString names = m_playlistManager.GetPlaylistNames();
    for (const auto& name : names) {
        m_playlistChoice->Append(name);
    }
    
    int currentIndex = m_playlistManager.GetCurrentPlaylistIndex();
    if (currentIndex >= 0 && currentIndex < m_playlistChoice->GetCount()) {
        m_playlistChoice->SetSelection(currentIndex);
    }
}

void MainFrame::RefreshPlaylistUI() {
    m_playlistCtrl->DeleteAllItems();
    
    // 从 PlaylistManager 获取文件列表
    m_playlist_files.clear();
    const auto& files = m_playlistManager.GetFiles();
    for (const auto& file : files) {
        m_playlist_files.push_back(file);
    }
    
    // 应用搜索过滤
    wxString keyword = m_searchCtrl->GetValue().Lower();
    bool hasSearch = !keyword.IsEmpty();
    
    long idx = 0;
    for (size_t i = 0; i < m_playlist_files.size(); ++i) {
        wxString path = m_playlist_files[i];
        wxString name = path.AfterLast('\\');
        
        if (!hasSearch || name.Lower().Contains(keyword)) {
            m_playlistCtrl->InsertItem(idx, name);
            m_playlistCtrl->SetItemData(idx, static_cast<long>(i));
            
            // 高亮当前播放的文件
            if (path == m_current_path) {
                m_playlistCtrl->SetItemState(idx, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
                m_current_play_index = static_cast<int>(idx);
            }
            
            idx++;
        }
    }
}

void MainFrame::SwitchToPlaylist(int index) {
    // 停止当前播放
    if (m_engine.is_playing()) {
        wxCommandEvent dummy;
        OnStop(dummy);
    }
    
    // 切换播放列表
    m_playlistManager.SetCurrentPlaylist(index);
    
    // 重置状态
    m_current_path = "";
    m_current_midi.reset();
    m_current_play_index = -1;
    
    // 更新UI
    m_playlistChoice->SetSelection(index);
    RefreshPlaylistUI();
    
    // 更新文件标签
    m_currentFileLabel->SetLabel(wxString::FromUTF8("未选择文件"));
    m_totalTimeLabel->SetLabel("00:00");
    m_currentTimeLabel->SetLabel("00:00");
    m_progressSlider->SetValue(0);
    GetStatusBar()->SetStatusText("BPM: --", 2);
    
    SavePlaylistConfig();
    
    wxString playlistName = m_playlistManager.GetCurrentPlaylist() 
        ? m_playlistManager.GetCurrentPlaylist()->name 
        : wxString::FromUTF8("播放列表");
    UpdateStatusText(wxString::FromUTF8("已切换到: ") + playlistName);
}

