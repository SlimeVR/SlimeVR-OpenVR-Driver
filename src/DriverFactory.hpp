#pragma once

#include <cstdlib>
#include <memory>

#include <openvr_driver.h>

#include <IVRDriver.hpp>

#ifdef WIN32
extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* interface_name, int* return_code);
#else
extern "C" void* HmdDriverFactory(const char* interface_name, int* return_code);
#endif

namespace SlimeVRDriver {
    std::shared_ptr<SlimeVRDriver::IVRDriver> GetDriver();
}