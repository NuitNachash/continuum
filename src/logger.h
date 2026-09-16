#pragma once
#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel {
    INFO,
    WARN,
    ERROR,
    DEBUG
};

class Logger {
public:
    static Logger& instance();

    void init(const std::string& path);
    void log(LogLevel level, const std::string& msg);

    // Convenience methods
    void info(const std::string& msg)  { log(LogLevel::INFO,  msg); }
    void warn(const std::string& msg)  { log(LogLevel::WARN,  msg); }
    void error(const std::string& msg) { log(LogLevel::ERROR, msg); }
    void debug(const std::string& msg) { log(LogLevel::DEBUG, msg); }

private:
    Logger() = default;
    ~Logger();

    std::ofstream file_;
    std::mutex mutex_;

    std::string levelStr(LogLevel level);
    std::string timestamp();
};

// Global convenience macros
#define LOG_INFO(msg)  Logger::instance().info(msg)
#define LOG_WARN(msg)  Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
#define LOG_DEBUG(msg) Logger::instance().debug(msg)