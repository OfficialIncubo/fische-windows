#ifndef FISHE_APP_LOGGER_H
#define FISHE_APP_LOGGER_H

#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <vector>
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

enum class LogLevel {
    LevelDebug = 0,
    LevelInfo = 1,
    LevelWarning = 2,
    LevelError = 3
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void setMinLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_minLevel = level;
    }

    void log(LogLevel level, const std::string& filePath, int line, const std::string& func, const std::string& message) {
        if (level < m_minLevel) return;

        std::lock_guard<std::mutex> lock(m_mutex);

        std::string fileName = std::filesystem::path(filePath).filename().string();
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::string color = "\033[0m";
        std::string levelStr;

        switch (level) {
            case LogLevel::LevelDebug:   levelStr = "[DEBUG]";   color = "\033[36m"; break;
            case LogLevel::LevelInfo:    levelStr = "[INFO ]";   color = "\033[32m"; break;
            case LogLevel::LevelWarning: levelStr = "[WARN ]";   color = "\033[33m"; break;
            case LogLevel::LevelError:   levelStr = "[ERROR]";   color = "\033[31m"; break;
        }

        std::tm localTime{};
#ifdef _WIN32
        localtime_s(&localTime, &time_t_now);
#else
        localTime = *std::localtime(&time_t_now);
#endif

        std::cout << color << std::put_time(&localTime, "%H:%M:%S")
                  << "." << std::setfill('0') << std::setw(3) << ms.count() << " "
                  << levelStr << " "
                  << "[" << fileName << ":" << line << " @ " << func << "()] "
                  << "\033[0m" << message << std::endl;
    }

    template<typename... Args>
    void logFormatted(LogLevel level, const std::string& filePath, int line, const std::string& func, const char* format, Args... args) {
        if (level < m_minLevel) return;

        int size_s = std::snprintf(nullptr, 0, format, args...) + 1;
        if (size_s <= 0) return;

        std::vector<char> buf(size_s);
        std::snprintf(buf.data(), size_s, format, args...);

        log(level, filePath, line, func, std::string(buf.data()));
    }

private:
    Logger() : m_minLevel(LogLevel::LevelInfo) {
        #ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
        }
        #endif
    }

    std::mutex m_mutex;
    LogLevel m_minLevel;
};

#define FISHE_LOG_DEBUG(fmt, ...) Logger::getInstance().logFormatted(LogLevel::LevelDebug, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define FISHE_LOG_INFO(fmt, ...)  Logger::getInstance().logFormatted(LogLevel::LevelInfo,  __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define FISHE_LOG_WARN(fmt, ...)  Logger::getInstance().logFormatted(LogLevel::LevelWarning, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#define FISHE_LOG_ERROR(fmt, ...) Logger::getInstance().logFormatted(LogLevel::LevelError, __FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

#endif //FISHE_APP_LOGGER_H
