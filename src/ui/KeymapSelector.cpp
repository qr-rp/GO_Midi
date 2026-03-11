#include "KeymapSelector.h"

namespace UI {

// ================= KeymapPopup Implementation =================

wxBEGIN_EVENT_TABLE(KeymapPopup, wxComboPopup)
    EVT_LISTBOX(wxID_ANY, KeymapPopup::OnListSelect)
    EVT_LISTBOX_DCLICK(wxID_ANY, KeymapPopup::OnListDClick)
wxEND_EVENT_TABLE()

bool KeymapPopup::Create(wxWindow* parent)
{
    m_list = new wxListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           0, nullptr, wxLB_SINGLE | wxLB_NEEDED_SB);
    m_list->Bind(wxEVT_MOTION, &KeymapPopup::OnMouseMove, this);
    m_list->Bind(wxEVT_LEFT_DOWN, &KeymapPopup::OnMouseClick, this);
    return true;
}

void KeymapPopup::SetStringValue(const wxString& value)
{
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].name == value) {
            m_selected = static_cast<int>(i);
            m_list->SetSelection(m_selected);
            return;
        }
    }
}

wxString KeymapPopup::GetStringValue() const
{
    if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size())) {
        return m_items[m_selected].name;
    }
    return wxString();
}

wxSize KeymapPopup::GetAdjustedSize(int minWidth, int prefHeight, int maxHeight)
{
    wxSize best = m_list->GetBestSize();
    best.x = wxMax(minWidth, best.x);
    best.y = wxMin(maxHeight, wxMax(50, best.y));
    return best;
}

void KeymapPopup::OnPopup()
{
    m_list->Clear();
    for (const auto& item : m_items) {
        if (item.isDefault) {
            m_list->Append(item.name);
        } else {
            m_list->Append(item.name + wxString::FromUTF8("  ✕"));
        }
    }
    if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size())) {
        m_list->SetSelection(m_selected);
    }
}

void KeymapPopup::SetItems(const std::vector<KeymapItem>& items)
{
    m_items = items;
}

void KeymapPopup::SetSelection(int index)
{
    m_selected = index;
    if (m_list && index >= 0 && index < static_cast<int>(m_items.size())) {
        m_list->SetSelection(index);
    }
}

void KeymapPopup::RemoveItem(int index)
{
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        m_items.erase(m_items.begin() + index);
        if (m_selected >= index) {
            m_selected = wxMax(0, m_selected - 1);
        }
    }
}

void KeymapPopup::OnListSelect(wxCommandEvent& event)
{
    m_selected = m_list->GetSelection();
}

void KeymapPopup::OnListDClick(wxCommandEvent& event)
{
    // 双击确认选择
    Dismiss();
    if (m_onSelect && m_selected >= 0) {
        m_onSelect(m_selected);
    }
}

void KeymapPopup::OnMouseMove(wxMouseEvent& event)
{
    // 可以在这里添加悬停效果
    event.Skip();
}

void KeymapPopup::OnMouseClick(wxMouseEvent& event)
{
    int item = m_list->HitTest(event.GetPosition());
    if (item == wxNOT_FOUND) {
        event.Skip();
        return;
    }

    // 检查是否点击了删除区域（右侧）
    if (item >= 0 && item < static_cast<int>(m_items.size())) {
        const auto& itemData = m_items[item];
        if (!itemData.isDefault) {
            // 计算删除按钮区域
            wxRect rect = m_list->GetItemRect(item);
            int deleteZoneStart = rect.GetRight() - 30; // 右侧30像素为删除区域

            if (event.GetX() >= deleteZoneStart) {
                // 点击了删除按钮
                if (m_onDelete) {
                    m_onDelete(item);
                }
                return;
            }
        }
    }

    // 否则正常选择
    event.Skip();
    m_selected = item;
    Dismiss();
    if (m_onSelect && m_selected >= 0) {
        m_onSelect(m_selected);
    }
}

// ================= KeymapSelector Implementation =================

wxBEGIN_EVENT_TABLE(KeymapSelector, wxComboCtrl)
wxEND_EVENT_TABLE()

KeymapSelector::KeymapSelector(wxWindow* parent, wxWindowID id)
    : wxComboCtrl(parent, id, wxString::FromUTF8("默认键位"),
                  wxDefaultPosition, wxDefaultSize, wxCB_READONLY)
{
    m_popup = new KeymapPopup();
    SetPopupControl(m_popup);
}

void KeymapSelector::SetItems(const std::vector<KeymapItem>& items)
{
    m_items = items;
    m_popup->SetItems(items);
    UpdateLabelText();
}

int KeymapSelector::GetSelection() const
{
    return m_popup->GetSelection();
}

void KeymapSelector::SetSelection(int index)
{
    m_popup->SetSelection(index);
    UpdateLabelText();
}

wxString KeymapSelector::GetSelectedName() const
{
    int sel = GetSelection();
    if (sel >= 0 && sel < static_cast<int>(m_items.size())) {
        return m_items[sel].name;
    }
    return wxString();
}

void KeymapSelector::AddItem(const KeymapItem& item)
{
    m_items.push_back(item);
    m_popup->SetItems(m_items);
}

void KeymapSelector::RemoveItem(int index)
{
    if (index >= 0 && index < static_cast<int>(m_items.size())) {
        m_items.erase(m_items.begin() + index);
        m_popup->SetItems(m_items);
        UpdateLabelText();
    }
}

void KeymapSelector::SetOnSelect(std::function<void(int)> callback)
{
    m_popup->SetOnSelect(callback);
}

void KeymapSelector::SetOnDelete(std::function<void(int)> callback)
{
    m_popup->SetOnDelete(callback);
}

void KeymapSelector::UpdateLabelText()
{
    int sel = GetSelection();
    if (sel >= 0 && sel < static_cast<int>(m_items.size())) {
        SetValue(m_items[sel].name);
    } else {
        SetValue(wxString::FromUTF8("默认键位"));
    }
}

} // namespace UI
