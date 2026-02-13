#pragma once

#include <vector>
#include <string>
#include <wx/string.h>
#include <wx/arrstr.h>

namespace Util {

/**
 * @brief 单个播放列表的数据结构
 */
struct Playlist {
    wxString name;                      // 播放列表名称
    std::vector<wxString> files;        // 文件路径列表
    
    Playlist(const wxString& n = "") : name(n) {}
    
    // 检查播放列表是否为空
    bool IsEmpty() const { return files.empty(); }
    
    // 获取文件数量
    size_t GetCount() const { return files.size(); }
    
    // 添加文件（不重复添加）
    bool AddFile(const wxString& path) {
        for (const auto& f : files) {
            if (f == path) return false;
        }
        files.push_back(path);
        return true;
    }
    
    // 移除文件
    bool RemoveFile(size_t index) {
        if (index >= files.size()) return false;
        files.erase(files.begin() + index);
        return true;
    }
    
    // 清空文件
    void Clear() {
        files.clear();
    }
};

/**
 * @brief 播放列表管理器 - 管理多个播放列表
 */
class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager();
    
    // 播放列表管理
    int CreatePlaylist(const wxString& name);          // 创建新播放列表，返回索引
    bool DeletePlaylist(int index);                     // 删除播放列表
    bool RenamePlaylist(int index, const wxString& name); // 重命名播放列表
    int GetPlaylistCount() const { return static_cast<int>(m_playlists.size()); }
    
    // 当前播放列表操作
    bool SetCurrentPlaylist(int index);                 // 切换当前播放列表
    int GetCurrentPlaylistIndex() const { return m_currentIndex; }
    Playlist* GetCurrentPlaylist();
    const Playlist* GetCurrentPlaylist() const;
    
    // 获取播放列表
    Playlist* GetPlaylist(int index);
    const Playlist* GetPlaylist(int index) const;
    
    // 获取所有播放列表名称
    wxArrayString GetPlaylistNames() const;
    
    // 文件操作（操作当前播放列表）
    bool AddFile(const wxString& path);
    bool RemoveFile(size_t index);
    void ClearFiles();
    const std::vector<wxString>& GetFiles() const;
    size_t GetFileCount() const;
    
    // 配置持久化
    void LoadConfig(class wxConfigBase* config);
    void SaveConfig(class wxConfigBase* config);
    
    // 默认播放列表名称
    static wxString GetDefaultName() { return wxString::FromUTF8("默认列表"); }
    
private:
    std::vector<Playlist> m_playlists;
    int m_currentIndex;
    
    void EnsureDefaultPlaylist();
    wxString GenerateUniqueName(const wxString& baseName);
};

} // namespace Util
