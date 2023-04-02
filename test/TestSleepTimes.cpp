#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>

TEST_CASE("Sleep times") {
    const int sleep_duration_ms = 2;
    const int benchmark_duration_sec = 1;
    const int num_iterations = 1000000;
    std::vector<long long> sleep_times;
    sleep_times.reserve(num_iterations);

    printf("Benching std::this_thread::sleep_for(std::chrono::milliseconds(%i));\n", sleep_duration_ms);
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
    printf("p1:  %.3lf ms %.3lf tps\n", p1_time_ms, 1e3 / p1_time_ms);
    printf("avg: %.3lf ms %.3lf tps\n", avg_time_ms, 1e3 / avg_time_ms);
    printf("p99: %.3lf ms %.3lf tps\n", p99_time_ms, 1e3 / p99_time_ms);
}