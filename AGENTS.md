# Repository Guidelines

## Project Overview

**GO_Midi** — 一个用于游戏的 MIDI 自动演奏工具。解析 MIDI 文件（Format 0/1），将音符转为键盘按键，通过 Win32 `SendInput` API 发送到目标窗口。支持多通道并行播放（每个通道独立配置目标窗口、音轨、移调）、AB 点循环、定时播放（NTP 同步）、播放列表管理。

- **语言**: C++17 (禁用异常/RTTI)
- **GUI**: wxWidgets 3.3.1（源码编译，静态链接）
- **构建**: CMake 3.10+，仅 Windows 目标
- **平台**: Windows 10/11 64-bit，需要管理员权限（SendInput 键盘模拟）

## Architecture & Data Flow

```
[MIDI file] → MidiParser → MidiEvent[] → PlaybackEngine → KeyboardSimulator → [Target Window]
                                                 ↑
[UI/MainFrame] ── config changes ────────────────┘
      ↑               (atomic m_config_version)
      │
[User interaction]
```

### 核心数据流

1. **MIDI 解析** — `MidiParser` 读取 `.mid` 文件，解析为 `MidiEvent` 列表（含 tick、音符、力度、通道号），将 tick 转换为秒
2. **播放引擎** — `PlaybackEngine` 在独立线程运行，从事件队列中按时间调度 `MidiEvent`，对每通道应用移调后发送到 `KeyboardSimulator`
3. **键盘模拟** — `KeyboardSimulator` 调用 `SendInput` (keybd 模式) 或 `PostMessage` (窗口消息模式)，支持两种键位映射路径
4. **UI 通信** — `MainFrame` 通过 `wxTimer` 轮询引擎状态（位置、播放中/暂停），配置变更通过原子版本号 (`m_config_version`) 加条件变量通知引擎线程

### 多通道架构

- 8 个独立通道，每通道可配置：目标窗口、音轨选择、移调（半音）、启用/禁用
- 支持发送到不同窗口，或同窗口不同音轨

### 播放状态机

```
Idle → Playing ↔ Paused → Stopped → Idle
          ↘ Scheduled → Playing   (NTP 定时播放)
               Error → Idle       (出错恢复)
```

## Key Directories

```
GO_Midi/
├── src/
│   ├── App.cpp/h            # wxWidgets 应用入口，wxApp 子类
│   ├── core/
│   │   ├── PlaybackEngine   # 播放引擎：事件调度、配置热更新、移调、AB 循环
│   │   └── KeyboardSimulator # Win32 键盘模拟（SendInput/PostMessage 双路径）
│   ├── midi/
│   │   └── MidiParser       # MIDI Format 0/1 解析，tick→秒转换
│   ├── ui/
│   │   ├── MainFrame        # 主窗口：面板布局、控件、事件处理、配置 I/O
│   │   ├── PlaybackState    # 状态机（Idle/Playing/Paused/Stopped/Scheduled/Error）
│   │   ├── UIHelpers        # UIConstants（字符串常量）、工具函数
│   │   └── Widgets          # 自定义控件：ModernSlider、ScrollingText
│   ├── util/
│   │   ├── KeyManager       # 键位映射（O(1) 缓存、中文路径、FF14/Yys 预设）
│   │   ├── Logger           # 线程安全日志（控制台 + 文件）
│   │   ├── NtpClient        # NTP 时间同步（定时播放用）
│   │   └── PlaylistManager  # 多播放列表管理（拖拽排序、多种播放模式）
│   ├── resource.rc          # Windows 资源文件（图标、版本信息）
│   ├── Win32.manifest       # 视觉样式 manifest（通用控件 6.0）
│   └── assets/
│       └── icon.ico         # 应用图标
├── wxWidgets-3.3.1/         # wxWidgets 源码（构建时编译，gitignored）
├── build_mingw/             # MinGW 构建输出目录（gitignored）
├── CMakeLists.txt           # 根构建文件
├── mingw-toolchain.cmake    # MinGW 交叉编译工具链
├── build_mingw.bat          # MinGW 构建脚本
└── .github/workflows/
    ├── dev.yml              # 开发版 CI（dev-v* 标签触发，prerelease）
    └── release.yml          # 正式版 CI（v* 标签触发，正式 release）
```

## Development Commands

### 本地构建（MinGW，推荐）

```
build_mingw.bat
# 输出: build_mingw/GO_MIDI!.exe
```

