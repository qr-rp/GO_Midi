# GO_Midi 项目上下文

## 项目概述

Windows 平台的 MIDI 自动演奏工具（游戏场景），解析 MIDI 文件将音符转为键盘按键发送到目标窗口。
- **语言**: C++17
- **GUI**: wxWidgets 3.3.1（源码编译静态链接）
- **构建**: CMake 3.10+，仅 Windows（Win32 API）
- **无测试框架**、**无 lint 工具**、**无格式化配置**

## 构建命令

```batch
# MinGW（推荐）
build_mingw.bat
# 输出: build_mingw/GO_MIDI!.exe

# Visual Studio 2022
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
# 输出: build/Release/GO_MIDI!.exe

# CI 构建（GitHub Actions）: 推送 dev-v* 或 v* 标签触发
```

**无测试命令** — 项目不包含测试框架或测试文件。CMake target: `wx_GO_MIDI_CPP`（输出名 `GO_MIDI!`）。

## 代码风格

### 命名

| 类别 | 规则 | 示例 |
|------|------|------|
| **类** | PascalCase | `PlaybackEngine`, `KeyManager` |
| **方法/函数** | snake_case | `load_midi()`, `set_channel_transpose()` |
| **成员变量** | `m_` 前缀 + snake_case | `m_current_time`, `m_config_version` |
| **常量** | `kCamelCase` 或 `UPPER_CASE` | `kCamelCase` 推荐；`UPPER_CASE` 用于枚举值 |
| **命名空间** | PascalCase | `Core::`, `Midi::`, `UI::`, `Util::` |
| **文件名** | PascalCase | `PlaybackEngine.cpp`, `MidiParser.h` |
| **枚举/结构体** | PascalCase | `struct ChannelSettings`, `enum class LogLevel` |

### Include 顺序

1. 对应的 `.h` 文件（`.cpp` 中）
2. 标准库
3. 第三方库（wxWidgets）
4. Windows 头文件
5. 项目头文件（相对路径 `../`）

每组之间空行分隔。例：
```cpp
#include "PlaybackEngine.h"

#include <algorithm>
#include <atomic>
#include <thread>

#include <windows.h>
#include <mmsystem.h>

#include "../util/Logger.h"
```

### 格式

- **缩进**: 4 空格
- **大括号**: `namespace` / `class` / `函数` 单独一行，`if/for` 同一行
- **类/结构体成员**: 每个成员用分号后换行，最后一个也要
- **对齐**: 成员变量按类型分组排列
- **类型别名**: 用 `using` 而非 `typedef`
- **头文件保护**: 一律 `#pragma once`

### 类型

- 优先 `std::` 标准库类型
- 宽字符: 使用 `std::wstring` + `std::filesystem::path` 支持中文路径
- 线程安全: `std::atomic<T>` 保护跨线程共享标量，`std::mutex` 保护复合数据
- 智能指针: `std::unique_ptr` 首选，`std::shared_ptr` 仅在确需共享所有权时使用
- 禁用 RTTI 和异常 (`wxUSE_EXCEPTIONS OFF`, `wxUSE_RTTI OFF`)，不要依赖 `dynamic_cast` / `try-catch`

### 错误处理

- 使用 `LOG_ERROR(msg)` / `LOG_WARN(msg)` 记录错误（见 `Logger.h` 日志宏）
- 关键路径函数返回 `bool` 表示成功/失败
- 不要 throw 异常（项目编译禁用了异常）
- 文件操作使用 `std::filesystem::path` 处理中文路径

### 日志宏

在 `src/util/Logger.h` 中定义，全局可用：
```cpp
LOG("message");             // INFO 级别
LOG_DEBUG("msg: " << var);  // Debug
LOG_INFO("msg: " << var);   // Info
LOG_WARN("msg: " << var);   // Warning
LOG_ERROR("msg: " << var);  // Error
LOG_FATAL("msg: " << var);  // Fatal
```

### 字符串常量

所有 UI 字符串统一在 `src/ui/UIHelpers.h` 的 `UIConstants` 命名空间中定义：
```cpp
namespace UIConstants {
    const wxString DEFAULT_WINDOW = wxString::FromUTF8("未选择");
    // ...
}
```
不要在多处定义相同的字符串字面量。

### UI 控件事件命名

事件处理函数使用 `On` 前缀 + PascalCase: `OnImportFile`, `OnPlay`, `OnModeClick`, `OnSliderChange`。

### 播放引擎

- 播放状态通过 `PlaybackStateMachine` 管理（状态: Idle/Playing/Paused/Stopped/Scheduled/Error）
- 引擎配置变更通过原子版本号 `m_config_version` + 条件变量通知播放线程
- 活跃按键用 `ActiveKeySet` (`unordered_set<pair<int, void*>>`) 追踪防卡键
- 智能移调基于音高直方图自动计算

## 项目结构

```
src/
├── App.cpp/h              # wxWidgets 应用入口
├── core/
│   ├── KeyboardSimulator  # Win32 SendInput 键盘模拟
│   └── PlaybackEngine     # 播放线程、事件处理、配置热更新
├── midi/
│   └── MidiParser         # MIDI Format 0/1 解析、Tick→秒转换
├── ui/
│   ├── MainFrame          # 主窗口（包含AB点循环、定时播放、窗口恢复）
│   ├── PlaybackState      # 播放状态机
│   ├── UIHelpers          # UI常量( UIConstants) + 辅助函数
│   ├── Widgets            # ModernSlider, ScrollingText 自定义控件
└── util/
    ├── KeyManager          # 键位映射（O(1)缓存、中文路径）
    ├── Logger              # 线程安全日志（控制台+文件）
    ├── NtpClient           # NTP 时间同步
    └── PlaylistManager     # 多播放列表管理
```

## 关键约定

- **平台**: 仅 Windows（Win32 API），需要管理员权限（SendInput）
- **热键**: F12 全局播放/暂停
- **配置文件**: `config.ini` 自动生成/读取（wxConfig 格式）
- **Git 分支**: `main`（稳定） / `dev`（开发）; 推送 `v*` 或 `dev-v*` 标签触发 CI Release
- **注释**: 用中文写注释（代码本身保持英文标识符）
- **不要**: 添加与当前任务无关的代码、修改 git config、push 到未授权的远程仓库
