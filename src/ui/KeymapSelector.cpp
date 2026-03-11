#include "KeymapSelector.h"

namespace UI {

// ================= KeymapPopup Implementation =================

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

void KeymapPopup::OnMouseMove(wxMouseEvent& event)
{
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
            // 获取列表的宽度作为删除区域的参考
            int listWidth = m_list->GetClientSize().GetWidth();
            int deleteZoneStart = listWidth - 40; // 右侧40像素为删除区域

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
    m_selected = item;
    Dismiss();
    if (m_onSelect && m_selected >= 0) {
        m_onSelect(m_selected);
    }
}

// ================= KeymapSelector Implementation =================

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