#include "UIHelpers.h"

namespace UI {

void UIHelpers::UpdateChoiceItems(
    wxChoice* choice,
    const wxArrayString& items,
    const std::vector<void*>& clientData,
    bool keepSelection
) {
    if (!choice) return;

    wxString currentSel = keepSelection ? choice->GetStringSelection() : "";
    
    choice->Freeze();
    choice->Clear();
    
    for (size_t i = 0; i < items.GetCount(); ++i) {
        void* data = (i < clientData.size()) ? clientData[i] : nullptr;
        choice->Append(items[i], data);
    }
    
    if (keepSelection && !currentSel.IsEmpty() && choice->FindString(currentSel) != wxNOT_FOUND) {
        choice->SetStringSelection(currentSel);
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
    
    items.Add(wxString::FromUTF8("未选择"));
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