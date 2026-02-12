# GO_MIDI!

一个基于 wxWidgets 的 MIDI 文件播放器，支持将 MIDI 音符转换为键盘按键发送到指定窗口。

## 功能特性

- **MIDI 文件解析** - 支持多音轨 MIDI 文件
- **键盘模拟** - 将 MIDI 音符转换为键盘按键
- **多通道配置** - 8 个独立通道，每个通道可单独配置
- **智能移调** - 自动计算最佳移调，使音符落在可用键位范围内
- **定时播放** - 支持 NTP 时间同步的定时播放功能
- **播放列表** - 支持单曲、列表、随机、循环播放模式
- **窗口选择** - 自动列出可操作的窗口，支持按键发送到指定窗口

## 系统要求

- Windows 10/11
- MinGW-w64 或 MSVC 编译器
- wxWidgets 3.3.1+

## 构建方法

### 使用 MinGW

```bash
mkdir build_mingw
cd build_mingw
cmake .. -G "MinGW Makefiles"
mingw32-make -j4
```

### 使用 MSVC

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## 使用说明

1. **导入 MIDI 文件** - 点击"导入"按钮选择 MIDI 文件
2. **配置通道** - 为每个通道选择目标窗口、音轨、移调
3. **设置键位映射** - 编辑 `keymap.txt` 自定义音符到按键的映射
4. **播放** - 点击"播放"按钮开始播放

## 键位映射

键位映射文件 `keymap.txt` 格式：

```
C4: a
D4: s
E4: d
F4: f
G4: g
A4: h
B4: j
C5: k
```

支持格式：
- 音名：`C4`, `D#4`, `Eb4`
- MIDI 编号：`60`, `61`, `62`

## 许可证

MIT License

## 致谢

- [wxWidgets](https://www.wxwidgets.org/) - 跨平台 GUI 框架
