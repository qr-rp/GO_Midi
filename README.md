<p align="center">
  <img src="src/assets/icon.ico" width="64" height="64" alt="GO_MIDI! Icon">
</p>

<p align="center">
  <strong>强大的 MIDI 自动演奏工具</strong>
</p>

<p align="center">
  <a href="#功能特性">功能特性</a> •
  <a href="#安装">安装</a> •
  <a href="#使用指南">使用指南</a> •
  <a href="#配置">配置</a> •
  <a href="#构建">构建</a>
</p>

<p align="center">
  <img src="https://img.shields.io/github/v/release/qr-rp/GO_Midi?style=flat-square" alt="Release">
  <img src="https://img.shields.io/github/license/qr-rp/GO_Midi?style=flat-square" alt="License">
  <img src="https://img.shields.io/github/stars/qr-rp/GO_Midi?style=flat-square" alt="Stars">
  <img src="https://img.shields.io/github/downloads/qr-rp/GO_Midi/total?style=flat-square" alt="Downloads">
</p>

<p align="center">
  💬 <a href="https://qm.qq.com/q/qDWtM0egCW"><strong>点击链接加入群聊【GO_MIDI!】</strong></a>
</p>

---

## ✨ 功能特性

### 🎵 MIDI 播放
- **多音轨支持** - 完整解析 MIDI 文件中的所有音轨
- **智能移调** - 自动计算最佳移调，让音符落在可用键位范围内
- **多通道配置** - 8 个独立通道，每个通道可单独配置目标窗口、音轨和移调
- **播放控制** - 播放/暂停/停止/跳转，支持变速播放

### ⌨️ 键盘模拟
- **窗口选择** - 自动列出所有可操作窗口，选择目标窗口发送按键
- **自定义键位映射** - 内置FF14键位，提供燕云十六声键位映射文件
- **多窗口支持** - 不同通道可发送到不同窗口

### 📋 播放列表
- **多播放列表支持** - 创建、删除、重命名多个独立播放列表
- **快速切换** - 一键切换不同播放列表，切换时不中断当前播放
- **拖拽排序** - 在列表内拖拽歌曲调整播放顺序
- **多种播放模式** - 单曲播放、列表播放、随机播放、循环播放
- **文件管理** - 导入多个 MIDI 文件，快速切换

### 🔁 AB 点循环播放
- **右键设置** - 在进度条上右键点击设置 A 点，再次右键设置 B 点
- **循环播放** - 设置完 B 点后自动从 A 点开始循环播放
- **拖动调整** - 右键按住 A/B 点可拖动调整位置
- **快速清除** - 第三次右键清除 AB 点并退出循环模式

### ⏰ 定时播放
- **NTP 时间同步** - 精确的网络时间同步
- **定时开始** - 设置指定时间自动开始播放

---

## 📥 安装

### 从 Release 下载

前往 [Releases](https://github.com/qr-rp/GO_Midi/releases) 页面下载最新版本。

### 系统要求

- Windows 10/11 (64-bit)
- 程序会自动请求管理员权限（用于键盘模拟）

---

## 📖 使用指南

### 快速开始

1. **启动程序** - 以管理员身份运行 `GO_MIDI!.exe`，在 UAC 提示时点击「是」
2. **导入映射** - 点击加载键位导入自定义键位或者使用内置默认键位
3. **设置音域** - 设置目标音域为游戏支持的音域，例如燕云十六声为48-83
4. **导入 MIDI** - 点击「导入」按钮选择 MIDI 文件
5. **配置通道** - 为每个通道选择目标窗口和音轨
6. **开始播放** - 点击「播放」按钮

### 全局热键

| 热键 | 功能 |
|------|------|
| **F12** | 播放/暂停（全局生效，可在其他窗口使用）|

> 💡 使用 F12 可以在任何时候快速控制播放，无需切换回程序窗口。

### 通道配置

| 设置项 | 说明 |
|--------|------|
| **窗口** | 选择接收按键的目标窗口 |
| **音轨** | 选择该通道播放的音轨（或「全部音轨」）|
| **移调** | 音符升降调（半音为单位）|
| **启用** | 开启/关闭该通道 |

---

## ⚙️ 配置

### 配置文件

程序会自动保存配置到 `config.ini`：

```ini
[Channel_0]
Window=窗口标题
Track=音轨名称
Transpose=0
Enabled=1

[Channel_1]
...
```

### 键位映射文件

创建 `任意名字.txt` 自定义音符到按键的映射：

```
# 格式：音符: 按键
# 支持音名或 MIDI 编号

C4: a
C#4: w
D4: s
D#4: e
E4: d
F4: f
F#4: t
G4: g
G#4: y
A4: h
A#4: u
B4: j
C5: k
```

---

## 🔨 构建

### 前置要求

- CMake 3.10+
- MinGW-w64 或 Visual Studio 2022

### MinGW 构建（推荐）

项目已集成 wxWidgets 源码编译，无需手动配置依赖：

```bash
# 1. 克隆仓库
git clone https://github.com/qr-rp/GO_Midi.git
cd GO_Midi

# 2. 下载 wxWidgets 3.3.1 源码
# 从 https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.1/wxWidgets-3.3.1.tar.bz2 下载
# 解压到项目目录，确保路径为：GO_Midi/wxWidgets-3.3.1

# 3. 运行构建脚本
build_mingw.bat
```

构建脚本会自动完成：
- CMake 配置（集成 wxWidgets 源码编译）
- 并行编译（使用所有 CPU 核心）
- 输出可执行文件到 `build_mingw/GO_MIDI!.exe`

> **首次构建时间**：由于需要编译 wxWidgets，首次构建约需 10-30 分钟。后续构建会快很多。

### Visual Studio 构建

```bash
# 克隆仓库
git clone https://github.com/qr-rp/GO_Midi.git
cd GO_Midi

# 下载 wxWidgets 3.3.1 源码并解压到项目目录

# 创建构建目录
mkdir build && cd build

# 配置和构建
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

---

## 🛠️ 技术栈

<p align="center">
  <img src="https://img.shields.io/badge/C++-17-blue?style=flat-square" alt="C++17">
  <img src="https://img.shields.io/badge/wxWidgets-3.3.1-green?style=flat-square" alt="wxWidgets">
  <img src="https://img.shields.io/badge/CMake-3.10+-red?style=flat-square" alt="CMake">
  <img src="https://img.shields.io/badge/Platform-Windows-blue?style=flat-square" alt="Windows">
</p>

- **语言**: C++17
- **GUI 框架**: wxWidgets 3.3.1
- **构建系统**: CMake
- **目标平台**: Windows 10/11

---

## 📝 许可证

本项目基于 [MIT License](LICENSE) 开源。

---

## 🙏 致谢

- [wxWidgets](https://www.wxwidgets.org/) - 跨平台 C++ GUI 框架

---

<p align="center">
  <sub>Made with ❤️ by <a href="https://github.com/qr-rp">qr-rp</a></sub>
</p>
