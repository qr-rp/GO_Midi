#pragma once

#include <wx/wx.h>
#include <wx/combo.h>
#include <vector>
#include <functional>

namespace UI {

/**
 * @brief 键位配置项结构
 */
struct KeymapItem {
    wxString name;           ///< 配置名称
    wxString path;           ///< 配置文件路径（空表示内置默认）
    bool isDefault;          ///< 是否为内置默认配置
    std::map<int, int> vkMap;    ///< 音符 -> VK码 映射
    std::map<int, int> modMap;   ///< 音符 -> 修饰键 映射
};

/**
 * @brief 自定义下拉弹出面板
 */
class KeymapPopup : public wxComboPopup
{
public:
    KeymapPopup() : m_selected(-1) {}

    virtual void Init() override {}

    virtual bool Create(wxWindow* parent) override;

    virtual wxWindow* GetControl() override { return m_list; }

    virtual void SetStringValue(const wxString& value) override;

    virtual wxString GetStringValue() const override;

    virtual wxSize GetAdjustedSize(int minWidth, int prefHeight, int maxHeight) override;

    virtual void OnPopup() override;

    /// 设置配置列表
    void SetItems(const std::vector<KeymapItem>& items);

    /// 获取当前选中索引
    int GetSelection() const { return m_selected; }

    /// 设置选中索引
    void SetSelection(int index);

    /// 删除指定索引的项
    void RemoveItem(int index);

    /// 获取项数量
    size_t GetCount() const { return m_items.size(); }

    /// 设置回调：选中配置时调用
    void SetOnSelect(std::function<void(int)> callback) { m_onSelect = callback; }

    /// 设置回调：删除配置时调用
    void SetOnDelete(std::function<void(int)> callback) { m_onDelete = callback; }

private:
    wxListBox* m_list = nullptr;
    std::vector<KeymapItem> m_items;
    int m_selected;
    std::function<void(int)> m_onSelect;
    std::function<void(int)> m_onDelete;

    void OnListSelect(wxCommandEvent& event);
    void OnListDClick(wxCommandEvent& event);
    void OnMouseMove(wxMouseEvent& event);
    void OnMouseClick(wxMouseEvent& event);

    wxDECLARE_EVENT_TABLE();
};

/**
 * @brief 键位配置选择器控件
 * 
 * 下拉框显示已导入的键位配置列表，默认配置不可删除。
 * 非默认配置项后面显示 "✕" 标记，点击可删除。
 */
class KeymapSelector : public wxComboCtrl
{
public:
    KeymapSelector(wxWindow* parent, wxWindowID id = wxID_ANY);

    /// 设置配置列表
    void SetItems(const std::vector<KeymapItem>& items);

    /// 获取当前选中的配置索引，-1 表示无选中
    int GetSelection() const;

    /// 设置选中配置
    void SetSelection(int index);

    /// 获取当前配置名称
    wxString GetSelectedName() const;

    /// 添加新配置
    void AddItem(const KeymapItem& item);

    /// 删除指定配置
    void RemoveItem(int index);

    /// 设置选中回调
    void SetOnSelect(std::function<void(int)> callback);

    /// 设置删除回调
    void SetOnDelete(std::function<void(int)> callback);

private:
    KeymapPopup* m_popup = nullptr;
    std::vector<KeymapItem> m_items;

    void UpdateLabelText();

    wxDECLARE_EVENT_TABLE();
};

} // namespace UI
