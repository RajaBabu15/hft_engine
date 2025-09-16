#pragma once

#include "hft/types.h"
#include "hft/bench_tsc.h"
#include "hft/ultra_profiler.h"
#include <vector>
#include <iostream>
#include <iomanip>
#include <functional>
#include <cmath>
#include <algorithm>
#include <atomic>

namespace hft {

class MicroBenchmark {
public:
    struct BenchResult {
        std::string name;
        uint64_t min_ns;
        uint64_t max_ns;
        uint64_t median_ns;
        uint64_t p99_ns;
        double mean_ns;
        double stdev_ns;
        size_t samples;
    };
    
    template<typename F>
    static BenchResult benchmark_function(const std::string& name, F func, size_t iterations = 100000) {
        std::vector<uint64_t> timings;
        timings.reserve(iterations);
        
        // Warmup
        for (size_t i = 0; i < 10000; ++i) {
            func();
        }
        
        // Actual measurements using TSC for maximum precision
        for (size_t i = 0; i < iterations; ++i) {
            uint64_t start_tsc, end_tsc;
            
            #if defined(__x86_64__) || defined(_M_X64)
                #ifdef __rdtscp
                    unsigned aux;
                    start_tsc = __rdtscp(&aux);
                    func();
                    end_tsc = __rdtscp(&aux);
                #else
                    start_tsc = __rdtsc();
                    func();
                    end_tsc = __rdtsc();
                #endif
                
                if (end_tsc >= start_tsc) {
                    timings.push_back(end_tsc - start_tsc);
                }
            #endif
        }
        
        return analyze_timings(name, timings);
    }
    
    static void run_critical_path_benchmarks() {
        std::cout << "\n=== CRITICAL PATH MICRO-BENCHMARKS ===" << std::endl;
        std::cout << std::setw(30) << std::left << "Operation"
                  << std::setw(10) << std::right << "Min(ns)"
                  << std::setw(10) << "Median(ns)"
                  << std::setw(10) << "P99(ns)"
                  << std::setw(10) << "Max(ns)"
                  << std::setw(12) << "Mean(ns)"
                  << std::setw(12) << "Stdev(ns)"
                  << "\n";
        std::cout << std::string(104, '-') << "\n";
        
        // Benchmark TSC timing itself
        auto tsc_result = benchmark_function("TSC timing", []() {
            volatile uint64_t x = now_ns();
            (void)x;
        });
        print_result(tsc_result);
        
        // Benchmark memory allocation
        auto alloc_result = benchmark_function("Memory allocation", []() {
            void* ptr = malloc(64);
            free(ptr);
        });
        print_result(alloc_result);
        
        // Benchmark atomic operations
        std::atomic<uint64_t> atomic_counter{0};
        auto atomic_result = benchmark_function("Atomic increment", [&]() {
            atomic_counter.fetch_add(1, std::memory_order_relaxed);
        });
        print_result(atomic_result);
        
        // Benchmark cache-aligned access
        alignas(64) uint64_t cache_aligned_data[8] = {1,2,3,4,5,6,7,8};
        auto cache_result = benchmark_function("Cache-aligned access", [&]() {
            volatile uint64_t sum = cache_aligned_data[0] + cache_aligned_data[7];
            (void)sum;
        });
        print_result(cache_result);
        
        // Benchmark branch prediction
        static bool toggle = false;
        auto branch_result = benchmark_function("Branch prediction", [&]() {
            if (toggle) {
                volatile int x = 1;
                (void)x;
            } else {
                volatile int x = 2;
                (void)x;
            }
            toggle = !toggle;
        });
        print_result(branch_result);
        
        std::cout << std::string(104, '=') << "\n";
    }
    
private:
    static BenchResult analyze_timings(const std::string& name, std::vector<uint64_t>& timings) {
        if (timings.empty()) {
            return BenchResult{name, 0, 0, 0, 0, 0.0, 0.0, 0};
        }
        
        std::sort(timings.begin(), timings.end());
        
        // Convert TSC to nanoseconds
        auto& state = get_tsc_state();
        double tsc_to_ns = state.ns_per_tick.load(std::memory_order_relaxed);
        if (tsc_to_ns <= 0.0) tsc_to_ns = 1.0; // fallback
        
        BenchResult result;
        result.name = name;
        result.samples = timings.size();
        result.min_ns = static_cast<uint64_t>(timings[0] * tsc_to_ns);
        result.max_ns = static_cast<uint64_t>(timings.back() * tsc_to_ns);
        result.median_ns = static_cast<uint64_t>(timings[timings.size() / 2] * tsc_to_ns);
        result.p99_ns = static_cast<uint64_t>(timings[static_cast<size_t>(timings.size() * 0.99)] * tsc_to_ns);
        
        // Calculate mean and standard deviation
        double sum = 0.0;
        for (auto t : timings) {
            sum += t * tsc_to_ns;
        }
        result.mean_ns = sum / timings.size();
        
        double variance = 0.0;
        for (auto t : timings) {
            double diff = (t * tsc_to_ns) - result.mean_ns;
            variance += diff * diff;
        }
        result.stdev_ns = std::sqrt(variance / timings.size());
        
        return result;
    }
    
    static void print_result(const BenchResult& result) {
        std::cout << std::setw(30) << std::left << result.name
                  << std::setw(10) << std::right << std::fixed << std::setprecision(0)
                  << result.min_ns
                  << std::setw(10) << result.median_ns
                  << std::setw(10) << result.p99_ns
                  << std::setw(10) << result.max_ns
                  << std::setw(12) << std::setprecision(1) << result.mean_ns
                  << std::setw(12) << result.stdev_ns
                  << "\n";
    }
};

} // namespace hft