#include "App.h"
#include "ui/MainFrame.h"
#include "util/Logger.h"
#include <ctime>
#include <cstdlib>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

wxIMPLEMENT_APP(App);

// 读取配置文件中日志级别，默认 info
static LogLevel LoadLogLevelFromConfig()
{
    // 默认日志级别为 Info（输出 info、warn、error、fatal）
    LogLevel level = LogLevel::Info;

    // 获取配置文件路径
    wxFileName exePath(wxStandardPaths::Get().GetExecutablePath());
    wxString configPath = wxFileName(exePath.GetPath(), "config.ini").GetFullPath();

    // 使用 wxFileConfig 读取配置
    wxFileConfig config("wx_GO_MIDI", "wx_GO_MIDI", configPath, "", wxCONFIG_USE_LOCAL_FILE);

    wxString levelStr;
    if (config.Read("/Global/LogLevel", &levelStr))
    {
        if (!Logger::ParseLevel(levelStr.ToStdString(), level))
        {
            // 解析失败，使用默认值 Info
            level = LogLevel::Info;
        }
    }

    return level;
}

bool App::OnInit()
{
    // 从配置文件读取日志级别
    LogLevel logLevel = LoadLogLevelFromConfig();
    Logger::Instance().Initialize(logLevel);
    LOG_INFO("GO_MIDI! 启动中...");

    // Set up high precision timer (Windows specific)
#ifdef _WIN32
    timeBeginPeriod(1);
#endif

    // Initialize random seed for better randomness
    srand(static_cast<unsigned int>(time(nullptr)));

    MainFrame *frame = new MainFrame();
    frame->Show(true);

    LOG_INFO("主窗口已创建，初始化完成");
    return true;
}

int App::OnExit()
{
    LOG_INFO("GO_MIDI! 正在关闭...");

    // 关闭日志系统
    Logger::Instance().Shutdown();

#ifdef _WIN32
    timeEndPeriod(1);
#endif
    return 0;
}
