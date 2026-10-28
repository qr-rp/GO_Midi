#include "KeymapEditorDialog.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/config.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <windows.h>  // VK_*

#include "../util/Logger.h"
#include "KeymapSchemeConfig.h"
#include "UIHelpers.h"

namespace UI {

enum {
    ID_KM_CHOICE = 3001,
    ID_KM_NEW,
    ID_KM_RENAME,
    ID_KM_DEL,
    ID_KM_LOAD,
    ID_KM_EXPORT,
    ID_KM_MIN_PITCH,
    ID_KM_MAX_PITCH,
    ID_KM_PITCH_SAVE_TIMER,
};

wxBEGIN_EVENT_TABLE(KeymapEditorDialog, wxDialog)
    EVT_CHOICE(ID_KM_CHOICE, KeymapEditorDialog::OnChoice)
    EVT_BUTTON(ID_KM_NEW, KeymapEditorDialog::OnNew)
    EVT_BUTTON(ID_KM_RENAME, KeymapEditorDialog::OnRename)
    EVT_BUTTON(ID_KM_DEL, KeymapEditorDialog::OnDelete)
    EVT_BUTTON(ID_KM_LOAD, KeymapEditorDialog::OnLoad)
    EVT_BUTTON(ID_KM_EXPORT, KeymapEditorDialog::OnExport)
    EVT_SPINCTRL(ID_KM_MIN_PITCH, KeymapEditorDialog::OnPitchChange)
    EVT_SPINCTRL(ID_KM_MAX_PITCH, KeymapEditorDialog::OnPitchChange)
    EVT_TIMER(ID_KM_PITCH_SAVE_TIMER, KeymapEditorDialog::OnPitchSaveTimer)
    EVT_CLOSE(KeymapEditorDialog::OnDialogClose)
wxEND_EVENT_TABLE()

KeymapEditorDialog::KeymapEditorDialog(wxWindow* parent, Core::PlaybackEngine* engine,
                                       wxConfigBase* config,
                                       std::vector<wxString>& keymapFiles,
                                       wxString& currentPath,
                                       std::function<void()> onChanged)
    : wxDialog(parent, wxID_ANY, wxString::FromUTF8("键位设置"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_engine(engine), m_config(config), m_km(nullptr),
      m_keymapFiles(keymapFiles), m_currentPath(currentPath),
      m_onChanged(std::move(onChanged)) {
    if (m_engine) m_km = &m_engine->get_key_manager();
    m_pitchSaveTimer.SetOwner(this, ID_KM_PITCH_SAVE_TIMER);
    BuildUI();
    SyncChoice();
    // 恢复当前方案音域
    RestorePitchRange();
    SyncRoll();
    m_roll->SetFocus();
}

void KeymapEditorDialog::BuildUI() {
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    // 顶部: 方案下拉 + 按钮组
    wxBoxSizer* top = new wxBoxSizer(wxHORIZONTAL);
    m_choice = new wxChoice(this, ID_KM_CHOICE);
    m_choice->SetMinSize(FromDIP(wxSize(150, -1)));
    top->Add(m_choice, 1, wxALL | wxALIGN_CENTER_VERTICAL, 6);

    wxButton* newBtn = new wxButton(this, ID_KM_NEW, L"＋");
    wxButton* renameBtn = new wxButton(this, ID_KM_RENAME, wxString::FromUTF8("重命名"));
    wxButton* delBtn = new wxButton(this, ID_KM_DEL, L"－");
    wxButton* loadBtn = new wxButton(this, ID_KM_LOAD, wxString::FromUTF8("加载"));
    wxButton* exportBtn = new wxButton(this, ID_KM_EXPORT, wxString::FromUTF8("导出"));

    wxButton* btns[] = { newBtn, delBtn, renameBtn, loadBtn, exportBtn };
    for (auto* b : btns) {
        b->SetMinSize(FromDIP(wxSize(42, 28)));
        top->Add(b, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    }
    newBtn->SetToolTip(wxString::FromUTF8("新建键位方案(空键位,存为 config 内方案)"));
    renameBtn->SetToolTip(wxString::FromUTF8("重命名当前自定义方案"));
    delBtn->SetToolTip(wxString::FromUTF8("删除当前自定义方案(恢复内置,不删文件)"));
    loadBtn->SetToolTip(wxString::FromUTF8("导入键位文件"));
    exportBtn->SetToolTip(wxString::FromUTF8("导出当前键位到文件(不改当前方案)"));
    root->Add(top, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 8);

    // 音域行: 每个方案独立绑定
    wxBoxSizer* pitchRow = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* pitchLabel = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("目标音域:"));
    m_minSpin = new wxSpinCtrl(this, ID_KM_MIN_PITCH, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS | wxTE_CENTRE, 0, 127, m_minPitch);
    wxStaticText* dash = new wxStaticText(this, wxID_ANY, L"–");
    m_maxSpin = new wxSpinCtrl(this, ID_KM_MAX_PITCH, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxSP_ARROW_KEYS | wxTE_CENTRE, 0, 127, m_maxPitch);
    m_minSpin->SetMinSize(FromDIP(wxSize(56, 24)));
    m_maxSpin->SetMinSize(FromDIP(wxSize(56, 24)));
    wxStaticText* pitchTip = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("(切换方案时自动恢复,各方案独立)"));
    pitchTip->SetForegroundColour(wxColour(0x88, 0x92, 0xa0));
    pitchRow->Add(pitchLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    pitchRow->Add(m_minSpin, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    pitchRow->Add(dash, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    pitchRow->Add(m_maxSpin, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    pitchRow->Add(pitchTip, 0, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    root->Add(pitchRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    // 中部: 钢琴卷随窗口横向伸展; 空间不足时使用内部滚动条
    m_roll = new PianoRollCtrl(this, m_minPitch, m_maxPitch);
    root->Add(m_roll, 1, wxEXPAND | wxALL, 8);

    // 底部: 当前方案 + 提示
    wxBoxSizer* foot = new wxBoxSizer(wxHORIZONTAL);
    m_fileLabel = new wxStaticText(this, wxID_ANY, wxString());
    m_fileLabel->SetForegroundColour(wxColour(0x2a, 0xa0, 0x7a));
    foot->Add(m_fileLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    m_hint = new wxStaticText(this, wxID_ANY, wxString());
    m_hint->SetForegroundColour(wxColour(0x88, 0x92, 0xa0));
    foot->Add(m_hint, 1, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    m_stats = new wxStaticText(this, wxID_ANY, wxString());
    m_stats->SetForegroundColour(wxColour(0x88, 0x92, 0xa0));
    foot->Add(m_stats, 0, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    root->Add(foot, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_roll->onStatus = [this](const wxString& t) { SetStatus(t); };
    // 绑定、清除、撤销和重做都同步回 KeyManager。
    m_roll->onMapChanged = [this](const std::map<int, Util::KeyMapping>& map) {
        if (!m_km) return;
        m_km->set_map(map);
        UpdateStats();
        Notify();
    };

    SetSizer(root);
    root->SetSizeHints(this);
    // 高度自适应内容; 初始宽度 740, 用户可继续拉宽以显示更多音域
    Fit();
    SetSize(wxSize(FromDIP(740), std::max(GetSize().y, FromDIP(280))));
    SetMinSize(wxSize(FromDIP(520), GetMinSize().y));
    CenterOnParent();
}

void KeymapEditorDialog::SetStatus(const wxString& text) {
    if (m_hint) m_hint->SetLabel(text);
}

void KeymapEditorDialog::UpdateStats() {
    if (!m_stats || !m_roll)
        return;
    const auto& analysis = m_roll->GetBindingAnalysis();
    m_stats->SetLabel(wxString::Format(
        wxString::FromUTF8("缺失 %d · 逻辑 %d · 物理 %d · 域外 %d"),
        analysis.unbound_in_range,
        static_cast<int>(analysis.logical_duplicates.size()),
        static_cast<int>(analysis.physical_conflicts.size()),
        static_cast<int>(analysis.out_of_range.size())));
    m_stats->SetToolTip(wxString::FromUTF8(
        "缺失: 目标音域内未绑定音符\n"
        "逻辑: 相同主键+修饰符的重复绑定\n"
        "物理: 相同主键但修饰符不同的冲突\n"
        "域外: 目标音域外的旧绑定"));
    m_stats->SetForegroundColour(
        !analysis.physical_conflicts.empty()
            ? wxColour(0xe0, 0x6a, 0x6a)
            : (!analysis.logical_duplicates.empty()
                ? wxColour(0xd6, 0xa5, 0x45)
                : wxColour(0x88, 0x92, 0xa0)));
    Layout();
}

void KeymapEditorDialog::SyncChoice() {
    if (!m_choice)
        return;
    KeymapSchemeStore::populate_choice(
        m_choice, m_keymapFiles, m_currentPath,
        UIConstants::DEFAULT_KEYMAP, UIConstants::KEYMAP_YYSLS);
}

void KeymapEditorDialog::SyncRoll() {
    if (m_km) m_roll->SetMap(m_km->get_map());
    // 当前方案标签
    wxString label;
    if (m_currentPath.IsEmpty() || m_currentPath == "@builtin_0") label = wxString::FromUTF8("当前: 内置键位 (FF14)");
    else if (m_currentPath == "@builtin_1") label = wxString::FromUTF8("当前: 内置键位 (燕云十六声)");
    else label = wxString::FromUTF8("当前方案: ") + m_currentPath.AfterLast('\\');
    if (m_fileLabel) m_fileLabel->SetLabel(label);
    UpdateStats();
    SetStatus(wxString::FromUTF8(
        "点击琴键绑定 · 未选中时 ←/→ 浏览 · Ctrl+Z 撤销 · Esc 取消后右键清除"));
    Layout();   // 钢琴卷随窗口宽度伸展, 内容变化时重新布局
}

void KeymapEditorDialog::Notify() {
    if (m_onChanged) m_onChanged();
}

void KeymapEditorDialog::OnChoice(wxCommandEvent&) {
    int sel = m_choice->GetSelection();
    if (sel < 0) return;
    if (sel == 0) {
        m_currentPath = wxString("@builtin_0");   // 与主界面标识一致, 内置 FF14
        if (m_km) m_km->reset_to_default();
        SetStatus(wxString::FromUTF8("已切换到默认键位"));
    } else if (sel == 1) {
        m_currentPath = wxString("@builtin_1");   // 与主界面标识一致, 内置燕云
        if (m_km) m_km->load_yysls_preset();
        SetStatus(wxString::FromUTF8("已切换到燕云十六声键位"));
    } else {
        size_t fileIdx = static_cast<size_t>(sel - 2);
        if (fileIdx < m_keymapFiles.size()) {
            const wxString& name = m_keymapFiles[fileIdx];
            std::map<int, Util::KeyMapping> schemeMap;
            if (KeymapSchemeStore::read_scheme_map(
                    m_config, m_keymapFiles, name, schemeMap))
            {
                if (m_km) m_km->set_map(schemeMap);
                m_currentPath = name;
                SetStatus(wxString::FromUTF8("已加载: ") + name);
            } else {
                SetStatus(wxString::FromUTF8("键位加载失败"));
            }
        }
    }
    // 恢复该方案音域
    RestorePitchRange();
    SyncRoll();
    Notify();
}

void KeymapEditorDialog::OnPitchChange(wxSpinEvent& event) {
    int minP = m_minSpin ? m_minSpin->GetValue() : 48;
    int maxP = m_maxSpin ? m_maxSpin->GetValue() : 84;
    // 保证 min <= max
    if (minP > maxP) {
        if (event.GetId() == ID_KM_MIN_PITCH) maxP = minP;
        else minP = maxP;
        if (m_minSpin) m_minSpin->SetValue(minP);
        if (m_maxSpin) m_maxSpin->SetValue(maxP);
    }
    m_minPitch = minP; m_maxPitch = maxP;
    m_roll->SetPitchRange(minP, maxP);
    UpdateStats();
    Layout();
    // #3: 音域写 config 去抖(300ms 尾沿), 持续拖拽不写盘, 停顿后落盘一次
    m_pitchSaveTimer.Start(300, wxTIMER_ONE_SHOT);
    SetStatus(wxString::Format("目标音域: %d – %d", minP, maxP));
    Notify();
}

void KeymapEditorDialog::OnPitchSaveTimer(wxTimerEvent&) {
    // 去抖落盘(引擎侧已通过 Notify 即时生效, 这里只负责持久化)
    FlushPitchSave();
}

void KeymapEditorDialog::FlushPitchSave() {
    KeymapSchemeStore::write_scheme_pitch(
        m_config, m_keymapFiles, m_currentPath, m_minPitch, m_maxPitch);
    if (m_config)
        m_config->Flush();
}

void KeymapEditorDialog::OnDialogClose(wxCloseEvent& event) {
    // 冲刷去抖中的音域写盘, 避免关窗丢失最后改动
    FlushPitchSave();
    event.Skip();
}

void KeymapEditorDialog::RestorePitchRange() {
    int minP = 48, maxP = 84;
    KeymapSchemeStore::read_scheme_pitch(
        m_config, m_keymapFiles, m_currentPath, minP, maxP);
    m_minPitch = minP; m_maxPitch = maxP;
    m_roll->SetPitchRange(minP, maxP);
    if (m_minSpin) m_minSpin->SetValue(minP);
    if (m_maxSpin) m_maxSpin->SetValue(maxP);
}

void KeymapEditorDialog::OnNew(wxCommandEvent&) {
    if (!m_km) return;
    wxString name = wxGetTextFromUser(
        wxString::FromUTF8("请输入新键位方案的名称:"),
        wxString::FromUTF8("新建键位方案"),
        wxString::FromUTF8("新键位"),
        this);
    if (name.IsEmpty()) return;
    // 新建方案为空键位（config 内管理，不新建文件，也不复制当前）
    std::map<int, Util::KeyMapping> newMap;
    wxString uniqueName = GenerateUniqueName(name);
    KeymapSchemeStore::write_scheme_map(
        m_config, m_keymapFiles, uniqueName, newMap);
    m_keymapFiles.push_back(uniqueName);
    m_currentPath = uniqueName;
    if (m_km) m_km->set_map(newMap);   // 当前键位切到空方案, 从零开始绑定
    SyncChoice();
    SyncRoll();
    RestorePitchRange();   // #4: 新方案音域重置为默认 48-84, 与引擎侧一致
    SetStatus(wxString::FromUTF8("已新建键位方案: ") + uniqueName);
    Notify();
}

void KeymapEditorDialog::OnRename(wxCommandEvent&) {
    int sel = m_choice->GetSelection();
    if (sel < 2) {
        SetStatus(wxString::FromUTF8("内置键位不可重命名"));
        return;
    }
    size_t fileIdx = static_cast<size_t>(sel - 2);
    if (fileIdx >= m_keymapFiles.size()) return;
    const wxString oldName = m_keymapFiles[fileIdx];
    wxString newName = wxGetTextFromUser(
        wxString::FromUTF8("请输入新的方案名称:"),
        wxString::FromUTF8("重命名键位方案"),
        oldName,
        this);
    if (newName.IsEmpty() || newName == oldName) return;
    // 重命名：先读旧方案数据，改名后写回（config 内迁移）
    std::map<int, Util::KeyMapping> schemeMap;
    if (!KeymapSchemeStore::read_scheme_map(
            m_config, m_keymapFiles, oldName, schemeMap))
    {
        SetStatus(wxString::FromUTF8("重命名失败: 方案数据缺失"));
        return;
    }
    wxString uniqueName = GenerateUniqueName(newName);
    m_keymapFiles[fileIdx] = uniqueName;
    if (m_currentPath == oldName) m_currentPath = uniqueName;
    KeymapSchemeStore::write_scheme_map(
        m_config, m_keymapFiles, uniqueName, schemeMap);
    SyncChoice();
    SyncRoll();
    SetStatus(wxString::FromUTF8("已重命名: ") + oldName + wxString::FromUTF8(" → ") + uniqueName);
    Notify();
}

void KeymapEditorDialog::OnDelete(wxCommandEvent&) {
    int sel = m_choice->GetSelection();
    // 仅当选中自定义方案时才删除（内置方案无可删除）
    if (sel < 2) {
        SetStatus(wxString::FromUTF8("当前为内置键位, 无可删除"));
        return;
    }
    size_t fileIdx = static_cast<size_t>(sel - 2);
    if (fileIdx >= m_keymapFiles.size()) return;
    // #8: 显式删除该方案在 config 中的槽位(索引失效前)
    if (m_config) {
        m_config->DeleteGroup(wxString::Format("/KeymapSchemes/List_%ld", static_cast<long>(fileIdx)));
    }
    m_keymapFiles.erase(m_keymapFiles.begin() + fileIdx);
    m_currentPath.Clear();
    if (m_km) m_km->reset_to_default();
    SyncChoice();
    SyncRoll();
    RestorePitchRange();   // #4: 恢复默认键位音域, 与主界面 ApplySchemePitch 一致
    SetStatus(wxString::FromUTF8("自定义键位方案已删除, 已恢复默认"));
    Notify();
}

void KeymapEditorDialog::OnLoad(wxCommandEvent&) {
    if (!m_km) return;
    wxFileDialog dlg(this, wxString::FromUTF8("导入键位配置"), L"", L"",
                     wxString::FromUTF8("键位配置文件 (*.txt)|*.txt"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() != wxID_OK) return;
    wxString path = dlg.GetPath();
    if (!m_km->load_config(path.ToStdWstring())) {
        SetStatus(wxString::FromUTF8("键位导入失败"));
        return;
    }
    // 导入内容存为 config 内新方案
    wxString baseName = path.AfterLast('\\').BeforeLast('.');
    wxString uniqueName = GenerateUniqueName(baseName);
    KeymapSchemeStore::write_scheme_map(
        m_config, m_keymapFiles, uniqueName, m_km->get_map());
    m_keymapFiles.push_back(uniqueName);
    m_currentPath = uniqueName;
    SyncChoice();
    SyncRoll();
    SetStatus(wxString::FromUTF8("键位已导入: ") + uniqueName);
    Notify();
}

void KeymapEditorDialog::OnExport(wxCommandEvent&) {
    if (!m_km) return;
    wxFileDialog dlg(this, wxString::FromUTF8("导出键位配置"), L"", L"keymap.txt",
                     wxString::FromUTF8("键位配置文件 (*.txt)|*.txt"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() != wxID_OK) return;
    if (m_km->save_config(dlg.GetPath().ToStdWstring())) {
        SetStatus(wxString::FromUTF8("键位已导出"));
    } else {
        SetStatus(wxString::FromUTF8("键位导出失败"));
    }
}

// ---- config 内方案读写辅助 ----
wxString KeymapEditorDialog::GenerateUniqueName(const wxString& base) const {
    wxString name = base;
    int counter = 0;
    while (KeymapSchemeStore::find_scheme_index(m_keymapFiles, name) >= 0) {
        name = wxString::Format("%s (%d)", base, ++counter);
    }
    return name;
}

void KeymapEditorDialog::OnRollStatus(const wxString& text) {
    SetStatus(text);
}

} // namespace UI
