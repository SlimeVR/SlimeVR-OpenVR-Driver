// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#include "PreciseSleeper.hpp"

#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <timeapi.h>
#else  // !_WIN32
#include <time.h>
#endif // !_WIN32

#ifdef _WIN32
/**
 * @brief RAII helper wrapping timeBeginPeriod/timeEndPeriod.
 */
class ScopedTimerResolution {
public:
    ScopedTimerResolution(UINT period)
        : period_(period) {
        timeBeginPeriod(period);
    }
    ~ScopedTimerResolution() {
        timeEndPeriod(period_);
    }

private:
    UINT period_;
};
#endif

PreciseSleeper::PreciseSleeper() {
#ifdef _WIN32
    timer_ = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, SYNCHRONIZE | TIMER_MODIFY_STATE);
    if (timer_ == NULL) {
        int err = GetLastError();
        throw std::system_error(err, std::system_category(), "CreateWaitableTimerExW() failed");
    }
#endif
}

PreciseSleeper::~PreciseSleeper() noexcept {
#ifdef _WIN32
    if (timer_ != NULL) {
        CloseHandle(timer_);
    }
#endif
}

void PreciseSleeper::SleepFor(int64_t s, int64_t ns) {
#ifdef _WIN32
    ScopedTimerResolution _(1);

    LARGE_INTEGER sleep_time;
    // According to SetWaitableTimer, lpDueTime is in 100ns intervals.
    sleep_time.QuadPart = -(((s * 1000 * 1000) + ns) / 100);

    if (SetWaitableTimer(timer_, &sleep_time, 0, NULL, NULL, false) == 0) {
        int err = GetLastError();
        throw std::system_error(err, std::system_category(), "SetWaitableTimer() failed");
    }

    if (WaitForSingleObject(timer_, INFINITE) != WAIT_OBJECT_0) {
        int err = GetLastError();
        throw std::system_error(err, std::system_category(), "WaitForSingleObject() failed");
    }
#else
    struct timespec ts{
        .tv_sec = s,
        .tv_nsec = ns,
    };

    int ret;
    while ((ret = nanosleep(&ts, &ts) == -1 && errno == EINTR)) { };

    if (ret == -1) {
        throw std::system_error(errno, std::system_category(), "nanosleep() failed");
    }
#endif
}