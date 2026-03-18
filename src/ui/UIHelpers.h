#pragma once

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/tglbtn.h>
#include <wx/spinctrl.h>
#include <vector>
#include <string>
#include "../core/KeyboardSimulator.h"

// UI 常量字符串
namespace UIConstants {
    const wxString DEFAULT_WINDOW = wxString::FromUTF8("未选择");
    const wxString DEFAULT_TRACK = wxString::FromUTF8("全部音轨");
    const wxString DEFAULT_KEYMAP = wxString::FromUTF8("默认键位");
    
    // 播放模式
    const wxString MODE_SINGLE = wxString::FromUTF8("单曲播放");
    const wxString MODE_SINGLE_LOOP = wxString::FromUTF8("单曲循环");
    const wxString MODE_LIST = wxString::FromUTF8("列表播放");
    const wxString MODE_LIST_LOOP = wxString::FromUTF8("列表循环");
    const wxString MODE_RANDOM = wxString::FromUTF8("随机播放");
}

namespace UI {

// UI更新辅助类
class UIHelpers {
public:
    // 更新wxChoice控件的内容，保持当前选择
    static void UpdateChoiceItems(
        wxChoice* choice,
        const wxArrayString& items,
        const std::vector<void*>& clientData,
        bool keepSelection = true
    );

    // 格式化时间为 MM:SS
    static wxString FormatTime(int seconds);

};

// 通道控件更新辅助类
class ChannelUIUpdater {
public:
    struct ChannelUpdateInfo {
        wxChoice* windowChoice;
        wxChoice* trackChoice;
        wxToggleButton* enableBtn;
        wxSpinCtrl* transposeCtrl;
        int channelIndex;
    };

    // 批量更新窗口列表
    static void UpdateWindowLists(
        std::vector<ChannelUpdateInfo>& channels,
        const std::vector<Core::KeyboardSimulator::WindowInfo>& windowList
    );

    // 批量更新音轨列表
    static void UpdateTrackLists(
        std::vector<ChannelUpdateInfo>& channels,
        const wxArrayString& displayItems,
        const std::vector<int>& trackIndices
    );
};

} // namespace UI