要求：CMake 3.10+、MinGW-w64 (x86_64)。wxWidgets 3.3.1 源码需在项目根目录 `wxWidgets-3.3.1/`。

### 本地构建（Visual Studio 2022）

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
# 输出: build/Release/GO_MIDI!.exe
```

### CI 构建

- `dev-v*` 标签推送 → 触发 `dev.yml`，输出 `GO_MIDI!_<version>-dev.exe` 作为 prerelease
- `v*` 标签推送 → 触发 `release.yml`，输出 `GO_MIDI!_<version>.exe` 作为正式 release
- CI 使用 `windows-latest` + MSVC，wxWidgets 缓存 key 基于 CMakeLists.txt 和源文件 hash

### 注意事项

- **无测试框架**、**无 lint 工具**、**无格式化配置**
- 首次构建约 10-30 分钟（需编译 wxWidgets），后续增量构建快
- CI 和本地构建工具链不同（MSVC vs MinGW），CMakeLists.txt 中有各自的优化 flags

## Code Conventions & Common Patterns

### Naming

| 类别 | 规则 | 示例 |
|------|------|------|
| **类** | PascalCase | `PlaybackEngine`, `KeyManager` |
| **方法/函数** | snake_case | `load_midi()`, `set_channel_transpose()` |
| **成员变量** | `m_` 前缀 + snake_case | `m_current_time`, `m_config_version` |
| **常量** | `kCamelCase` | `kCamelCase` 推荐 |
| **枚举值** | `UPPER_CASE` | `enum class LogLevel { DEBUG, INFO, WARN, ERROR, FATAL }` |
| **命名空间** | PascalCase | `Core::`, `Midi::`, `UI::`, `Util::` |
| **文件名** | PascalCase | `PlaybackEngine.cpp`, `MidiParser.h` |
| **结构体** | PascalCase | `struct ChannelSettings` |
| **事件处理** | `On` + PascalCase | `OnImportFile`, `OnPlay`, `OnModeClick`, `OnSliderChange` |

### Formatting

- 缩进: 4 空格
- 大括号: `namespace` / `class` / 函数单独一行，`if` / `for` / `while` 同行
- 类/结构体成员每个用分号换行，最后一个也要
- 类型别名：`using` 而非 `typedef`
- 头文件保护：`#pragma once`

### Include Order

每组之间空行分隔：

```cpp
#include "PlaybackEngine.h"     // 1. 对应的 .h

#include <algorithm>             // 2. 标准库
#include <atomic>
#include <thread>

#include <wx/wx.h>              // 3. 第三方库（wxWidgets）

#include <windows.h>            // 4. Windows 头文件
#include <mmsystem.h>

#include "../util/Logger.h"     // 5. 项目头文件（相对路径 ../）
```

### Error Handling

- **无异常**：项目禁用异常（`wxUSE_EXCEPTIONS OFF`），不要 throw
- **无 RTTI**：`wxUSE_RTTI OFF`，不要用 `dynamic_cast`
- 关键路径函数返回 `bool` 表示成功/失败
- 使用日志宏记录错误（见 Logger.h）

### Logging (Logger.h)

```cpp
LOG("message");             // INFO 级别
LOG_DEBUG("msg: " << var);  // Debug
LOG_INFO("msg: " << var);   // Info
LOG_WARN("msg: " << var);   // Warning
LOG_ERROR("msg: " << var);  // Error
LOG_FATAL("msg: " << var);  // Fatal
```

Logger 是线程安全的（内部 `std::mutex` 保护），同时输出到控制台和文件。日志文件路径见 Logger.cpp。

### String Constants

所有 UI 字符串集中在 `src/ui/UIHelpers.h` 的 `UIConstants` 命名空间：

```cpp
namespace UIConstants {
    const wxString DEFAULT_WINDOW = wxString::FromUTF8("未选择");
    // ...
}
```

不要在多处定义相同的字符串字面量。

### Types & Memory

- 优先 `std::` 标准库类型
- 宽字符：`std::wstring` + `std::filesystem::path`（中文路径支持）
- 线程安全：`std::atomic<T>` 跨线程共享标量，`std::mutex` 保护复合数据
- 智能指针：`std::unique_ptr` 首选，`std::shared_ptr` 仅在共享所有权时用
- 键位映射查找：O(1) `unordered_map` + 缓存

### Playback Engine Patterns

