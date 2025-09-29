#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <Logger.hpp>

TEST_CASE("Sleep times") {
    const int sleep_duration_ms = 2;
    const int benchmark_duration_sec = 1;
    const int num_iterations = 1000000;
    std::vector<long long> sleep_times;
    sleep_times.reserve(num_iterations);

    auto logger = std::static_pointer_cast<Logger>(std::make_shared<ConsoleLogger>(""));
    logger->Log("Benching std::this_thread::sleep_for(std::chrono::milliseconds({}));", sleep_duration_ms);
    auto start_time = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::high_resolution_clock::now() - start_time).count() < benchmark_duration_sec) {
        auto iteration_start_time = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_duration_ms));
        auto iteration_end_time = std::chrono::high_resolution_clock::now();

        sleep_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(iteration_end_time - iteration_start_time).count());
    }
    std::sort(sleep_times.begin(), sleep_times.end());

    const size_t num_samples = sleep_times.size();
    const size_t p1_index = num_samples * 1 / 100;
    const size_t p99_index = num_samples * 99 / 100;
    const double avg_time_ms = static_cast<double>(std::accumulate(sleep_times.begin(), sleep_times.end(), 0LL)) / num_samples / 1000;
    const double p1_time_ms = static_cast<double>(sleep_times[p1_index]) / 1000;
    const double p99_time_ms = static_cast<double>(sleep_times[p99_index]) / 1000;
    logger->Log("p1:  {:.3f} ms {:.3f} tps", p1_time_ms, 1e3 / p1_time_ms);
    logger->Log("avg: {:.3f} ms {:.3f} tps", avg_time_ms, 1e3 / avg_time_ms);
    logger->Log("p99: {:.3f} ms {:.3f} tps", p99_time_ms, 1e3 / p99_time_ms);
}