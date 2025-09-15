#pragma once

#include "hft/types.h"
#include "hft/matching_engine.h"
#include "hft/bench_tsc.h"
#include "hft/deep_profiler.h"
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <random>
#include <iostream>
#include <fstream>
#include <algorithm>

namespace hft {

class StressTester {
public:
    struct TestConfig {
        uint64_t target_messages_per_sec = 100000;
        uint32_t duration_seconds = 60;
        uint32_t num_producer_threads = 4;
        uint32_t num_symbols = 100;
        Price base_price = 100000;  // $10.00 in cents
        Quantity base_quantity = 100;
        double cancel_ratio = 0.3;  // 30% cancellations
        uint32_t warmup_seconds = 5;
        bool enable_latency_measurement = true;
        std::string results_file = "stress_test_results.json";
        
        // P99 latency targets (in nanoseconds)
        uint64_t p99_target_ns = 100000;  // 100 microseconds
        uint64_t p95_target_ns = 50000;   // 50 microseconds
        uint64_t p50_target_ns = 10000;   // 10 microseconds
    };

    struct TestResults {
        uint64_t total_messages_sent = 0;
        uint64_t total_messages_processed = 0;
        uint64_t total_trades = 0;
        uint64_t total_accepts = 0;
        uint64_t total_rejects = 0;
        
        double actual_throughput_msg_per_sec = 0.0;
        double processing_throughput_msg_per_sec = 0.0;
        
        // Latency statistics (nanoseconds)
        uint64_t min_latency_ns = UINT64_MAX;
        uint64_t max_latency_ns = 0;
        double avg_latency_ns = 0.0;
        uint64_t p50_latency_ns = 0;
        uint64_t p95_latency_ns = 0;
        uint64_t p99_latency_ns = 0;
        uint64_t p999_latency_ns = 0;
        
        // Target achievement
        bool achieved_throughput_target = false;
        bool achieved_p99_target = false;
        bool achieved_p95_target = false;
        bool achieved_p50_target = false;
        
        uint64_t test_duration_ns = 0;
        std::vector<uint64_t> latency_samples;
        std::string timestamp;
    };

private:
    struct OrderGenerator {
        std::mt19937 rng;
        std::uniform_int_distribution<Symbol> symbol_dist;
        std::uniform_int_distribution<int> side_dist;
        std::uniform_int_distribution<Price> price_dist;
        std::uniform_int_distribution<Quantity> qty_dist;
        std::uniform_real_distribution<double> cancel_dist;
        
        std::atomic<uint64_t> order_id_counter{1};
        std::vector<OrderId> active_orders;
        std::mutex active_orders_mutex;
        
        OrderGenerator(const TestConfig& config) 
            : rng(std::random_device{}()),
              symbol_dist(1, config.num_symbols),
              side_dist(0, 1),
              price_dist(config.base_price - 1000, config.base_price + 1000),
              qty_dist(config.base_quantity / 2, config.base_quantity * 2),
              cancel_dist(0.0, 1.0) {}
        
        Order generate_order() {
            Order order;
            order.id = order_id_counter.fetch_add(1, std::memory_order_relaxed);
            order.symbol = symbol_dist(rng);
            order.side = side_dist(rng) == 0 ? Side::BUY : Side::SELL;
            order.type = OrderType::LIMIT;
            order.price = price_dist(rng);
            order.qty = qty_dist(rng);
            order.tif = TimeInForce::GTC;
            order.user_id = 1;
            order.status = OrderStatus::NEW;
            return order;
        }
        
        bool should_cancel() {
            return cancel_dist(rng) < 0.3;  // 30% cancel probability
        }
        
        void add_active_order(OrderId id) {
            std::lock_guard<std::mutex> lock(active_orders_mutex);
            active_orders.push_back(id);
        }
        
        OrderId get_cancel_order() {
            std::lock_guard<std::mutex> lock(active_orders_mutex);
            if (active_orders.empty()) return 0;
            
            std::uniform_int_distribution<size_t> idx_dist(0, active_orders.size() - 1);
            size_t idx = idx_dist(rng);
            OrderId id = active_orders[idx];
            active_orders.erase(active_orders.begin() + idx);
            return id;
        }
    };

    struct LatencyMeasurement {
        OrderId order_id;
        uint64_t sent_time_ns;
        uint64_t processed_time_ns;
        
        uint64_t latency_ns() const { 
            return processed_time_ns > sent_time_ns ? processed_time_ns - sent_time_ns : 0; 
        }
    };

