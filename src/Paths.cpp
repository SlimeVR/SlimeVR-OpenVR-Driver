// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "Paths.hpp"
#include <stdexcept>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#undef GetTempPath
#endif

#define SLIMEVR_IDENTIFIER "dev.slimevr.SlimeVR"

namespace fs = std::filesystem;

static fs::path getOpenVRConfigFolder() {
#if defined(_WIN32)
    const char* appData = getenv("LOCALAPPDATA");
    if (!appData) {
        throw std::runtime_error("LOCALAPPDATA is unset");
    }

    return appData;
#elif defined(__linux__)
    if (const char* dataHome = getenv("XDG_CONFIG_HOME")) {
        return dataHome;
    }

    const char* home = getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME is unset");
    }

    return fs::path(home) / ".config";
#else
#error "Unsupported platform"
#endif
}

std::filesystem::path Paths::GetOpenVRConfigPath() {
    return getOpenVRConfigFolder() / "openvr" / "openvrpaths.vrpath";
}

fs::path Paths::GetDataPath() {
    fs::path basePath{};

#if defined(__linux__)
    if (const char* dataHomeOverride = getenv("XDG_DATA_HOME")) {
        basePath = dataHomeOverride;
    } else {
        const char* homeDir = getenv("HOME");
        if (homeDir == nullptr)
            throw std::runtime_error("HOME is unset");

        basePath = fs::path(homeDir) / ".local" / "share";
    }
#elif defined(_WIN32)
    {
        const char* appData = getenv("APPDATA");
        if (appData == nullptr)
            throw std::runtime_error("APPDATA is unset");

        basePath = appData;
    }
#else
#error "Unsupported platform"
#endif

    return basePath / SLIMEVR_IDENTIFIER;
}

fs::path Paths::GetLogPath() { return Paths::GetDataPath() / "logs"; }

fs::path Paths::GetTempPath() noexcept {
#ifdef _WIN32
    // This should match the server as long as the user's machine
    // does not have the java.io.tmpdir system property overridden.
    WCHAR tmp_dir[MAX_PATH + 1];
    GetTempPathW(std::size(tmp_dir), tmp_dir);
    return tmp_dir;
#else
    if (const char* tmp_dir = getenv("TMPDIR")) {
        return tmp_dir;
    }

    return "/tmp";
#endif
}
