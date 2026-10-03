// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#pragma once
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <cassert>

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

class Logger {
private:
    static constexpr LogLevel DefaultLogLevel = LogLevel::Info;

    LogLevel minimum_log_level_ = DefaultLogLevel;
    std::optional<std::string> prefix_;
    std::ofstream log_stream_;
    std::mutex mutex_;

    /**
     * @todo: Do we still need this? I think I had an issue with the bindings provider where
     * it would hang when writing log messages when there's no console window. - Sapphire
     */
    bool log_to_std_streams_ = true;

    static inline LogLevel SanitiseLogLevel(uint64_t level) {
        if (level < std::to_underlying(LogLevel::Trace) || level > std::to_underlying(LogLevel::Fatal)) {
            // invalid
            return DefaultLogLevel;
        }
        return static_cast<LogLevel>(level);
    }

    static inline const char* GetLogLevelName(LogLevel level) {
        switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        default:
            break;
        }

        assert(false);
        return "";
    }
    static inline bool IsLogLevelImportant(LogLevel level) {
        return level >= LogLevel::Error;
    }

    void Log(LogLevel level, const std::string& str);

public:
    /**
     * @brief Initialise the logger.
     *
     * @param log_file_name name of the log file, inside the logs folder returned by @ref Paths::GetLogPath
     * @param prefix prefix before every log message
     */
    Logger(std::optional<std::string> log_file_name = std::nullopt, std::optional<std::string> prefix = std::nullopt);

    void UpdateLogLevel();

#define LOG_LEVEL_WRAPPER_BOILERPLATE(LEVEL)                             \
    template <typename... Args>                                          \
    inline void LEVEL(std::format_string<Args...> fmt, Args&&... args) { \
        if (LogLevel::LEVEL < minimum_log_level_)                        \
            return;                                                      \
        std::string s = std::format(fmt, std::forward<Args>(args)...);   \
        if (prefix_)                                                     \
            s = *prefix_ + ": " + s;                                     \
        Log(LogLevel::LEVEL, s);                                         \
    }

    LOG_LEVEL_WRAPPER_BOILERPLATE(Trace);
    LOG_LEVEL_WRAPPER_BOILERPLATE(Debug);
    LOG_LEVEL_WRAPPER_BOILERPLATE(Info);
    LOG_LEVEL_WRAPPER_BOILERPLATE(Warn);
    LOG_LEVEL_WRAPPER_BOILERPLATE(Error);
    LOG_LEVEL_WRAPPER_BOILERPLATE(Fatal);

#undef LOG_LEVEL_WRAPPER_BOILERPLATE
};