    TestConfig config_;
    MatchingEngine* engine_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> test_start_time_{0};
    
    // Thread-safe latency tracking
    mutable std::mutex latency_mutex_;
    std::vector<LatencyMeasurement> latency_measurements_;

public:
    explicit StressTester(MatchingEngine& engine) : engine_(&engine) {}

    TestResults run_stress_test(const TestConfig& config = TestConfig{}) {
        config_ = config;
        
        std::cout << "🚀 Starting HFT Engine Stress Test" << std::endl;
        std::cout << "Target: " << config.target_messages_per_sec << " msg/sec for " 
                  << config.duration_seconds << " seconds" << std::endl;
        std::cout << "Threads: " << config.num_producer_threads << " producers" << std::endl;
        std::cout << "P99 Target: " << config.p99_target_ns / 1000.0 << " μs" << std::endl;
        
        // Reset engine counters
        engine_->reset_performance_counters();
        DeepProfiler::instance().reset();
        
        // Warmup phase
        std::cout << "⏰ Warming up for " << config.warmup_seconds << " seconds..." << std::endl;
        run_warmup();
        
        // Main test
        std::cout << "🔥 Starting main stress test..." << std::endl;
        auto results = run_main_test();
        
        // Analysis
        analyze_results(results);
        save_results(results);
        
        return results;
    }

private:
    void run_warmup() {
        running_.store(true, std::memory_order_release);
        
        std::vector<std::thread> warmup_threads;
        for (uint32_t i = 0; i < config_.num_producer_threads; ++i) {
            warmup_threads.emplace_back([this, i]() {
                warmup_producer_thread(i);
            });
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(config_.warmup_seconds));
        running_.store(false, std::memory_order_release);
        
        for (auto& t : warmup_threads) {
            t.join();
        }
        
        // Reset counters after warmup
        engine_->reset_performance_counters();
        messages_sent_.store(0, std::memory_order_relaxed);
    }

    TestResults run_main_test() {
        TestResults results;
        results.timestamp = get_current_timestamp();
        
        // Clear previous measurements
        {
            std::lock_guard<std::mutex> lock(latency_mutex_);
            latency_measurements_.clear();
            latency_measurements_.reserve(config_.target_messages_per_sec * config_.duration_seconds);
        }
        
        running_.store(true, std::memory_order_release);
        test_start_time_.store(now_ns(), std::memory_order_release);
        
        // Start producer threads
        std::vector<std::thread> producer_threads;
        for (uint32_t i = 0; i < config_.num_producer_threads; ++i) {
            producer_threads.emplace_back([this, i]() {
                producer_thread(i);
            });
        }
        
        // Let test run for specified duration
        auto start_time = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::seconds(config_.duration_seconds));
        
        // Stop threads
        running_.store(false, std::memory_order_release);
        for (auto& t : producer_threads) {
            t.join();
        }
        
        auto end_time = std::chrono::steady_clock::now();
        results.test_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time).count();
        
        // Collect engine statistics
        results.total_messages_sent = messages_sent_.load(std::memory_order_relaxed);
        results.total_messages_processed = engine_->accept_count() + engine_->reject_count();
        results.total_trades = engine_->trade_count();
        results.total_accepts = engine_->accept_count();
        results.total_rejects = engine_->reject_count();
        
        // Calculate throughput
        double duration_sec = results.test_duration_ns / 1e9;
        results.actual_throughput_msg_per_sec = results.total_messages_sent / duration_sec;
        results.processing_throughput_msg_per_sec = results.total_messages_processed / duration_sec;
        
        // Process latency measurements
        process_latency_measurements(results);
        
