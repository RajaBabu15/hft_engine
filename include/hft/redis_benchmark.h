#pragma once

#include "hft/types.h"
#include "hft/redis_cache.h"
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <random>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <numeric>

namespace hft {

// Comprehensive Redis performance benchmarking and validation
class RedisBenchmark {
public:
    struct BenchmarkConfig {
        uint32_t num_threads = 8;
        uint32_t operations_per_thread = 100000;
        uint32_t warmup_operations = 10000;
        double read_write_ratio = 0.8;  // 80% reads, 20% writes
        uint32_t key_space_size = 10000;
        uint32_t value_size_bytes = 128;
        bool enable_pipeline = false;
        uint32_t pipeline_size = 100;
        std::string test_name = "redis_benchmark";
        bool save_results = true;
    };
    
    struct BenchmarkResults {
        // Throughput metrics
        double operations_per_second = 0.0;
        double reads_per_second = 0.0;
        double writes_per_second = 0.0;
        
        // Latency metrics (microseconds)
        double avg_latency_us = 0.0;
        double p50_latency_us = 0.0;
        double p95_latency_us = 0.0;
        double p99_latency_us = 0.0;
        double p999_latency_us = 0.0;
        double min_latency_us = 0.0;
        double max_latency_us = 0.0;
        
        // Cache metrics
        double cache_hit_ratio = 0.0;
        uint64_t cache_hits = 0;
        uint64_t cache_misses = 0;
        uint64_t total_operations = 0;
        
        // Performance comparison
        double improvement_factor = 1.0;  // vs baseline
        bool achieved_30x_improvement = false;
        
        uint64_t test_duration_ns = 0;
        std::string timestamp;
        std::string test_configuration;
    };

private:
    BenchmarkConfig config_;
    std::unique_ptr<RedisCache> redis_cache_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> operations_completed_{0};
    
    // Thread-safe latency tracking
    mutable std::mutex latency_mutex_;
    std::vector<double> latency_samples_;
    
public:
    explicit RedisBenchmark(const BenchmarkConfig& config = BenchmarkConfig{})
        : config_(config) {
        
        RedisCache::Config redis_config;
        redis_config.enable_pipeline = config.enable_pipeline;
        redis_config.pool_size = config.num_threads;
        redis_config.timeout_ms = 1;  // Ultra-low timeout for HFT
        
        redis_cache_ = std::make_unique<RedisCache>(redis_config);
    }
    
    // Run comprehensive benchmark suite
    BenchmarkResults run_full_benchmark() {
        std::cout << " Starting Redis Performance Benchmark Suite" << std::endl;
        std::cout << "=============================================" << std::endl;
        
        BenchmarkResults results;
        
        // Baseline test (without Redis)
        std::cout << " Running baseline test (no caching)..." << std::endl;
        auto baseline_results = run_baseline_test();
        
        // Redis enabled test
        std::cout << " Running Redis-enabled test..." << std::endl;
        auto redis_results = run_redis_test();
        
        // Calculate improvement factor
        results = redis_results;
        if (baseline_results.operations_per_second > 0) {
            results.improvement_factor = redis_results.operations_per_second / baseline_results.operations_per_second;
            results.achieved_30x_improvement = results.improvement_factor >= 30.0;
        }
        
        // Display comprehensive results
        print_comparison_report(baseline_results, redis_results);
        
        if (config_.save_results) {
            save_benchmark_results(results);
        }
        
        return results;
    }
    
    // Run performance validation tests
    bool validate_30x_improvement() {
        std::cout << " Validating 30x Performance Improvement Claim" << std::endl;
        std::cout << "================================================" << std::endl;
        
        auto results = run_full_benchmark();
        
        std::cout << "\n VALIDATION RESULTS:" << std::endl;
        std::cout << "Improvement Factor: " << std::fixed << std::setprecision(1) 
                  << results.improvement_factor << "x" << std::endl;
        std::cout << "30x Target: " << (results.achieved_30x_improvement ? " ACHIEVED" : " NOT ACHIEVED") 
                  << std::endl;
        
        if (results.achieved_30x_improvement) {
            std::cout << " Successfully validated 30x+ throughput improvement!" << std::endl;
        } else {
            std::cout << "  30x improvement not achieved in this test environment." << std::endl;
            std::cout << "    Note: Results may vary based on Redis deployment, network, and hardware." << std::endl;
        }
        
        return results.achieved_30x_improvement;
    }
    
