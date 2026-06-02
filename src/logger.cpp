#include "logger.h"
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <sys/time.h>

static const char* level_str(LogLevel lv) {
    switch (lv) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() {
    shutdown();
}

void Logger::init(const std::string& log_file) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!log_file.empty()) {
        log_file_ = fopen(log_file.c_str(), "a");
        if (!log_file_) {
            fprintf(stderr, "Logger: failed to open log file %s: %s\n",
                    log_file.c_str(), strerror(errno));
        }
    }
}

void Logger::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_) {
        fclose(log_file_);
        log_file_ = nullptr;
    }
}

void Logger::set_level(LogLevel level) {
    level_ = level;
}

void Logger::log(LogLevel level, const char* file, int line, const std::string& msg) {
    if (level < level_) return;

    // Timestamp
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);

    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
             (int)(tv.tv_usec / 1000));

    // Short file name
    const char* short_file = strrchr(file, '/');
    short_file = short_file ? short_file + 1 : file;

    // Format: [timestamp] [LEVEL] (file:line) message
    char formatted[2048];
    snprintf(formatted, sizeof(formatted), "[%s] [%s] (%s:%d) %s",
             timestamp, level_str(level), short_file, line, msg.c_str());

    std::lock_guard<std::mutex> lock(mutex_);


    // Write to file
    if (log_file_) {
        fprintf(log_file_, "%s\n", formatted);
        fflush(log_file_);
    }

    // Write to stderr if no log file (fallback)
    if (!log_file_) {
        fprintf(stderr, "%s\n", formatted);
    }
}
