// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors
#pragma once

#include <chrono>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

class PreciseSleeper {
public:
    /**
     * @brief Initialise the precise sleeper.
     *
     * @throws std::system_error system error when initialising sleeper
     */
    PreciseSleeper();
    ~PreciseSleeper() noexcept;

    template <typename Rep, typename Period>
    void SleepFor(std::chrono::duration<Rep, Period> duration) {
        auto s = std::chrono::duration_cast<std::chrono::seconds>(duration);
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration - s);
        SleepFor(s.count(), ns.count());
    }

    /**
     * @brief Block execution of the current thread for at least the requested time.
     *
     * @throws std::system_error error when setting up / waiting on timer
     */
    void SleepFor(int64_t s, int64_t ns);

    template <typename Clock, typename Duration>
        requires(std::chrono::is_clock_v<Clock>)
    void SleepUntil(std::chrono::time_point<Clock, Duration> time) {
        auto now = Clock::now();
        if (Clock::is_steady) {
            SleepFor(time - now);
        } else {
            while (now < time) {
                SleepFor(time - now);
                now = Clock::now();
            }
        }
    }

private:
#ifdef _WIN32
    HANDLE timer_;
#endif
};
