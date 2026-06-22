#include "Common/Logger.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace rgh {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::init(const std::string& logDir, LogLevel minLevel) {
    std::lock_guard<std::mutex> lock(mutex_);
    logDir_ = logDir;
    minLevel_ = minLevel;

#ifdef _WIN32
    _mkdir(logDir_.c_str());
#else
    mkdir(logDir_.c_str(), 0755);
#endif

    rotateIfNeeded();
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
    }
    return "?    ";
}

void Logger::rotateIfNeeded() {
    // 在锁内调用
    if (currentFileSize_ >= MaxFileSize) {
        if (file_.is_open()) {
            file_.close();
        }
        // 轮转：重命名旧文件
        std::string oldFile = currentLogFile_ + ".old";
#ifdef _WIN32
        DeleteFileA(oldFile.c_str());
        rename(currentLogFile_.c_str(), oldFile.c_str());
#else
        remove(oldFile.c_str());
        rename(currentLogFile_.c_str(), oldFile.c_str());
#endif
        currentFileSize_ = 0;
    }

    if (!file_.is_open()) {
        // 生成日期文件名
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        std::ostringstream oss;
        oss << logDir_ << "/rgh_"
            << std::put_time(tm, "%Y%m%d") << ".log";
        currentLogFile_ = oss.str();
        file_.open(currentLogFile_, std::ios::app);
        currentFileSize_ = 0;
        if (file_.is_open()) {
            file_.seekp(0, std::ios::end);
            currentFileSize_ = static_cast<size_t>(file_.tellp());
        }
    }
}

void Logger::log(LogLevel level, const std::string& tag, const std::string& message) {
    if (level < minLevel_) return;

    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << ms.count()
        << " [" << levelToString(level) << "]"
        << " [" << tag << "] "
        << message
        << std::endl;

    std::string line = oss.str();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        rotateIfNeeded();
        if (file_.is_open()) {
            file_ << line;
            file_.flush();
            currentFileSize_ += line.size();
        }
    }

    // 控制台输出（调试用）
    if (level >= LogLevel::Warn) {
        std::cerr << line;
    } else {
        std::cout << line;
    }

#ifdef _WIN32
    // OutputDebugString 便于调试器查看
    OutputDebugStringA(line.c_str());
#endif
}

} // namespace rgh
