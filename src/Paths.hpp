// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#pragma once

#include <filesystem>

#ifdef _WINDOWS_
#undef GetTempPath
#endif

namespace Paths {
std::filesystem::path GetOpenVRConfigPath();

// Throws when path cannot be found
std::filesystem::path GetDataPath() noexcept(false);

// Throws when path cannot be found
std::filesystem::path GetLogPath() noexcept(false);

std::filesystem::path GetTempPath() noexcept;
} // namespace Paths
