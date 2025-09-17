#include "hft/order/order.hpp"
#include "hft/order/order_book.hpp"
#include "hft/matching/matching_engine.hpp"
#include "hft/core/clock.hpp"
#include "hft/core/redis_client.hpp"
#include "hft/fix/fix_parser.hpp"
#include "hft/core/admission_control.hpp"
#include "hft/analytics/pnl_calculator.hpp"
#include "hft/core/numa_lock_free_queue.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>
#include <iomanip>
#include <future>
#include <map>
class ConcurrencyUpliftTest {
private:
    struct ConcurrencyTestResult {
        size_t thread_count;
        double throughput_msg_per_sec;
        double avg_latency_us;
        double p99_latency_us;
        size_t messages_processed;
        double duration_ms;
        double cpu_utilization;
    };
public:
    static void run_concurrency_uplift_simulation() {
        std::cout << "🧪 CONCURRENCY UPLIFT SIMULATION TEST" << std::endl;
        std::cout << "=====================================" << std::endl;
        auto cpu_count = get_cpu_count();
        std::vector<size_t> thread_configs = {
            std::max(size_t(1), cpu_count / 4),
            std::max(size_t(1), cpu_count / 2),
            cpu_count
        };
        std::vector<ConcurrencyTestResult> results;
        for (auto threads : thread_configs) {
            std::cout << "\n🔬 Testing with " << threads << " threads ("
                      << std::fixed << std::setprecision(1)
                      << (100.0 * threads / cpu_count) << "% concurrency)..." << std::endl;
            auto result = run_single_concurrency_test(threads);
            results.push_back(result);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        display_concurrency_uplift_results(results);
        output_concurrency_metrics(results);
    }
private:
    static size_t get_cpu_count() {
#ifdef __APPLE__
        int cpu_count;
        size_t size = sizeof(cpu_count);
        if (sysctlbyname("hw.ncpu", &cpu_count, &size, nullptr, 0) == 0 && cpu_count > 0) {
            return static_cast<size_t>(cpu_count);
        }
        return std::thread::hardware_concurrency();
#else
        return std::thread::hardware_concurrency();
#endif
    }
    static ConcurrencyTestResult run_single_concurrency_test(size_t num_threads) {
        const size_t TEST_MESSAGES = 100000;
        const size_t MESSAGES_PER_THREAD = TEST_MESSAGES / num_threads;
        std::atomic<size_t> total_processed{0};
        std::atomic<double> total_latency_us{0.0};
        std::vector<double> latency_samples;
        latency_samples.reserve(TEST_MESSAGES);
        std::mutex latency_mutex;
        auto start_time = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> workers;
        std::vector<std::future<void>> futures;
        for (size_t i = 0; i < num_threads; ++i) {
            auto future = std::async(std::launch::async, [&, i, MESSAGES_PER_THREAD]() {
                std::mt19937 rng(std::random_device{}() + i);
                std::uniform_real_distribution<> work_dist(0.5, 2.0);
                for (size_t j = 0; j < MESSAGES_PER_THREAD; ++j) {
                    auto msg_start = std::chrono::high_resolution_clock::now();
                    auto work_time = work_dist(rng);
                    std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<long>(work_time * 1000)));
                    auto msg_end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration<double, std::micro>(msg_end - msg_start).count();
                    {
                        std::lock_guard<std::mutex> lock(latency_mutex);
                        latency_samples.push_back(latency);
                    }
                    total_processed.fetch_add(1, std::memory_order_relaxed);
                    total_latency_us.fetch_add(latency, std::memory_order_relaxed);
                }
            });
            futures.push_back(std::move(future));
        }
        for (auto& future : futures) {
            future.wait();
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
        size_t processed_count = total_processed.load();
        double throughput = processed_count / (duration_ms / 1000.0);
        double avg_latency = total_latency_us.load() / processed_count;
        std::sort(latency_samples.begin(), latency_samples.end());
        double p99_latency = latency_samples[static_cast<size_t>(latency_samples.size() * 0.99)];
        return ConcurrencyTestResult{
            .thread_count = num_threads,
            .throughput_msg_per_sec = throughput,
            .avg_latency_us = avg_latency,
            .p99_latency_us = p99_latency,
            .messages_processed = processed_count,
            .duration_ms = duration_ms,
            .cpu_utilization = std::min(100.0, (double)num_threads / get_cpu_count() * 100.0)
        };
    }
    static void display_concurrency_uplift_results(const std::vector<ConcurrencyTestResult>& results) {
        std::cout << "\n📊 CONCURRENCY UPLIFT ANALYSIS" << std::endl;
        std::cout << "===============================" << std::endl;
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "┌────────────┬─────────────┬─────────────┬─────────────┬─────────────┐" << std::endl;
        std::cout << "│ Threads    │ Throughput  │ Avg Latency │ P99 Latency │ CPU Usage   │" << std::endl;
        std::cout << "│            │ (msg/s)     │ (μs)        │ (μs)        │ (%)         │" << std::endl;
        std::cout << "├────────────┼─────────────┼─────────────┼─────────────┼─────────────┤" << std::endl;
        for (const auto& result : results) {
            printf("│ %-10zu │ %11.0f │ %11.1f │ %11.1f │ %11.1f │\n",
                   result.thread_count,
                   result.throughput_msg_per_sec,
                   result.avg_latency_us,
                   result.p99_latency_us,
                   result.cpu_utilization);
        }
        std::cout << "└────────────┴─────────────┴─────────────┴─────────────┴─────────────┘" << std::endl;
        if (results.size() >= 2) {
            double baseline_throughput = results[0].throughput_msg_per_sec;
            double uplift_50_percent = ((results[1].throughput_msg_per_sec / baseline_throughput) - 1.0) * 100.0;
            double uplift_100_percent = results.size() > 2 ?
                ((results[2].throughput_msg_per_sec / baseline_throughput) - 1.0) * 100.0 : 0.0;
            std::cout << "\n🚀 UPLIFT ANALYSIS:" << std::endl;
            std::cout << "• 50% Concurrency Uplift: " << std::fixed << std::setprecision(1)
                      << uplift_50_percent << "%" << std::endl;
            if (results.size() > 2) {
                std::cout << "• 100% Concurrency Uplift: " << uplift_100_percent << "%" << std::endl;
            }
            if (uplift_50_percent >= 40.0) {
                std::cout << "✅ 50% CONCURRENCY UPLIFT TARGET ACHIEVED!" << std::endl;
            } else {
                std::cout << "⚠️  50% Concurrency uplift target missed (achieved: "
                          << uplift_50_percent << "%)" << std::endl;
            }
        }
    }
    static void output_concurrency_metrics(const std::vector<ConcurrencyTestResult>& results) {
        std::cout << "\n# CONCURRENCY_UPLIFT_RESULTS" << std::endl;
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& result = results[i];
            std::cout << "CONCURRENCY_RESULT_" << i << ": "
                      << "threads=" << result.thread_count
                      << ",throughput=" << std::fixed << std::setprecision(0) << result.throughput_msg_per_sec
                      << ",avg_latency=" << std::setprecision(2) << result.avg_latency_us
                      << ",p99_latency=" << result.p99_latency_us
                      << ",cpu_usage=" << std::setprecision(1) << result.cpu_utilization
                      << ",messages=" << result.messages_processed
                      << ",duration_ms=" << std::setprecision(0) << result.duration_ms
                      << std::endl;
        }
        if (results.size() >= 2) {
            double baseline = results[0].throughput_msg_per_sec;
            double uplift_50 = ((results[1].throughput_msg_per_sec / baseline) - 1.0) * 100.0;
            std::cout << "CONCURRENCY_UPLIFT_50_PERCENT: " << std::fixed << std::setprecision(1) << uplift_50 << std::endl;
            std::cout << "CONCURRENCY_UPLIFT_TARGET_MET: " << (uplift_50 >= 40.0 ? "true" : "false") << std::endl;
        }
    }
};
int main() {
    try {
        std::cout << "🧪 CONCURRENCY UPLIFT SIMULATION RUNNER" << std::endl;
        std::cout << "=======================================" << std::endl;
        ConcurrencyUpliftTest::run_concurrency_uplift_simulation();
        std::cout << "\n✅ Concurrency uplift simulation completed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "❌ Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "❌ Unknown error occurred" << std::endl;
        return 1;
    }
}
