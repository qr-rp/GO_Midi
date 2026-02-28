# GO_Midi 项目上下文

## 项目概述

GO_Midi 是一个 Windows 平台的 MIDI 自动演奏工具，主要用于游戏内自动化演奏（如《最终幻想14》、《燕云十六声》等）。程序读取 MIDI 文件，将音符转换为键盘按键，发送到目标游戏窗口。

### 核心功能
- **MIDI 解析与播放** - 完整解析 MIDI 文件，支持多音轨、变速播放
- **智能移调** - 自动计算最佳移调，确保音符落在可用键位范围内
- **多通道配置** - 8 个独立通道，每个通道可配置目标窗口、音轨、移调
- **播放列表管理** - 多播放列表、拖拽排序、拖放添加文件
- **AB 点循环播放** - 在进度条上右键设置 AB 点进行区间循环
- **定时播放** - 基于 NTP 时间同步的定时开始功能

### 技术栈
- **语言**: C++17
- **GUI 框架**: wxWidgets 3.3.1
- **构建系统**: CMake 3.10+
- **目标平台**: Windows 10/11 (64-bit)

---

## 项目结构

```
GO_Midi/
├── src/
│   ├── App.cpp/h              # 应用程序入口，日志配置初始化
│   ├── resource.rc            # Windows 资源文件（图标、manifest）
│   ├── assets/icon.ico        # 应用程序图标
│   ├── core/
│   │   ├── PlaybackEngine.*   # 播放引擎核心：线程管理、事件调度
│   │   └── KeyboardSimulator.* # 键盘模拟：发送按键到目标窗口
│   ├── midi/
│   │   └── MidiParser.*       # MIDI 文件解析器
│   ├── ui/
│   │   ├── MainFrame.*        # 主窗口框架，事件处理
│   │   ├── Widgets.*          # 自定义控件（ModernSlider, ScrollingText）
│   │   ├── PlaybackState.*    # 播放状态机
│   │   └── UIHelpers.*        # UI 辅助函数
│   └── util/
│       ├── KeyManager.*       # 键位映射管理
│       ├── Logger.*           # 日志系统
│       ├── PlaylistManager.*  # 播放列表管理器
│       ├── NtpClient.*        # NTP 时间同步客户端
│       └── MemoryPool.h       # 内存池优化
├── keymaps/                   # 键位映射文件目录
├── build_mingw.bat            # MinGW 构建脚本
├── CMakeLists.txt             # CMake 配置
└── .github/workflows/release.yml # GitHub Actions 发布流程
```

---

## 构建指南

### 前置要求
- CMake 3.10+
- MinGW-w64 或 Visual Studio 2022
- wxWidgets 3.3.1+（需要预先编译）

### MinGW 构建
```batch
# 设置 wxWidgets 路径（修改 build_mingw.bat 中的 WX_ROOT_PARAM）
build_mingw.bat
```

### Visual Studio 构建
```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DwxWidgets_ROOT_DIR="path/to/wxWidgets"
cmake --build . --config Release
```

### 关键 CMake 变量
- `wxWidgets_ROOT_DIR` - wxWidgets 安装路径
- `USE_STATIC_WX` - 是否静态链接 wxWidgets（默认 ON）

---

## 核心模块说明

### PlaybackEngine (core/PlaybackEngine.*)
播放引擎是程序核心，负责：
- 管理 MIDI 播放线程
- 将 MIDI 音符转换为键盘事件
- 处理多通道配置（每个通道独立的窗口、音轨、移调）
- 支持变速播放、跳转

关键方法：
```cpp
void load_midi(const Midi::MidiFile& midi_file);
void play(); void pause(); void stop(); void seek(double time_s);
void set_channel_transpose(int channel, int semitones);
void set_channel_window(int channel, void* hwnd);
void set_pitch_range(int min_pitch, int max_pitch);
```

### KeyboardSimulator (core/KeyboardSimulator.*)
负责模拟键盘输入：
- `send_key_down/up/press` - 发送按键事件
- `GetWindowList()` - 获取可用的窗口列表

### MidiParser (midi/MidiParser.*)
解析 MIDI 文件：
- 支持多音轨
- 计算 tick 到秒的转换（考虑 tempo 变化）
- 输出 `RawNote` 结构（开始时间、音高、持续时间、音轨索引）

### KeyManager (util/KeyManager.*)
管理音符到按键的映射：
- `load_config()` - 加载键位配置文件
- `get_mapping(int note)` - 获取音符对应的按键
- 支持 Shift/Ctrl 修饰键

### MainFrame (ui/MainFrame.*)
主窗口类，处理所有 UI 逻辑：
- 播放列表管理（多播放列表、拖拽排序）
- 通道配置面板
- AB 点循环播放（右键进度条设置）
- 拖放文件添加

### ModernSlider (ui/Widgets.*)
自定义进度条控件：
- AB 点可视化显示
- 右键设置 AB 点
- 拖动 A/B 点调整位置

---

## 配置文件

### config.ini
程序自动生成和读取的配置文件：
```ini
[Global]
MinPitch=48
MaxPitch=84
PlayMode=单曲播放
Decompose=0
LogEnabled=0
LogLevel=info

[Files]
# 每个文件的通道配置

[LastSelected]
FilePath=最后选中的文件路径

[Playlists]
Count=1
CurrentIndex=0

[Playlists/List_0]
Name=默认列表
FileCount=N
File_0=路径
...
```

### 键位映射文件格式
```
# 格式：音符: 按键
# 支持音名或 MIDI 编号
C4: a
C#4: w
D4: s
# 支持修饰键
C5: shift+a
```

---

## 开发约定

### 编码规范
- 使用 UTF-8 编码
- wxString 转 std::string 时使用 `ToUTF8()` 方法（避免中文乱码）
- 文件路径使用 `ToUTF8().data()` 传递给底层 API

### 日志系统
- 默认关闭文件日志（`LogEnabled=0`）
- 日志级别：debug, info, warn, error, fatal
- 只有 `LogEnabled=1` 时才创建 `logs/` 目录

### Git 工作流
- `main` 分支：稳定版本
- `dev` 分支：开发版本
- Tag 格式：
  - `v*`（如 v1.0.5）→ 正式发布
  - `dev-v*`（如 dev-v1.0.5）→ 预发布

### 版本发布
1. 在 dev 分支开发并测试
2. 推送 `dev-v*` tag 触发预发布
3. 合并到 main，推送 `v*` tag 触发正式发布

---

## 常见问题

### 中文路径乱码
使用 `wxString::ToUTF8()` 而非 `ToStdString()`，后者在 Windows 上使用本地 ANSI 编码。

### wxWidgets 找不到
修改 `build_mingw.bat` 中的 `WX_ROOT_PARAM` 变量指向正确的 wxWidgets 路径。

### 程序需要管理员权限
程序需要管理员权限来模拟全局键盘输入，这是 Windows 安全机制要求。

---

## 联系方式

- GitHub: https://github.com/qr-rp/GO_Midi
- QQ群: https://qm.qq.com/q/qDWtM0egCW
