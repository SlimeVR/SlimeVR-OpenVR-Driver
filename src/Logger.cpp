// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "Logger.hpp"
#include "Paths.hpp"

#include <cstdlib>

#include <chrono>
#include <filesystem>
#include <iostream>

#if defined(_WIN32) && !defined(SLIMEVR_LOGGER_USE_DRIVER_LOG)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#ifdef SLIMEVR_LOGGER_USE_DRIVER_LOG
#include <openvr_driver.h>
#endif

namespace fs = std::filesystem;

Logger::Logger(std::optional<std::string> log_file_name, std::optional<std::string> prefix)
    : prefix_(prefix)
#if defined(_WIN32) && !defined(SLIMEVR_LOGGER_USE_DRIVER_LOG)
    , should_log_to_std_streams_ = GetConsoleWindow() != nullptr
#endif
{
    UpdateLogLevel();

    if (log_file_name) {
        fs::path log_path = Paths::GetLogPath() / *log_file_name;
        log_stream_.open(log_path, std::ios::out | std::ios::app);
        if (log_stream_.fail()) {
            try {
                Warn("Failed to open log file at path {}", log_path.string());
            } catch (std::exception& e) {
                Warn("Failed to open log file");
            }
        }
    }
}

void Logger::UpdateLogLevel() {
    if (const char* log_level_env = getenv("SLIMEVR_LOG_LEVEL")) {
        char* end;
        long log_level = std::strtol(log_level_env, &end, 10);
        if (end == log_level_env) {
            // nothing converted
        } else if (*end != '\0' && !std::isspace(*end)) {
            // partially converted
        } else {
            minimum_log_level_ = SanitiseLogLevel(log_level);
        }
    }
#ifdef SLIMEVR_LOGGER_USE_DRIVER_LOG
    // Protect against segfault if we're logging before driver is activated or after cleanup
    else if (vr::VRDriverContext() != nullptr) {
        vr::EVRSettingsError err;
        int32_t log_level = vr::VRSettings()->GetInt32("driver_slimevr", "logLevel", &err);
        if (err == vr::VRSettingsError_None) {
            minimum_log_level_ = SanitiseLogLevel(log_level);
        }
    }
#endif
}

void Logger::Log(LogLevel level, const std::string& str) {
    std::lock_guard lock(mutex_);

    if (log_stream_ || log_to_std_streams_) {
        auto now = std::chrono::system_clock::now();

        std::string s = std::format("[{:%F %T%z}] [{}] {}", std::chrono::zoned_time{ std::chrono::current_zone(), now }, GetLogLevelName(level), str);

        if (log_stream_) {
            log_stream_ << s << std::endl;
        }

        if (log_to_std_streams_) {
            if (IsLogLevelImportant(level)) {
                std::cerr << s << std::endl;
            } else {
                std::cout << s << std::endl;
            }
        }
    }

#ifdef SLIMEVR_LOGGER_USE_DRIVER_LOG
    // Protect against segfault if we're logging before driver is activated or after cleanup
    if (vr::VRDriverContext() != nullptr) {
        vr::VRDriverLog()->Log(str.c_str());
    }
#endif
}