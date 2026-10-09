// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#pragma once

#include <memory>

#include <openvr_driver.h>

#include "VRDriver.hpp"

#ifdef WIN32
extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* interface_name, int* return_code);
#else
extern "C" void* HmdDriverFactory(const char* interface_name, int* return_code);
#endif

namespace SlimeVRDriver {
std::shared_ptr<SlimeVRDriver::VRDriver> GetDriver();
}