#include "logger.h"
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() {
    if (file_.is_open())
        file_.close();
}

void Logger::init(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.open(path, std::ios::app);
    if (!file_.is_open())
        std::cerr << "[Logger] Failed to open log file: " << path << "\n";
}

void Logger::log(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string line = "[" + timestamp() + "] [" + levelStr(level) + "] " + msg + "\n";

    // Always print to stdout as well
    std::cout << line;

    if (file_.is_open()) {
        file_ << line;
        file_.flush();
    }
}

std::string Logger::levelStr(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::DEBUG: return "DEBUG";
        default:              return "INFO";
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}