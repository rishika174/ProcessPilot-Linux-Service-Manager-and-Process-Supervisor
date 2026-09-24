#pragma once

#include <string>
#include <mutex>
#include <fstream>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace processpilot {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void init(const std::string& logFilePath = "", LogLevel minLevel = LogLevel::INFO, bool consoleOutput = true);
    void log(LogLevel level, const std::string& module, const std::string& message);
    void setMinLevel(LogLevel level);

    static std::string levelToString(LogLevel level);
    static std::string getTimestamp();

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex logMutex_;
    std::ofstream logFile_;
    LogLevel minLevel_{LogLevel::INFO};
    bool consoleOutput_{true};
    bool fileOutputEnabled_{false};
};

} // namespace processpilot

#define LOG_DEBUG(module, msg) processpilot::Logger::getInstance().log(processpilot::LogLevel::DEBUG, module, msg)
#define LOG_INFO(module, msg)  processpilot::Logger::getInstance().log(processpilot::LogLevel::INFO,  module, msg)
#define LOG_WARN(module, msg)  processpilot::Logger::getInstance().log(processpilot::LogLevel::WARN,  module, msg)
#define LOG_ERROR(module, msg) processpilot::Logger::getInstance().log(processpilot::LogLevel::ERROR, module, msg)
#define LOG_CRIT(module, msg)  processpilot::Logger::getInstance().log(processpilot::LogLevel::CRITICAL, module, msg)
