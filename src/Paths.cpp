// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "Paths.hpp"
#include <stdexcept>

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