    // Stress test Redis under high load
    BenchmarkResults run_stress_test(uint32_t target_ops_per_sec = 1000000) {
        std::cout << "🔥 Running Redis Stress Test" << std::endl;
        std::cout << "Target: " << target_ops_per_sec << " operations/sec" << std::endl;
        
        BenchmarkConfig stress_config = config_;
        stress_config.num_threads = 16;
        stress_config.operations_per_thread = target_ops_per_sec / stress_config.num_threads;
        
        return run_performance_test("stress_test", stress_config);
    }

private:
    BenchmarkResults run_baseline_test() {
        BenchmarkConfig baseline_config = config_;
        baseline_config.test_name = "baseline";
        
        // Disable Redis for baseline
        redis_cache_->enable_caching(false);
        
        auto results = run_performance_test("baseline", baseline_config);
        
        // Re-enable Redis
        redis_cache_->enable_caching(true);
        
        return results;
    }
    
    BenchmarkResults run_redis_test() {
        return run_performance_test("redis_enabled", config_);
    }
    
    BenchmarkResults run_performance_test(const std::string& test_name, const BenchmarkConfig& test_config) {
        std::cout << "🧪 Running test: " << test_name << std::endl;
        std::cout << "  Threads: " << test_config.num_threads << std::endl;
        std::cout << "  Operations per thread: " << test_config.operations_per_thread << std::endl;
        std::cout << "  Total operations: " << test_config.num_threads * test_config.operations_per_thread << std::endl;
        
        BenchmarkResults results;
        results.test_configuration = test_name;
        results.timestamp = get_current_timestamp();
        
        // Reset counters
        operations_completed_.store(0, std::memory_order_relaxed);
        redis_cache_->clear_stats();
        
        {
            std::lock_guard<std::mutex> lock(latency_mutex_);
            latency_samples_.clear();
            latency_samples_.reserve(test_config.num_threads * test_config.operations_per_thread);
        }
        
        // Warmup phase
        run_warmup(test_config);
        
        // Main test
        running_.store(true, std::memory_order_release);
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (uint32_t i = 0; i < test_config.num_threads; ++i) {
            threads.emplace_back([this, &test_config, i]() {
                benchmark_thread(test_config, i);
            });
        }
        
        // Wait for completion
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        results.test_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time).count();
        
        // Calculate results
        calculate_benchmark_results(results, test_config);
        
        return results;
    }
    
    void run_warmup(const BenchmarkConfig& test_config) {
        std::cout << "🔥 Warming up (" << test_config.warmup_operations << " operations)..." << std::endl;
        
        std::vector<std::thread> warmup_threads;
        for (uint32_t i = 0; i < test_config.num_threads; ++i) {
            warmup_threads.emplace_back([this, &test_config]() {
                warmup_thread(test_config);
            });
        }
        
        for (auto& thread : warmup_threads) {
            thread.join();
        }
    }
    
    void warmup_thread(const BenchmarkConfig& config) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> key_dist(1, config.key_space_size);
        std::uniform_real_distribution<> operation_dist(0.0, 1.0);
        
