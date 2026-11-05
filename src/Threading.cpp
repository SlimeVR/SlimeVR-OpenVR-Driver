// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "Threading.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <pthread.h>
#endif

void Threading::SetThisThreadName(std::string name) {
#if defined(_WIN32)
    HANDLE thread = GetCurrentThread();

    int wide_name_len = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
    LPWSTR wide_name = new WCHAR[wide_name_len];
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wide_name, wide_name_len);

    SetThreadDescription(thread, wide_name);

    delete[] wide_name;
#elif defined(__linux__)
    int ret = pthread_setname_np(pthread_self(), name.c_str());
    if (ret == ERANGE) {
        pthread_setname_np(pthread_self(), name.substr(0, 15).c_str());
    }
#elif defined(__APPLE__)
    pthread_setname_np(name);
#endif
}