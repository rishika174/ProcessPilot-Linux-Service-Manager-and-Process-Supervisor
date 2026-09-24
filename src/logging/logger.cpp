#include "logging/logger.hpp"
#include <ctime>
#include <unistd.h>

namespace processpilot {

Logger::~Logger() {
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void Logger::init(const std::string& logFilePath, LogLevel minLevel, bool consoleOutput) {
    std::lock_guard<std::mutex> lock(logMutex_);
    minLevel_ = minLevel;
    consoleOutput_ = consoleOutput;

    if (!logFilePath.empty()) {
        logFile_.open(logFilePath, std::ios::out | std::ios::app);
        if (logFile_.is_open()) {
            fileOutputEnabled_ = true;
        } else {
            std::cerr << "[Logger] Failed to open log file: " << logFilePath << std::endl;
        }
    }
}

void Logger::setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(logMutex_);
    minLevel_ = level;
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO ";
        case LogLevel::WARN:     return "WARN ";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT ";
        default:                 return "UNK  ";
    }
}

std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm bt{};
    localtime_r(&in_time_t, &bt);

    std::ostringstream ss;
    ss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

void Logger::log(LogLevel level, const std::string& module, const std::string& message) {
    if (level < minLevel_) {
        return;
    }

    std::string timestamp = getTimestamp();
    std::string levelStr = levelToString(level);

    std::lock_guard<std::mutex> lock(logMutex_);

    // Console output with ANSI colors
    if (consoleOutput_) {
        const char* color = "\033[0m";
        switch (level) {
            case LogLevel::DEBUG:    color = "\033[36m"; break; // Cyan
            case LogLevel::INFO:     color = "\033[32m"; break; // Green
            case LogLevel::WARN:     color = "\033[33m"; break; // Yellow
            case LogLevel::ERROR:    color = "\033[31m"; break; // Red
            case LogLevel::CRITICAL: color = "\033[1;31m"; break; // Bold Red
        }

        std::cout << "\033[90m" << timestamp << "\033[0m "
                  << color << "[" << levelStr << "]\033[0m "
                  << "\033[35m[" << module << "]\033[0m "
                  << message << std::endl;
    }

    // Persistent file output
    if (fileOutputEnabled_ && logFile_.is_open()) {
        logFile_ << timestamp << " [" << levelStr << "] [" << module << "] " << message << std::endl;
        logFile_.flush();
    }
}

} // namespace processpilot