        for (uint32_t i = 0; i < config.warmup_operations / config.num_threads; ++i) {
            std::string key = "warmup_key_" + std::to_string(key_dist(gen));
            std::string value = generate_random_value(config.value_size_bytes);
            
            if (operation_dist(gen) < config.read_write_ratio) {
                // Read operation
                std::string cached_value;
                redis_cache_->get(key, cached_value);
            } else {
                // Write operation
                redis_cache_->set(key, value);
            }
        }
    }
    
    void benchmark_thread(const BenchmarkConfig& config, uint32_t thread_id) {
        std::random_device rd;
        std::mt19937 gen(rd() + thread_id);
        std::uniform_int_distribution<> key_dist(1, config.key_space_size);
        std::uniform_real_distribution<> operation_dist(0.0, 1.0);
        
        std::vector<double> thread_latencies;
        thread_latencies.reserve(config.operations_per_thread);
        
        for (uint32_t i = 0; i < config.operations_per_thread && running_.load(std::memory_order_relaxed); ++i) {
            auto operation_start = std::chrono::high_resolution_clock::now();
            
            std::string key = "bench_key_" + std::to_string(key_dist(gen));
            
            if (operation_dist(gen) < config.read_write_ratio) {
                // Read operation
                std::string cached_value;
                redis_cache_->get(key, cached_value);
            } else {
                // Write operation
                std::string value = generate_random_value(config.value_size_bytes);
                redis_cache_->set(key, value);
            }
            
            auto operation_end = std::chrono::high_resolution_clock::now();
            double latency_us = std::chrono::duration_cast<std::chrono::nanoseconds>(
                operation_end - operation_start).count() / 1000.0;
            
            thread_latencies.push_back(latency_us);
            operations_completed_.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Add thread latencies to global collection
        {
            std::lock_guard<std::mutex> lock(latency_mutex_);
            latency_samples_.insert(latency_samples_.end(), 
                                  thread_latencies.begin(), thread_latencies.end());
        }
    }
    
    void calculate_benchmark_results(BenchmarkResults& results, const BenchmarkConfig& config) {
        results.total_operations = operations_completed_.load(std::memory_order_relaxed);
        
        // Calculate throughput
        double duration_sec = results.test_duration_ns / 1e9;
        results.operations_per_second = results.total_operations / duration_sec;
        
        uint64_t expected_reads = static_cast<uint64_t>(results.total_operations * config.read_write_ratio);
        uint64_t expected_writes = results.total_operations - expected_reads;
        
        results.reads_per_second = expected_reads / duration_sec;
        results.writes_per_second = expected_writes / duration_sec;
        
        // Calculate latency statistics
        {
            std::lock_guard<std::mutex> lock(latency_mutex_);
            if (!latency_samples_.empty()) {
                std::sort(latency_samples_.begin(), latency_samples_.end());
                
                results.min_latency_us = latency_samples_.front();
                results.max_latency_us = latency_samples_.back();
                
                double sum = std::accumulate(latency_samples_.begin(), latency_samples_.end(), 0.0);
                results.avg_latency_us = sum / latency_samples_.size();
                
                size_t size = latency_samples_.size();
                results.p50_latency_us = latency_samples_[size * 50 / 100];
                results.p95_latency_us = latency_samples_[size * 95 / 100];
                results.p99_latency_us = latency_samples_[size * 99 / 100];
                results.p999_latency_us = latency_samples_[size * 999 / 1000];
            }
        }
        
        // Cache statistics
        const auto& cache_stats = redis_cache_->get_stats();
        results.cache_hits = cache_stats.cache_hits.load(std::memory_order_relaxed);
        results.cache_misses = cache_stats.cache_misses.load(std::memory_order_relaxed);
        results.cache_hit_ratio = cache_stats.get_hit_ratio();
    }
    
    void print_comparison_report(const BenchmarkResults& baseline, const BenchmarkResults& redis_results) {
        std::cout << "\n REDIS PERFORMANCE COMPARISON REPORT" << std::endl;
        std::cout << "=======================================" << std::endl;
        
        std::cout << "\n🏃‍♂️ THROUGHPUT COMPARISON:" << std::endl;
        std::cout << "  Baseline:     " << std::fixed << std::setprecision(0) 
                  << baseline.operations_per_second << " ops/sec" << std::endl;
        std::cout << "  Redis:        " << redis_results.operations_per_second << " ops/sec" << std::endl;
        std::cout << "  Improvement:  " << std::setprecision(1) 
                  << redis_results.improvement_factor << "x" << std::endl;
        
        std::cout << "\n⏱️  LATENCY COMPARISON (microseconds):" << std::endl;
        std::cout << "                Baseline    Redis      Improvement" << std::endl;
        std::cout << "  Average:      " << std::setw(8) << std::setprecision(2) << baseline.avg_latency_us
                  << "    " << std::setw(8) << redis_results.avg_latency_us
                  << "    " << std::setw(6) << std::setprecision(1) 
                  << (baseline.avg_latency_us / redis_results.avg_latency_us) << "x" << std::endl;
        std::cout << "  P99:          " << std::setw(8) << std::setprecision(2) << baseline.p99_latency_us
                  << "    " << std::setw(8) << redis_results.p99_latency_us
                  << "    " << std::setw(6) << std::setprecision(1) 
                  << (baseline.p99_latency_us / redis_results.p99_latency_us) << "x" << std::endl;
        
        std::cout << "\n💾 CACHE PERFORMANCE:" << std::endl;
        std::cout << "  Hit Ratio:    " << std::fixed << std::setprecision(1) 
                  << redis_results.cache_hit_ratio * 100.0 << "%" << std::endl;
        std::cout << "  Cache Hits:   " << redis_results.cache_hits << std::endl;
        std::cout << "  Cache Misses: " << redis_results.cache_misses << std::endl;
        
        std::cout << "\n TARGET VALIDATION:" << std::endl;
        std::cout << "  30x Improvement: " << (redis_results.achieved_30x_improvement ? " ACHIEVED" : " NOT ACHIEVED") 
                  << std::endl;
        
        if (redis_results.improvement_factor >= 10.0) {
            std::cout << " Excellent performance improvement achieved!" << std::endl;
        } else if (redis_results.improvement_factor >= 5.0) {
            std::cout << " Good performance improvement achieved!" << std::endl;
        } else {
            std::cout << "  Consider optimizing Redis configuration for better performance." << std::endl;
        }
    }
    
    void save_benchmark_results(const BenchmarkResults& results) {
        std::string filename = "redis_benchmark_" + results.timestamp + ".json";
        std::ofstream file(filename);
        
        if (!file.is_open()) {
            std::cerr << "Failed to save benchmark results to " << filename << std::endl;
            return;
        }
        
        file << "{\n";
        file << "  \"timestamp\": \"" << results.timestamp << "\",\n";
        file << "  \"test_configuration\": \"" << results.test_configuration << "\",\n";
        file << "  \"test_duration_seconds\": " << results.test_duration_ns / 1e9 << ",\n";
        file << "  \"throughput\": {\n";
        file << "    \"operations_per_second\": " << results.operations_per_second << ",\n";
        file << "    \"reads_per_second\": " << results.reads_per_second << ",\n";
        file << "    \"writes_per_second\": " << results.writes_per_second << "\n";
        file << "  },\n";
        file << "  \"latency_microseconds\": {\n";
        file << "    \"average\": " << results.avg_latency_us << ",\n";
        file << "    \"p50\": " << results.p50_latency_us << ",\n";
        file << "    \"p95\": " << results.p95_latency_us << ",\n";
        file << "    \"p99\": " << results.p99_latency_us << ",\n";
        file << "    \"p999\": " << results.p999_latency_us << ",\n";
        file << "    \"min\": " << results.min_latency_us << ",\n";
        file << "    \"max\": " << results.max_latency_us << "\n";
        file << "  },\n";
        file << "  \"cache_performance\": {\n";
        file << "    \"hit_ratio\": " << results.cache_hit_ratio << ",\n";
        file << "    \"cache_hits\": " << results.cache_hits << ",\n";
        file << "    \"cache_misses\": " << results.cache_misses << ",\n";
        file << "    \"total_operations\": " << results.total_operations << "\n";
        file << "  },\n";
        file << "  \"performance_improvement\": {\n";
        file << "    \"improvement_factor\": " << results.improvement_factor << ",\n";
        file << "    \"achieved_30x_target\": " << (results.achieved_30x_improvement ? "true" : "false") << "\n";
        file << "  }\n";
        file << "}\n";
        
        file.close();
        std::cout << "💾 Benchmark results saved to " << filename << std::endl;
    }
    
    std::string generate_random_value(uint32_t size_bytes) {
        static const std::string chars = 
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_int_distribution<> dis(0, chars.size() - 1);
        
        std::string result;
        result.reserve(size_bytes);
        
        for (uint32_t i = 0; i < size_bytes; ++i) {
            result += chars[dis(gen)];
        }
        
        return result;
    }
    
    std::string get_current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        return ss.str();
    }
};

} // namespace hft