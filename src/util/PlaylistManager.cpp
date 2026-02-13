#include "PlaylistManager.h"
#include <wx/config.h>
#include <algorithm>

namespace Util {

PlaylistManager::PlaylistManager() 
    : m_currentIndex(0) {
    EnsureDefaultPlaylist();
}

PlaylistManager::~PlaylistManager() {
}

void PlaylistManager::EnsureDefaultPlaylist() {
    if (m_playlists.empty()) {
        m_playlists.emplace_back(GetDefaultName());
        m_currentIndex = 0;
    }
}

wxString PlaylistManager::GenerateUniqueName(const wxString& baseName) {
    wxString name = baseName;
    int counter = 1;
    
    while (true) {
        bool found = false;
        for (const auto& pl : m_playlists) {
            if (pl.name == name) {
                found = true;
                break;
            }
        }
        if (!found) return name;
        name = wxString::Format("%s (%d)", baseName, ++counter);
    }
}

int PlaylistManager::CreatePlaylist(const wxString& name) {
    wxString uniqueName = name.IsEmpty() 
        ? GenerateUniqueName(wxString::FromUTF8("新列表")) 
        : GenerateUniqueName(name);
    
    m_playlists.emplace_back(uniqueName);
    return static_cast<int>(m_playlists.size() - 1);
}

bool PlaylistManager::DeletePlaylist(int index) {
    // 不允许删除最后一个播放列表
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) {
        return false;
    }
    
    if (m_playlists.size() <= 1) {
        return false; // 保留至少一个播放列表
    }
    
    m_playlists.erase(m_playlists.begin() + index);
    
    // 调整当前索引
    if (m_currentIndex >= static_cast<int>(m_playlists.size())) {
        m_currentIndex = static_cast<int>(m_playlists.size()) - 1;
    } else if (m_currentIndex > index) {
        m_currentIndex--;
    }
    
    return true;
}

bool PlaylistManager::RenamePlaylist(int index, const wxString& name) {
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) {
        return false;
    }
    
    if (name.IsEmpty()) {
        return false;
    }
    
    // 检查名称是否重复（排除自身）
    for (int i = 0; i < static_cast<int>(m_playlists.size()); ++i) {
        if (i != index && m_playlists[i].name == name) {
            return false;
        }
    }
    
    m_playlists[index].name = name;
    return true;
}

bool PlaylistManager::SetCurrentPlaylist(int index) {
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) {
        return false;
    }
    
    m_currentIndex = index;
    return true;
}

Playlist* PlaylistManager::GetCurrentPlaylist() {
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_playlists.size())) {
        return nullptr;
    }
    return &m_playlists[m_currentIndex];
}

const Playlist* PlaylistManager::GetCurrentPlaylist() const {
    if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_playlists.size())) {
        return nullptr;
    }
    return &m_playlists[m_currentIndex];
}

Playlist* PlaylistManager::GetPlaylist(int index) {
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) {
        return nullptr;
    }
    return &m_playlists[index];
}

const Playlist* PlaylistManager::GetPlaylist(int index) const {
    if (index < 0 || index >= static_cast<int>(m_playlists.size())) {
        return nullptr;
    }
    return &m_playlists[index];
}

wxArrayString PlaylistManager::GetPlaylistNames() const {
    wxArrayString names;
    for (const auto& pl : m_playlists) {
        names.Add(pl.name);
    }
    return names;
}

bool PlaylistManager::AddFile(const wxString& path) {
    Playlist* pl = GetCurrentPlaylist();
    if (!pl) return false;
    return pl->AddFile(path);
}

bool PlaylistManager::RemoveFile(size_t index) {
    Playlist* pl = GetCurrentPlaylist();
    if (!pl) return false;
    return pl->RemoveFile(index);
}

void PlaylistManager::ClearFiles() {
    Playlist* pl = GetCurrentPlaylist();
    if (pl) pl->Clear();
}

const std::vector<wxString>& PlaylistManager::GetFiles() const {
    static const std::vector<wxString> empty;
    const Playlist* pl = GetCurrentPlaylist();
    return pl ? pl->files : empty;
}

size_t PlaylistManager::GetFileCount() const {
    const Playlist* pl = GetCurrentPlaylist();
    return pl ? pl->GetCount() : 0;
}

void PlaylistManager::LoadConfig(wxConfigBase* config) {
    m_playlists.clear();
    m_currentIndex = 0;
    
    // 读取播放列表数量
    long playlistCount = 0;
    config->Read("/Playlists/Count", &playlistCount, 0L);
    
    if (playlistCount <= 0) {
        EnsureDefaultPlaylist();
        return;
    }
    
    // 读取当前播放列表索引
    long currentIndex = 0;
    config->Read("/Playlists/CurrentIndex", &currentIndex, 0L);
    
    // 读取每个播放列表
    for (long i = 0; i < playlistCount; ++i) {
        wxString groupPath = wxString::Format("/Playlists/List_%ld", i);
        config->SetPath(groupPath);
        
        wxString name;
        config->Read("Name", &name, wxString::Format(wxString::FromUTF8("列表 %ld"), i + 1));
        
        Playlist pl(name);
        
        long fileCount = 0;
        config->Read("FileCount", &fileCount, 0L);
        
        for (long j = 0; j < fileCount; ++j) {
            wxString fileKey = wxString::Format("File_%ld", j);
            wxString filePath;
            if (config->Read(fileKey, &filePath) && !filePath.IsEmpty()) {
                pl.files.push_back(filePath);
            }
        }
        
        m_playlists.push_back(std::move(pl));
        config->SetPath("/");
    }
    
    // 设置当前索引
    if (currentIndex >= 0 && currentIndex < static_cast<long>(m_playlists.size())) {
        m_currentIndex = static_cast<int>(currentIndex);
    }
    
    EnsureDefaultPlaylist();
}

void PlaylistManager::SaveConfig(wxConfigBase* config) {
    // 删除旧的播放列表配置
    config->DeleteGroup("/Playlists");
    
    config->SetPath("/Playlists");
    
    // 写入播放列表数量
    long playlistCount = static_cast<long>(m_playlists.size());
    config->Write("Count", playlistCount);
    config->Write("CurrentIndex", static_cast<long>(m_currentIndex));
    
    // 写入每个播放列表
    for (long i = 0; i < playlistCount; ++i) {
        const Playlist& pl = m_playlists[i];
        
        wxString groupPath = wxString::Format("List_%ld", i);
        config->SetPath(groupPath);
        
        config->Write("Name", pl.name);
        config->Write("FileCount", static_cast<long>(pl.files.size()));
        
        for (size_t j = 0; j < pl.files.size(); ++j) {
            wxString fileKey = wxString::Format("File_%ld", static_cast<long>(j));
            config->Write(fileKey, pl.files[j]);
        }
        
        config->SetPath("..");
    }
    
    config->SetPath("/");
    config->Flush();
}

} // namespace Util
