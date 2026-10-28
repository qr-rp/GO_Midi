#pragma once

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <functional>
#include <string>
#include <vector>

class wxConfigBase;  // 前向声明（global namespace）

#include "PianoRollCtrl.h"
#include "../core/PlaybackEngine.h"

namespace UI {

struct KeymapEditorCallbacks {
    std::function<void()> onChanged;
    std::function<void()> onPersist;
};

/// 键位编辑弹窗(对齐 next 分支设计):
/// 顶部 = 方案下拉 + ＋新建 / －删除 / 加载 / 导出 (关闭走系统标题栏 ✕);
/// 中部 = 钢琴卷(点击琴键绑定);底部 = 当前方案文件 + 操作提示。
class KeymapEditorDialog : public wxDialog {
public:
    KeymapEditorDialog(wxWindow* parent, Core::PlaybackEngine* engine,
                       wxConfigBase* config,
                       std::vector<wxString>& keymapFiles,
                       wxString& currentPath,
                       KeymapEditorCallbacks callbacks);

private:
    Core::PlaybackEngine* m_engine;
    wxConfigBase* m_config;            // config 读写（方案持久化）
    Util::KeyManager* m_km;
    std::vector<wxString>& m_keymapFiles;  // 引用 MainFrame 的自定义方案名列表
    wxString& m_currentPath;               // 引用 MainFrame 的当前方案标识(空=内置FF14, @builtin_N=内置, 或方案名)
    std::function<void()> m_onChanged;
    std::function<void()> m_onPersist;

    wxChoice* m_choice = nullptr;
    PianoRollCtrl* m_roll = nullptr;
    wxStaticText* m_fileLabel = nullptr;
    wxStaticText* m_hint = nullptr;
    wxStaticText* m_stats = nullptr;
    wxSpinCtrl* m_minSpin = nullptr;   // 目标音域下界
    wxSpinCtrl* m_maxSpin = nullptr;   // 目标音域上界
    wxTimer m_configSaveTimer;

    int m_minPitch = 48;  // 默认 C3
    int m_maxPitch = 84;  // 默认 C6

    void BuildUI();
    void SyncChoice();                // 重建下拉(内置2 + 自定义)
    void SyncRoll();                  // 把 KeyManager 当前 map 刷到钢琴卷
    void Notify();
    void SchedulePersist();
    void PersistNow();
    void RestorePitchRange();         // 按当前方案恢复音域(spins+钢琴卷)
    wxString VkName(int vk, int mod) const;
    void SetStatus(const wxString& text);
    void UpdateStats();

    void OnChoice(wxCommandEvent& event);
    void OnPitchChange(wxSpinEvent& event);  // 音域调整 → 去抖写当前方案
    void OnConfigSaveTimer(wxTimerEvent& event);
    void OnDialogClose(wxCloseEvent& event);
    void OnNew(wxCommandEvent& event);     // ＋ 新建键位方案(复制当前键位存 config)
    void OnRename(wxCommandEvent& event);  // 重命名当前自定义方案
    void OnDelete(wxCommandEvent& event);  // － 删除当前自定义方案
    void OnLoad(wxCommandEvent& event);    // 加载(导入 txt → 存为 config 方案)
    void OnExport(wxCommandEvent& event);  // 导出(纯写文件,不改当前方案)
    void OnRollStatus(const wxString& text);

    wxString GenerateUniqueName(const wxString& base) const;

    wxDECLARE_EVENT_TABLE();
};

} // namespace UI
