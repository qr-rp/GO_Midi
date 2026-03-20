#include "UIHelpers.h"

namespace UI {

void UIHelpers::UpdateChoiceItems(
    wxChoice* choice,
    const wxArrayString& items,
    const std::vector<void*>& clientData,
    bool keepSelection
) {
    if (!choice) return;

    // 优先通过 clientData (HWND/trackIndex) 恢复选择，避免字符串匹配因 PID 变化导致失败
    void* currentData = nullptr;
    if (keepSelection) {
        int currentSel = choice->GetSelection();
        if (currentSel != wxNOT_FOUND && currentSel > 0) {
            currentData = choice->GetClientData(currentSel);
        }
    }
    
    choice->Freeze();
    choice->Clear();
    
    for (size_t i = 0; i < items.GetCount(); ++i) {
        void* data = (i < clientData.size()) ? clientData[i] : nullptr;
        choice->Append(items[i], data);
    }
    
    // 通过 clientData 恢复选择
    if (keepSelection && currentData != nullptr) {
        bool found = false;
        for (size_t i = 0; i < choice->GetCount(); ++i) {
            if (choice->GetClientData(i) == currentData) {
                choice->SetSelection(i);
                found = true;
                break;
            }
        }
        if (!found) {
            choice->SetSelection(0);
        }
    } else {
        choice->SetSelection(0);
    }
    
    choice->Thaw();
}

wxString UIHelpers::FormatTime(int seconds) {
    return wxString::Format("%02d:%02d", seconds / 60, seconds % 60);
}


void ChannelUIUpdater::UpdateWindowLists(
    std::vector<ChannelUpdateInfo>& channels,
    const std::vector<Core::KeyboardSimulator::WindowInfo>& windowList
) {
    wxArrayString items;
    std::vector<void*> clientData;
    
    items.Add(UIConstants::DEFAULT_WINDOW);
    clientData.push_back(nullptr);
    
    // 添加窗口到列表
    for (const auto& win : windowList) {
        wxString label = wxString::Format("%s(%lu)", wxString::FromUTF8(win.title), win.pid);
        items.Add(label);
        clientData.push_back(win.hwnd);
    }
    
    for (auto& ch : channels) {
        UIHelpers::UpdateChoiceItems(ch.windowChoice, items, clientData, true);
    }
}

void ChannelUIUpdater::UpdateTrackLists(
    std::vector<ChannelUpdateInfo>& channels,
    const wxArrayString& displayItems,
    const std::vector<int>& trackIndices
) {
    for (auto& ch : channels) {
        if (!ch.trackChoice) continue;
        
        std::vector<void*> clientData;
        for (size_t i = 0; i < trackIndices.size(); ++i) {
            clientData.push_back((void*)(intptr_t)trackIndices[i]);
        }
        
        UIHelpers::UpdateChoiceItems(ch.trackChoice, displayItems, clientData, true);
    }
}

} // namespace UI