// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "Paths.hpp"

#include <filesystem>

#ifdef _WIN32
#include <system_error>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Shlobj.h>
#include <Windows.h>
#undef GetTempPath
#else // vvv !_WIN32
#include <stdexcept>
#endif

#define SLIMEVR_IDENTIFIER "dev.slimevr.SlimeVR"

namespace fs = std::filesystem;

// Try to create the directory and its parents without throwing
static void TryCreateDirectories(const fs::path& p) noexcept {
    std::error_code ec;
    fs::create_directories(p, ec);
}

#ifdef _WIN32
static fs::path GetKnownFolderPath(KNOWNFOLDERID id) noexcept(false) {
    PWSTR path_str;
    HRESULT ret = SHGetKnownFolderPath(id, KF_FLAG_CREATE, NULL, &path_str);
    if (ret != S_OK) {
        throw std::system_error(ret, std::system_category(), "SHGetKnownFolderPath() failed");
    }
    fs::path path(path_str);
    CoTaskMemFree(path_str);
    return path;
}
#endif

static fs::path GetOpenVRConfigFolder() {
#if defined(_WIN32)
    return GetKnownFolderPath(FOLDERID_LocalAppData);
#else
    if (const char* config_home = getenv("XDG_CONFIG_HOME")) {
        return config_home;
    }

    const char* home = getenv("HOME");
    if (!home) {
        throw std::runtime_error("HOME is unset");
    }

    return fs::path(home) / ".config";
#endif
}

std::filesystem::path Paths::GetOpenVRConfigPath() {
    return GetOpenVRConfigFolder() / "openvr" / "openvrpaths.vrpath";
}

fs::path Paths::GetDataPath() {
    fs::path base;

#ifndef _WIN32
    if (const char* data_home = getenv("XDG_DATA_HOME")) {
        base = data_home;
    } else {
        const char* home = getenv("HOME");
        if (home == nullptr)
            throw std::runtime_error("HOME is unset");

        base = fs::path(home) / ".local" / "share";
    }
#else
    base = GetKnownFolderPath(FOLDERID_RoamingAppData);
#endif

    fs::path data_path = base / SLIMEVR_IDENTIFIER;
    TryCreateDirectories(data_path);
    return data_path;
}

fs::path Paths::GetLogPath() {
    fs::path log_path = Paths::GetDataPath() / "logs";
    TryCreateDirectories(log_path);
    return log_path;
}

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
