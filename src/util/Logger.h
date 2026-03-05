#pragma once

// 标准库
#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <array>

namespace Util
{

    /// 日志级别枚举
    /// 从低到高：Debug < Info < Warning < Error < Fatal
    /// 设置某级别后，只有该级别及以上的日志会被输出
    enum class LogLevel : int
    {
        Debug = 0,
        Info = 1,
        Warning = 2,
        Error = 3,
        Fatal = 4
    };

    /// 轻量级高性能日志系统
    ///
    /// 特性：
    /// - 线程安全：使用互斥锁保护输出
    /// - 异步友好：最小化锁持有时间
    /// - 双输出：同时输出到控制台和文件
    /// - 日志轮转：最多保留5份日志文件
    /// - 可配置：运行时可调整日志级别
    ///
    /// 使用方式：
    /// @code
    /// // 初始化（程序启动时调用一次）
    /// Logger::Instance().Initialize(LogLevel::Info);
    ///
    /// // 记录日志（兼容旧代码的单参数格式）
    /// LOG("调试信息: " + std::string(value));
    ///
    /// // 或使用流式语法
    /// LOG("调试信息: " << value);
    ///
    /// // 使用级别特定宏
    /// LOG_DEBUG("调试信息: " << value);
    /// LOG_INFO("普通信息");
    /// LOG_WARN("警告信息");
    /// LOG_ERROR("错误信息: " << error_code);
    /// LOG_FATAL("致命错误");
    ///
    /// // 关闭（程序退出时调用）
    /// Logger::Instance().Shutdown();
    /// @endcode
    class Logger
    {
    public:
        /// 获取单例实例
        static Logger& Instance();

        /// 初始化日志系统
        /// @param level 最低输出级别
        /// @param logDir 日志目录，默认为 "./logs/"
        /// @param fileOutput 是否启用文件输出，默认为 true
        /// @return true 初始化成功，false 初始化失败
        bool Initialize(LogLevel level = LogLevel::Info, const std::string& logDir = "./logs/", bool fileOutput = true);

        /// 关闭日志系统
        void Shutdown();

        /// 设置日志级别
        void SetLevel(LogLevel level);

        /// 获取当前日志级别
        LogLevel GetLevel() const;

        /// 从字符串获取日志级别
        /// @param levelStr 日志级别字符串 (debug, info, warn, error, fatal)
        /// @return 解析成功返回 true
        static bool ParseLevel(const std::string& levelStr, LogLevel& level);

        /// 设置是否输出到控制台
        void SetConsoleOutput(bool enable);

        /// 设置是否输出到文件
        void SetFileOutput(bool enable);

        /// 核心日志输出函数
        /// 此函数是线程安全的，且设计为最小化性能影响：
        /// 1. 先在调用线程中格式化消息（无锁）
        /// 2. 然后获取锁并输出（最小锁持有时间）
        void Log(LogLevel level, const char* file, int line, const char* func, const std::string& message);

        /// 检查指定级别是否会被输出（用于性能敏感代码中避免不必要的字符串格式化）
        bool ShouldLog(LogLevel level) const;

    private:
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        static const char* LevelToString(LogLevel level);  ///< 获取日志级别名称
        static std::string GetTimestamp();                  ///< 获取当前时间戳字符串
        static std::string GetThreadId();                   ///< 获取当前线程ID
        bool CreateLogFile(const std::string& logDir);      ///< 创建日志文件并处理轮转
        void RotateOldLogs(const std::string& logDir, int maxFiles);  ///< 清理旧日志
        static const char* ExtractFileName(const char* path);         ///< 提取文件名（不含路径）

        std::atomic<LogLevel> m_level{LogLevel::Info};
        std::atomic<bool> m_consoleOutput{true};
        std::atomic<bool> m_fileOutput{true};
        std::mutex m_mutex;
        std::ofstream m_fileStream;
        std::string m_currentLogFile;
        bool m_initialized{false};
    };

} // namespace Util

// ============================================================================
// 全局命名空间便捷访问
// ============================================================================

// 在全局命名空间提供便捷类型，方便各模块使用
using Logger = Util::Logger;
using LogLevel = Util::LogLevel;

// ============================================================================
// 日志宏定义
// ============================================================================

/// 内部日志实现宏（不直接使用，请使用 LOG、LOG_DEBUG 等宏）
#define LOG_IMPL(level, message)                                  \
    do                                                            \
    {                                                             \
        if (Logger::Instance().ShouldLog(level))                  \
        {                                                         \
            std::ostringstream _oss;                              \
            _oss << message;                                      \
            Logger::Instance().Log(                               \
                level, __FILE__, __LINE__, __func__, _oss.str()); \
        }                                                         \
    } while (0)

/// 兼容旧代码的 LOG 宏（单参数，默认 INFO 级别）
/// 支持两种格式：
/// 1. 字符串拼接：LOG("Error: " + msg)
/// 2. 流式语法：LOG("Error: " << code)
#define LOG(message) LOG_IMPL(LogLevel::Info, message)

/// 级别特定日志宏
#define LOG_DEBUG(message) LOG_IMPL(LogLevel::Debug, message)
#define LOG_INFO(message) LOG_IMPL(LogLevel::Info, message)
#define LOG_WARN(message) LOG_IMPL(LogLevel::Warning, message)
#define LOG_ERROR(message) LOG_IMPL(LogLevel::Error, message)
#define LOG_FATAL(message) LOG_IMPL(LogLevel::Fatal, message)

/// 带级别的日志宏（双参数版本）
#define LOG_LEVEL(level, message) LOG_IMPL(level, message)
