#pragma once

#include <mutex>
#include <string>
#include <cstdio>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& instance();

    void init(const std::string& log_file = "", bool use_syslog = true);
    void shutdown();

    void set_level(LogLevel level);
    void log(LogLevel level, const char* file, int line, const std::string& msg);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mutex_;
    FILE* log_file_ = nullptr;
    LogLevel level_ = LogLevel::DEBUG;
    bool syslog_open_ = false;
};

#define LOG_DEBUG(fmt, ...) do { \
    char _buf[1024]; snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::instance().log(LogLevel::DEBUG, __FILE__, __LINE__, _buf); \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    char _buf[1024]; snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::instance().log(LogLevel::INFO, __FILE__, __LINE__, _buf); \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    char _buf[1024]; snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::instance().log(LogLevel::WARN, __FILE__, __LINE__, _buf); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    char _buf[1024]; snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    Logger::instance().log(LogLevel::ERROR, __FILE__, __LINE__, _buf); \
} while(0)
