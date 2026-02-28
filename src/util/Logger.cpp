#include "Logger.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <cctype>
#include <windows.h>

namespace GO_MIDI
{

    Logger &Logger::Instance()
    {
        static Logger instance;
        return instance;
    }

    bool Logger::Initialize(LogLevel level, const std::string &logDir, bool fileOutput)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_initialized)
        {
            return true; // 已初始化
        }

        m_level.store(level);
        m_fileOutput.store(fileOutput);

        // 只有启用文件输出时才创建日志目录和文件
        if (fileOutput)
        {
            // 创建日志目录
            try
            {
                std::filesystem::path logPath(logDir);
                if (!std::filesystem::exists(logPath))
                {
                    std::filesystem::create_directories(logPath);
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "[Logger] Failed to create log directory: " << e.what() << std::endl;
                return false;
            }

            // 清理旧日志并创建新日志文件
            if (!CreateLogFile(logDir))
            {
                return false;
            }
        }

        m_initialized = true;

        // 记录初始化信息
        std::ostringstream oss;
        oss << "Logger initialized. Level: " << LevelToString(level);
        if (fileOutput)
        {
            oss << ", Log file: " << m_currentLogFile;
        }
        else
        {
            oss << ", File output: disabled";
        }

        // 直接输出到控制台和文件（此时已初始化）
        std::cout << "[Logger] " << oss.str() << std::endl;
        if (m_fileStream.is_open())
        {
            m_fileStream << GetTimestamp() << " [" << GetThreadId() << "] [INFO] "
                         << oss.str() << std::endl;
            m_fileStream.flush();
        }

        return true;
    }

    void Logger::Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_initialized)
        {
            return;
        }

        // 记录关闭信息
        if (m_fileStream.is_open())
        {
            m_fileStream << GetTimestamp() << " [" << GetThreadId() << "] [INFO] "
                         << "Logger shutting down." << std::endl;
            m_fileStream.close();
        }

        m_initialized = false;
    }

    void Logger::SetLevel(LogLevel level)
    {
        m_level.store(level);
    }

    LogLevel Logger::GetLevel() const
    {
        return m_level.load();
    }

    bool Logger::ParseLevel(const std::string &levelStr, LogLevel &level)
    {
        // 转换为小写进行比较
        std::string lowerStr = levelStr;
        for (auto &c : lowerStr)
        {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }

        if (lowerStr == "debug")
        {
            level = LogLevel::Debug;
            return true;
        }
        else if (lowerStr == "info")
        {
            level = LogLevel::Info;
            return true;
        }
        else if (lowerStr == "warn" || lowerStr == "warning")
        {
            level = LogLevel::Warning;
            return true;
        }
        else if (lowerStr == "error")
        {
            level = LogLevel::Error;
            return true;
        }
        else if (lowerStr == "fatal")
        {
            level = LogLevel::Fatal;
            return true;
        }

        // 解析失败，返回 false
        return false;
    }

    void Logger::SetConsoleOutput(bool enable)
    {
        m_consoleOutput.store(enable);
    }

    void Logger::SetFileOutput(bool enable)
    {
        m_fileOutput.store(enable);
    }

    void Logger::Log(LogLevel level, const char *file, int line, const char *func, const std::string &message)
    {
        if (!m_initialized || !ShouldLog(level))
        {
            return;
        }

        // 格式化日志行（在锁外进行，减少锁持有时间）
        std::ostringstream oss;
        oss << GetTimestamp()
            << " [" << GetThreadId() << "] "
            << "[" << LevelToString(level) << "] "
            << "[" << ExtractFileName(file) << ":" << line << " " << func << "] "
            << message;

        std::string logLine = oss.str();

        // 获取锁并输出
        std::lock_guard<std::mutex> lock(m_mutex);

        // 输出到控制台
        if (m_consoleOutput.load())
        {
            // 根据级别选择输出流和颜色
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            WORD originalColor = 7; // 默认灰白色

            if (hConsole != INVALID_HANDLE_VALUE)
            {
                CONSOLE_SCREEN_BUFFER_INFO csbi;
                if (GetConsoleScreenBufferInfo(hConsole, &csbi))
                {
                    originalColor = csbi.wAttributes;
                }

                // 设置颜色
                WORD color = originalColor;
                switch (level)
                {
                case LogLevel::Debug:
                    color = 8; // 深灰色
                    break;
                case LogLevel::Info:
                    color = 7; // 白色
                    break;
                case LogLevel::Warning:
                    color = 14; // 黄色
                    break;
                case LogLevel::Error:
                    color = 12; // 红色
                    break;
                case LogLevel::Fatal:
                    color = 79; // 白底红字
                    break;
                }
                SetConsoleTextAttribute(hConsole, color);
            }

            std::cout << logLine << std::endl;

            // 恢复颜色
            if (hConsole != INVALID_HANDLE_VALUE)
            {
                SetConsoleTextAttribute(hConsole, originalColor);
            }
        }

        // 输出到文件
        if (m_fileOutput.load() && m_fileStream.is_open())
        {
            m_fileStream << logLine << std::endl;
            // 对于 Error 和 Fatal 级别，立即刷新
            if (level >= LogLevel::Error)
            {
                m_fileStream.flush();
            }
        }
    }

    bool Logger::ShouldLog(LogLevel level) const
    {
        return static_cast<int>(level) >= static_cast<int>(m_level.load());
    }

    const char *Logger::LevelToString(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }

    std::string Logger::GetTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch()) %
                  1000;

        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;