        return results;
    }

    void warmup_producer_thread(uint32_t thread_id) {
        OrderGenerator generator(config_);
        
        while (running_.load(std::memory_order_relaxed)) {
            Order order = generator.generate_order();
            engine_->submit_order(std::move(order));
            
            // Throttle to reasonable rate during warmup
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }

    void producer_thread(uint32_t thread_id) {
        OrderGenerator generator(config_);
        
        // Calculate target rate per thread
        uint64_t target_rate_per_thread = config_.target_messages_per_sec / config_.num_producer_threads;
        uint64_t nanos_per_message = 1000000000ULL / target_rate_per_thread;
        
        uint64_t next_send_time = now_ns();
        
        while (running_.load(std::memory_order_relaxed)) {
            uint64_t current_time = now_ns();
            
            if (current_time >= next_send_time) {
                if (generator.should_cancel()) {
                    // Send cancel
                    OrderId cancel_id = generator.get_cancel_order();
                    if (cancel_id != 0) {
                        if (config_.enable_latency_measurement) {
                            record_latency_start(cancel_id, current_time);
                        }
                        engine_->cancel_order(cancel_id);
                        messages_sent_.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    // Send new order
                    Order order = generator.generate_order();
                    OrderId order_id = order.id;
                    
                    if (config_.enable_latency_measurement) {
                        record_latency_start(order_id, current_time);
                    }
                    
                    if (engine_->submit_order(std::move(order))) {
                        generator.add_active_order(order_id);
                    }
                    messages_sent_.fetch_add(1, std::memory_order_relaxed);
                }
                
                next_send_time += nanos_per_message;
            } else {
                // Precision sleep - spin if very close, otherwise yield
                uint64_t diff = next_send_time - current_time;
                if (diff > 1000) {  // > 1μs
                    std::this_thread::yield();
                } else {
                    // Spin wait for precision
                    while (now_ns() < next_send_time) {
                        CPU_RELAX();
                    }
                }
            }
        }
    }

    void record_latency_start(OrderId order_id, uint64_t sent_time) {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        latency_measurements_.push_back({order_id, sent_time, 0});
    }

    void process_latency_measurements(TestResults& results) {
        std::lock_guard<std::mutex> lock(latency_mutex_);
        
        if (latency_measurements_.empty()) {
            std::cout << "⚠️  No latency measurements recorded" << std::endl;
            return;
        }
        
        // Extract valid latencies
        std::vector<uint64_t> latencies;
        latencies.reserve(latency_measurements_.size());
        
        for (const auto& measurement : latency_measurements_) {
            if (measurement.processed_time_ns > 0) {
                uint64_t latency = measurement.latency_ns();
                if (latency > 0 && latency < 1000000000ULL) {  // Sanity check: < 1 second
                    latencies.push_back(latency);
                }
            }
        }
        
        if (latencies.empty()) {
            std::cout << "⚠️  No valid latency measurements found" << std::endl;
            return;
        }
        
        std::sort(latencies.begin(), latencies.end());
        results.latency_samples = latencies;
        
        // Calculate statistics
        results.min_latency_ns = latencies.front();
        results.max_latency_ns = latencies.back();
        
        uint64_t sum = 0;
        for (uint64_t lat : latencies) sum += lat;
        results.avg_latency_ns = static_cast<double>(sum) / latencies.size();
        
        // Percentiles
        auto percentile = [&](double p) -> uint64_t {
            size_t idx = static_cast<size_t>(p * (latencies.size() - 1));
            return latencies[idx];
        };
        
        results.p50_latency_ns = percentile(0.50);
        results.p95_latency_ns = percentile(0.95);
        results.p99_latency_ns = percentile(0.99);
        results.p999_latency_ns = percentile(0.999);
    }

    void analyze_results(TestResults& results) {
        std::cout << "\n📊 STRESS TEST RESULTS" << std::endl;
        std::cout << "======================" << std::endl;
        
        // Throughput analysis
        results.achieved_throughput_target = 
            results.actual_throughput_msg_per_sec >= config_.target_messages_per_sec * 0.95;
        
        std::cout << "📈 Throughput:" << std::endl;
        std::cout << "   Sent: " << static_cast<uint64_t>(results.actual_throughput_msg_per_sec) 
                  << " msg/sec (target: " << config_.target_messages_per_sec << ")" << std::endl;
        std::cout << "   Processed: " << static_cast<uint64_t>(results.processing_throughput_msg_per_sec) 
                  << " msg/sec" << std::endl;
        std::cout << "   Target achieved: " << (results.achieved_throughput_target ? "✅" : "❌") << std::endl;
        
        // Latency analysis
        if (!results.latency_samples.empty()) {
            results.achieved_p50_target = results.p50_latency_ns <= config_.p50_target_ns;
            results.achieved_p95_target = results.p95_latency_ns <= config_.p95_target_ns;
            results.achieved_p99_target = results.p99_latency_ns <= config_.p99_target_ns;
            
            std::cout << "\n⏱️  Latency (microseconds):" << std::endl;
            std::cout << "   Min: " << results.min_latency_ns / 1000.0 << " μs" << std::endl;
            std::cout << "   Avg: " << results.avg_latency_ns / 1000.0 << " μs" << std::endl;
            std::cout << "   P50: " << results.p50_latency_ns / 1000.0 << " μs (target: " 
                      << config_.p50_target_ns / 1000.0 << ") " 
                      << (results.achieved_p50_target ? "✅" : "❌") << std::endl;
            std::cout << "   P95: " << results.p95_latency_ns / 1000.0 << " μs (target: " 
                      << config_.p95_target_ns / 1000.0 << ") " 
                      << (results.achieved_p95_target ? "✅" : "❌") << std::endl;
            std::cout << "   P99: " << results.p99_latency_ns / 1000.0 << " μs (target: " 
                      << config_.p99_target_ns / 1000.0 << ") " 
                      << (results.achieved_p99_target ? "✅" : "❌") << std::endl;
            std::cout << "   P99.9: " << results.p999_latency_ns / 1000.0 << " μs" << std::endl;
            std::cout << "   Max: " << results.max_latency_ns / 1000.0 << " μs" << std::endl;
        }
        
        // Trading statistics
        std::cout << "\n💰 Trading Statistics:" << std::endl;
        std::cout << "   Total Messages: " << results.total_messages_sent << std::endl;
        std::cout << "   Processed: " << results.total_messages_processed << std::endl;
        std::cout << "   Trades: " << results.total_trades << std::endl;
        std::cout << "   Accepts: " << results.total_accepts << std::endl;
        std::cout << "   Rejects: " << results.total_rejects << std::endl;
        
        double reject_rate = results.total_messages_processed > 0 ? 
            (double)results.total_rejects / results.total_messages_processed : 0.0;
        std::cout << "   Reject Rate: " << reject_rate * 100.0 << "%" << std::endl;
        
        // Overall assessment
        bool overall_success = results.achieved_throughput_target && 
                              results.achieved_p99_target && 
                              results.achieved_p95_target;
        
        std::cout << "\n🎯 Overall Result: " << (overall_success ? "✅ SUCCESS" : "❌ FAILED") << std::endl;
    }

    void save_results(const TestResults& results) {
        std::ofstream file(config_.results_file);
        if (!file.is_open()) {
            std::cerr << "Failed to save results to " << config_.results_file << std::endl;
            return;
        }
        
        file << "{\n";
        file << "  \"timestamp\": \"" << results.timestamp << "\",\n";
        file << "  \"test_duration_seconds\": " << results.test_duration_ns / 1e9 << ",\n";
        file << "  \"target_throughput\": " << config_.target_messages_per_sec << ",\n";
        file << "  \"actual_throughput\": " << results.actual_throughput_msg_per_sec << ",\n";
        file << "  \"processing_throughput\": " << results.processing_throughput_msg_per_sec << ",\n";
        file << "  \"total_messages_sent\": " << results.total_messages_sent << ",\n";
        file << "  \"total_messages_processed\": " << results.total_messages_processed << ",\n";
        file << "  \"total_trades\": " << results.total_trades << ",\n";
        file << "  \"total_accepts\": " << results.total_accepts << ",\n";
        file << "  \"total_rejects\": " << results.total_rejects << ",\n";
        file << "  \"latency_stats_ns\": {\n";
        file << "    \"min\": " << results.min_latency_ns << ",\n";
        file << "    \"avg\": " << results.avg_latency_ns << ",\n";
        file << "    \"p50\": " << results.p50_latency_ns << ",\n";
        file << "    \"p95\": " << results.p95_latency_ns << ",\n";
        file << "    \"p99\": " << results.p99_latency_ns << ",\n";
        file << "    \"p999\": " << results.p999_latency_ns << ",\n";
        file << "    \"max\": " << results.max_latency_ns << "\n";
        file << "  },\n";
        file << "  \"targets_achieved\": {\n";
        file << "    \"throughput\": " << (results.achieved_throughput_target ? "true" : "false") << ",\n";
        file << "    \"p50_latency\": " << (results.achieved_p50_target ? "true" : "false") << ",\n";
        file << "    \"p95_latency\": " << (results.achieved_p95_target ? "true" : "false") << ",\n";
        file << "    \"p99_latency\": " << (results.achieved_p99_target ? "true" : "false") << "\n";
        file << "  }\n";
        file << "}\n";
        
        file.close();
        std::cout << "💾 Results saved to " << config_.results_file << std::endl;
    }

    std::string get_current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }
};

} // namespace hft