- **状态机**: `PlaybackStateMachine` (Idle / Playing / Paused / Stopped / Scheduled / Error)
- **配置热更新**: 原子版本号 `m_config_version` + `std::condition_variable` 通知播放线程
- **活跃按键追踪**: `ActiveKeySet` (`unordered_set<pair<int, void*>>`) + 引用计数防止卡键
- **智能移调**: 基于音高直方图自动计算最佳移调量
- **AB 循环**: 进度条右键设 A/B 点，循环播放，第三次右键清除
- **键盘模拟**: `SendInput`（推荐）和 `PostMessage`（备选）双路径
- **时间同步**: NTP 客户端用于定时播放的精确时间

### UI Event System

- 主 UI 运行在 wxWidgets 主线程（`wxTimer` 轮询引擎状态）
- 引擎运行在独立 `std::thread`
- UI → 引擎通信：原子标志 + 条件变量
- 引擎 → UI 通信：通过 `wxQueueEvent` 或 `CallAfter` 跨线程投递

### Comments

注释用中文写，代码标识符用英文。

## Important Files

| 文件 | 作用 |
|------|------|
| `src/App.cpp` | wxWidgets 入口 `OnInit()` / `OnExit()` |
| `src/core/PlaybackEngine.cpp/h` | 播放引擎核心（~39KB），线程调度、配置热更新 |
| `src/core/KeyboardSimulator.cpp/h` | Win32 键盘模拟（SendInput/PostMessage） |
| `src/midi/MidiParser.cpp/h` | MIDI 解析（Format 0/1，~23KB） |
| `src/ui/MainFrame.cpp/h` | 主窗口（~95KB，项目最大文件），所有控件和事件 |
| `src/ui/PlaybackState.cpp/h` | 状态机实现 |
| `src/ui/UIHelpers.cpp/h` | UI 字符串常量和辅助函数 |
| `src/ui/Widgets.cpp/h` | 自定义控件（ModernSlider, ScrollingText） |
| `src/util/KeyManager.cpp/h` | 键位映射管理（~28KB） |
| `src/util/Logger.cpp/h` | 线程安全日志系统 |
| `src/util/NtpClient.cpp/h` | NTP 时间同步 |
| `src/util/PlaylistManager.cpp/h` | 播放列表管理 |
| `CMakeLists.txt` | 构建配置（wxWidgets 特征开关） |
| `mingw-toolchain.cmake` | MinGW 交叉编译工具链定义 |
| `build_mingw.bat` | MinGW 一键构建脚本 |
| `.github/workflows/dev.yml` | 开发版 CI |
| `.github/workflows/release.yml` | 正式版 CI |

## Runtime/Tooling Preferences

- **构建系统**: CMake 3.10+（仅 Windows）
- **工具链**: MinGW-w64 (x86_64) 本地，MSVC (windows-latest) CI
- **包管理**: 无包管理器；wxWidgets 通过源码 `add_subdirectory` 集成
- **wxWidgets 版本**: 3.3.1，静态链接，~40 个不需要的特性被禁用以减少体积
- **编译器 flags**:
  - MSVC Release: `/O1 /GL /LTCG /OPT:REF /OPT:ICF`
  - MinGW Release: `-Os -DNDEBUG -ffunction-sections -fdata-sections -s -Wl,--gc-sections`
  - MinGW: `-static-libgcc -static-libstdc++ -static`
- **Windows 链接库**: `winmm`, `ws2_32`, `gdi32`, `user32`, `kernel32`, `shell32`, `ole32`, `comctl32`, `uxtheme`, `oleacc`, `gdiplus` 等（~25 个）
- **配置文件**: `config.ini`（wxConfig INI 格式），自动生成/读取
- **热键**: F12 全局播放/暂停
- **Git 分支**: `main`（稳定）/ `dev`（开发）
- **无测试工具链** — 项目不包含测试框架或测试文件

## Testing & QA

当前项目**没有测试框架、测试文件或 lint 工具**。所有质量保障通过以下方式进行：

- **编译验证**: `cmake --build` 成功即视为基本验证通过
- **CI**: GitHub Actions 在标签推送时自动构建，生成 release artifact
- **运行时测试**: 手动测试为主（MIDI 文件播放、窗口切换、键位映射等）

如果计划引入测试，建议方向：
- 单元测试：`MidiParser`（无外部依赖，纯数据解析）
- 集成测试：`PlaybackEngine` + `MidiParser` 联调
- 不建议为 UI 层（wxWidgets）和 Windows API 层（SendInput）写自动化测试