#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();

        return oss.str();
    }

    std::string Logger::GetThreadId()
    {
        std::ostringstream oss;
        oss << std::this_thread::get_id();
        return oss.str();
    }

    bool Logger::CreateLogFile(const std::string &logDir)
    {
        // 先清理旧日志
        RotateOldLogs(logDir, 5);

        // 生成日志文件名：GO_MIDI_YYYYMMDD_HHMMSS.log
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;

#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif

        std::ostringstream filename;
        filename << logDir << "/GO_MIDI_"
                 << std::put_time(&tm, "%Y%m%d_%H%M%S")
                 << ".log";

        m_currentLogFile = filename.str();

        // 打开文件
        m_fileStream.open(m_currentLogFile, std::ios::out | std::ios::app);
        if (!m_fileStream.is_open())
        {
            std::cerr << "[Logger] Failed to open log file: " << m_currentLogFile << std::endl;
            return false;
        }

        return true;
    }

    void Logger::RotateOldLogs(const std::string &logDir, int maxFiles)
    {
        try
        {
            std::filesystem::path logPath(logDir);
            if (!std::filesystem::exists(logPath))
            {
                return;
            }

            // 收集所有日志文件
            std::vector<std::filesystem::path> logFiles;
            for (const auto &entry : std::filesystem::directory_iterator(logPath))
            {
                if (entry.is_regular_file())
                {
                    std::string filename = entry.path().filename().string();
                    if (filename.find("GO_MIDI_") == 0 &&
                        filename.find(".log") == filename.length() - 4)
                    {
                        logFiles.push_back(entry.path());
                    }
                }
            }

            // 按修改时间排序（新的在前）
            std::sort(logFiles.begin(), logFiles.end(),
                      [](const std::filesystem::path &a, const std::filesystem::path &b)
                      {
                          return std::filesystem::last_write_time(a) >
                                 std::filesystem::last_write_time(b);
                      });

            // 删除超出数量限制的旧日志
            for (size_t i = static_cast<size_t>(maxFiles); i < logFiles.size(); ++i)
            {
                std::filesystem::remove(logFiles[i]);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Logger] Error during log rotation: " << e.what() << std::endl;
        }
    }

    const char *Logger::ExtractFileName(const char *path)
    {
        const char *name = path;
        const char *p = path;

        while (*p)
        {
            if (*p == '/' || *p == '\\')
            {
                name = p + 1;
            }
            ++p;
        }

        return name;
    }

} // namespace GO_MIDI