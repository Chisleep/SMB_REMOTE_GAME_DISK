#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <ctime>

namespace rgh {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

// 线程安全的日志器，支持文件 + 控制台输出，带日志轮转
class Logger {
public:
    static Logger& instance();

    // 初始化日志文件路径
    void init(const std::string& logDir, LogLevel minLevel = LogLevel::Info);

    void log(LogLevel level, const std::string& tag, const std::string& message);

    void trace(const std::string& tag, const std::string& msg) { log(LogLevel::Trace, tag, msg); }
    void debug(const std::string& tag, const std::string& msg) { log(LogLevel::Debug, tag, msg); }
    void info(const std::string& tag, const std::string& msg)  { log(LogLevel::Info, tag, msg); }
    void warn(const std::string& tag, const std::string& msg)  { log(LogLevel::Warn, tag, msg); }
    void error(const std::string& tag, const std::string& msg) { log(LogLevel::Error, tag, msg); }
    void fatal(const std::string& tag, const std::string& msg) { log(LogLevel::Fatal, tag, msg); }

    void setMinLevel(LogLevel level) { minLevel_ = level; }

private:
    Logger() = default;

    void rotateIfNeeded();
    static const char* levelToString(LogLevel level);

    std::mutex mutex_;
    std::ofstream file_;
    std::string logDir_;
    std::string currentLogFile_;
    LogLevel minLevel_ = LogLevel::Info;
    size_t currentFileSize_ = 0;
    static constexpr size_t MaxFileSize = 10 * 1024 * 1024; // 10MB 轮转
};

// 便捷日志宏
#define RGH_LOG_TRACE(tag, msg) rgh::Logger::instance().trace(tag, msg)
#define RGH_LOG_DEBUG(tag, msg) rgh::Logger::instance().debug(tag, msg)
#define RGH_LOG_INFO(tag, msg)  rgh::Logger::instance().info(tag, msg)
#define RGH_LOG_WARN(tag, msg)  rgh::Logger::instance().warn(tag, msg)
#define RGH_LOG_ERROR(tag, msg) rgh::Logger::instance().error(tag, msg)
#define RGH_LOG_FATAL(tag, msg) rgh::Logger::instance().fatal(tag, msg)

} // namespace rgh
