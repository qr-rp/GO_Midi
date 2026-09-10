#include "KeymapEditorDialog.h"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/config.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <windows.h>  // VK_*

#include "../util/Logger.h"

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

    // 中部: 钢琴卷(内部自绘横向滚动, 可视宽固定 700, 扩大音域滚轮/拖动滚动条查看)
    m_roll = new PianoRollCtrl(this, m_minPitch, m_maxPitch);
    m_roll->SetViewWidth(FromDIP(700));
    root->Add(m_roll, 0, wxALIGN_CENTER | wxALL, 8);

    // 底部: 当前方案 + 提示
    wxBoxSizer* foot = new wxBoxSizer(wxHORIZONTAL);
    m_fileLabel = new wxStaticText(this, wxID_ANY, wxString());
    m_fileLabel->SetForegroundColour(wxColour(0x2a, 0xa0, 0x7a));
    foot->Add(m_fileLabel, 0, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    m_hint = new wxStaticText(this, wxID_ANY, wxString());
    m_hint->SetForegroundColour(wxColour(0x88, 0x92, 0xa0));
    foot->Add(m_hint, 1, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    root->Add(foot, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_roll->onStatus = [this](const wxString& t) { SetStatus(t); };
    // 绑定/清除同步回 KeyManager(否则只改显示不改实际键位)
    m_roll->onBind = [this](int note, int vk, int modifier) {
        if (!m_km) return;
        auto map = m_km->get_map();
        if (vk < 0) {
            map.erase(note);
        } else {
            map[note] = Util::KeyMapping{ vk, modifier };
        }
        m_km->set_map(map);
        Notify();
    };

    SetSizer(root);
    root->SetSizeHints(this);
    // 高度自适应内容(顶部+音域+钢琴卷+底部), 宽度固定 740
    Fit();
    // 给滚动条/边距额外余量, 避免滚动容器被压缩导致琴键截断
    SetSize(wxSize(FromDIP(740), std::max(GetSize().y, FromDIP(280))));
    CenterOnParent();
}

void KeymapEditorDialog::SetStatus(const wxString& text) {
    if (m_hint) m_hint->SetLabel(text);
}

void KeymapEditorDialog::SyncChoice() {
    if (!m_choice) return;
    m_choice->Clear();
    m_choice->Append(wxString::FromUTF8("默认键位"));       // 0: 内置 FF14
    m_choice->Append(wxString::FromUTF8("燕云十六声"));     // 1: 内置燕云
    for (const auto& name : m_keymapFiles) {
        m_choice->Append(name);
    }
    // 根据当前方案定位选择 (与主界面标识体系一致: @builtin_0=FF14, @builtin_1=燕云)
    if (m_currentPath.IsEmpty() || m_currentPath == "@builtin_0") {
        m_choice->SetSelection(0);   // 默认键位(内置 FF14)
    } else if (m_currentPath == "@builtin_1") {
        m_choice->SetSelection(1);   // 燕云十六声
    } else {
        int idx = 2;
        bool found = false;
        for (const auto& name : m_keymapFiles) {
            if (name == m_currentPath) { found = true; break; }
            ++idx;
        }
        m_choice->SetSelection(found ? idx : 0);
    }
}

void KeymapEditorDialog::SyncRoll() {
    if (m_km) m_roll->SetMap(m_km->get_map());
    // 当前方案标签
    wxString label;
    if (m_currentPath.IsEmpty() || m_currentPath == "@builtin_0") label = wxString::FromUTF8("当前: 内置键位 (FF14)");
    else if (m_currentPath == "@builtin_1") label = wxString::FromUTF8("当前: 内置键位 (燕云十六声)");
    else label = wxString::FromUTF8("当前方案: ") + m_currentPath.AfterLast('\\');
    if (m_fileLabel) m_fileLabel->SetLabel(label);
    SetStatus(wxString::FromUTF8("点击琴键 → 按下新键即绑定 (Esc 取消) · 右键清除绑定"));
    Layout();   // 钢琴卷宽随显示范围变化, 重新布局
}

void KeymapEditorDialog::Notify() {
    if (m_onChanged) m_onChanged();
}

void KeymapEditorDialog::OnChoice(wxCommandEvent& event) {
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
            if (ReadSchemeMap(name, schemeMap)) {
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
    Layout();
    // #3: 音域写 config 去抖(300ms 尾沿), 持续拖拽不写盘, 停顿后落盘一次
    m_pitchSaveTimer.Start(300, wxTIMER_ONE_SHOT);
    SetStatus(wxString::Format("目标音域: %d – %d", minP, maxP));
    Notify();
}

void KeymapEditorDialog::OnPitchSaveTimer(wxTimerEvent& event) {
    // 去抖落盘(引擎侧已通过 Notify 即时生效, 这里只负责持久化)
    WriteSchemePitch(m_currentPath, m_minPitch, m_maxPitch);
}

void KeymapEditorDialog::OnDialogClose(wxCloseEvent& event) {
    // 冲刷去抖中的音域写盘, 避免关窗丢失最后改动
    WriteSchemePitch(m_currentPath, m_minPitch, m_maxPitch);
    event.Skip();
}

void KeymapEditorDialog::RestorePitchRange() {
    int minP = 48, maxP = 84;
    ReadSchemePitch(m_currentPath, minP, maxP);
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
    WriteSchemeMap(uniqueName, newMap);
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
    if (!ReadSchemeMap(oldName, schemeMap)) {
        SetStatus(wxString::FromUTF8("重命名失败: 方案数据缺失"));
        return;
    }
    wxString uniqueName = GenerateUniqueName(newName);
    m_keymapFiles[fileIdx] = uniqueName;
    if (m_currentPath == oldName) m_currentPath = uniqueName;
    WriteSchemeMap(uniqueName, schemeMap);
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
    WriteSchemeMap(uniqueName, m_km->get_map());
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
long KeymapEditorDialog::FindIndex(const wxString& name) const {
    for (size_t i = 0; i < m_keymapFiles.size(); ++i) {
        if (m_keymapFiles[i] == name) return static_cast<long>(i);
    }
    return -1;
}

bool KeymapEditorDialog::ReadSchemeMap(const wxString& name, std::map<int, Util::KeyMapping>& out) const {
    if (!m_config) return false;
    long idx = FindIndex(name);
    if (idx < 0) return false;
    wxString groupPath = wxString::Format("/KeymapSchemes/List_%ld", idx);
    if (!m_config->HasGroup(groupPath)) return false;
    m_config->SetPath(groupPath);
    long noteCount = 0;
    m_config->Read("NoteCount", &noteCount, 0L);
    for (long n = 0; n < noteCount; ++n) {
        wxString val;
        if (m_config->Read(wxString::Format("Note%ld", n), &val)) {
            long pitch = 0, vk = 0, mod = 0;
            wxString rest = val;
            if (!rest.BeforeFirst(',').ToLong(&pitch)) continue;
            rest = rest.AfterFirst(',');
            if (!rest.BeforeFirst(',').ToLong(&vk)) continue;
            rest = rest.AfterFirst(',');
            if (!rest.ToLong(&mod)) continue;
            out[static_cast<int>(pitch)] = {static_cast<int>(vk), static_cast<int>(mod)};
        }
    }
    m_config->SetPath("/");
    return true;
}

void KeymapEditorDialog::WriteSchemeMap(const wxString& name, const std::map<int, Util::KeyMapping>& map) const {
    if (!m_config) return;
    // 确保方案在列表中有槽位（列表顺序 = config 顺序）
    long idx = FindIndex(name);
    if (idx < 0) return;
    wxString groupPath = wxString::Format("/KeymapSchemes/List_%ld", idx);
    m_config->SetPath(groupPath);
    m_config->Write("Name", name);
    m_config->Write("NoteCount", static_cast<long>(map.size()));
    long noteIdx = 0;
    for (const auto& pair : map) {
        wxString noteKey = wxString::Format("Note%ld", noteIdx++);
        wxString noteVal = wxString::Format("%d,%d,%d", pair.first, pair.second.vk_code, pair.second.modifier);
        m_config->Write(noteKey, noteVal);
    }
    m_config->SetPath("/");
}

bool KeymapEditorDialog::ReadSchemePitch(const wxString& name, int& minP, int& maxP) const {
    minP = 48; maxP = 84;   // 默认(FF14 内置音域)
    if (!m_config) return false;
    wxString key;
    if (name.IsEmpty() || name == "@builtin_0") {
        // 内置 FF14 (空标识兼容旧数据)
        key = "/Global/FF14Pitch";
        if (!m_config->HasEntry(key)) {
            m_config->Read("/Global/MinPitch", &minP, 48);   // 兼容旧版全局音域
            m_config->Read("/Global/MaxPitch", &maxP, 84);
            return true;
        }
    } else if (name.StartsWith("@builtin_")) {
        // 内置燕云(@builtin_1)等
        key = "/Global/YYPitch";
        minP = 48; maxP = 83;   // 燕云十六声内置音域
    } else {
        long idx = FindIndex(name);
        if (idx < 0) return false;
        key = wxString::Format("/KeymapSchemes/List_%ld/Pitch", idx);
    }
    if (!m_config->HasEntry(key)) {
        // 无已保存音域: 返回内置默认值, 不视为失败
        return false;
    }
    m_config->Read(key, &minP, 48);
    m_config->Read(key + "Max", &maxP, 84);
    return true;
}

void KeymapEditorDialog::WriteSchemePitch(const wxString& name, int minP, int maxP) const {
    if (!m_config) return;
    wxString key;
    if (name.IsEmpty() || name == "@builtin_0") {
        key = "/Global/FF14Pitch";
    } else if (name.StartsWith("@builtin_")) {
        key = "/Global/YYPitch";
    } else {
        long idx = FindIndex(name);
        if (idx < 0) return;
        key = wxString::Format("/KeymapSchemes/List_%ld/Pitch", idx);
    }
    m_config->Write(key, minP);
    m_config->Write(key + "Max", maxP);
    m_config->Flush();
}

wxString KeymapEditorDialog::GenerateUniqueName(const wxString& base) const {
    wxString name = base;
    int counter = 0;
    while (FindIndex(name) >= 0) {
        name = wxString::Format("%s (%d)", base, ++counter);
    }
    return name;
}

void KeymapEditorDialog::OnRollStatus(const wxString& text) {
    SetStatus(text);
}

} // namespace UI
