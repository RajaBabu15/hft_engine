#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>
#include "hft/types.h"

namespace hft {

    // Advanced adaptive admission control with multiple algorithms
    class LatencyController {
    public:
        enum class AdmissionAlgorithm {
            EWMA,           // Exponentially Weighted Moving Average
            PERCENTILE,     // P99 based throttling
            VEGAS,          // Vegas-style congestion control
            AIMD,           // Additive Increase, Multiplicative Decrease
            GRADIENT        // Gradient-based adaptation
        };
        
        struct ControllerConfig {
            uint64_t target_latency_ns = 100000;     // 100 μs target
            uint64_t p99_threshold_ns = 1000000;     // 1ms P99 threshold
            double ewma_alpha = 0.2;
            double aimd_increase_factor = 0.01;
            double aimd_decrease_factor = 0.5;
            uint32_t sample_window_size = 1000;
            uint32_t update_frequency = 100;          // Update every N samples
            AdmissionAlgorithm algorithm = AdmissionAlgorithm::VEGAS;
        };

    private:
        ControllerConfig config_;
        
        // Latency tracking
        std::atomic<uint64_t> last_latency_{0};
        std::atomic<uint64_t> ewma_latency_{0};
        std::atomic<uint64_t> sample_count_{0};
        
        // Advanced metrics
        std::atomic<double> admission_probability_{1.0};  // Current admission rate
        std::atomic<uint64_t> total_requests_{0};
        std::atomic<uint64_t> admitted_requests_{0};
        std::atomic<uint64_t> rejected_requests_{0};
        
        // Percentile tracking (thread-safe circular buffer)
        mutable std::mutex samples_mutex_;
        std::vector<uint64_t> latency_samples_;
        std::atomic<size_t> sample_index_{0};
        
        // Vegas algorithm state
        std::atomic<double> base_rtt_{0.0};
        std::atomic<double> expected_throughput_{0.0};
        std::atomic<double> actual_throughput_{0.0};
        
        // AIMD algorithm state
        std::atomic<double> congestion_window_{1.0};
        std::atomic<uint64_t> last_congestion_event_{0};
        
        // Gradient algorithm state
        std::atomic<double> gradient_estimate_{0.0};
        std::atomic<uint64_t> last_update_time_{0};

    public:
        explicit LatencyController(uint64_t threshold_ns) 
            : LatencyController(ControllerConfig{threshold_ns}) {}
            
        explicit LatencyController(const ControllerConfig& config)
            : config_(config), latency_samples_(config.sample_window_size, 0) {
            last_update_time_.store(now_ns(), std::memory_order_relaxed);
        }

        void record_latency(uint64_t latency_ns) {
            last_latency_.store(latency_ns, std::memory_order_relaxed);
            sample_count_.fetch_add(1, std::memory_order_relaxed);
            
            // Update EWMA
            uint64_t current_ewma = ewma_latency_.load(std::memory_order_relaxed);
            uint64_t new_ewma = static_cast<uint64_t>(
                (config_.ewma_alpha * latency_ns) + (1.0 - config_.ewma_alpha) * current_ewma);
            ewma_latency_.store(new_ewma, std::memory_order_relaxed);
            
            // Add to sample window for percentile calculations
            {
                std::lock_guard<std::mutex> lock(samples_mutex_);
                size_t idx = sample_index_.fetch_add(1, std::memory_order_relaxed) % config_.sample_window_size;
                latency_samples_[idx] = latency_ns;
            }
            
            // Update algorithm-specific state
            update_algorithm_state(latency_ns);
            
            // Periodic admission control updates
            if (sample_count_.load(std::memory_order_relaxed) % config_.update_frequency == 0) {
                update_admission_control();
            }
        }

        bool should_throttle() {
            total_requests_.fetch_add(1, std::memory_order_relaxed);
            
            bool should_admit = false;
            
            switch (config_.algorithm) {
                case AdmissionAlgorithm::EWMA:
                    should_admit = ewma_based_admission();
                    break;
                case AdmissionAlgorithm::PERCENTILE:
                    should_admit = percentile_based_admission();
                    break;
                case AdmissionAlgorithm::VEGAS:
                    should_admit = vegas_based_admission();
                    break;
                case AdmissionAlgorithm::AIMD:
                    should_admit = aimd_based_admission();
                    break;
                case AdmissionAlgorithm::GRADIENT:
                    should_admit = gradient_based_admission();
                    break;
            }
            
            if (should_admit) {
                admitted_requests_.fetch_add(1, std::memory_order_relaxed);
                return false; // Don't throttle
            } else {
                rejected_requests_.fetch_add(1, std::memory_order_relaxed);
                return true;  // Throttle
            }
        }
        
        // Performance metrics
        uint64_t get_average_latency() const {
            return ewma_latency_.load(std::memory_order_relaxed);
        }
        
        double get_admission_rate() const {
            uint64_t total = total_requests_.load(std::memory_order_relaxed);
            uint64_t admitted = admitted_requests_.load(std::memory_order_relaxed);
            return total > 0 ? static_cast<double>(admitted) / total : 1.0;
        }
        
        double get_rejection_rate() const {
            return 1.0 - get_admission_rate();
        }
        
        uint64_t get_p99_latency() const {
            std::lock_guard<std::mutex> lock(samples_mutex_);
            
            std::vector<uint64_t> sorted_samples = latency_samples_;
            std::sort(sorted_samples.begin(), sorted_samples.end());
            
            if (sorted_samples.empty()) return 0;
            
            size_t p99_idx = static_cast<size_t>(sorted_samples.size() * 0.99);
            return sorted_samples[std::min(p99_idx, sorted_samples.size() - 1)];
        }
        
        // Algorithm configuration
        void set_algorithm(AdmissionAlgorithm algorithm) {
            config_.algorithm = algorithm;
        }
        
        void set_target_latency(uint64_t target_ns) {
            config_.target_latency_ns = target_ns;
        }
        
        // Reset statistics
        void reset_stats() {
            total_requests_.store(0, std::memory_order_relaxed);
            admitted_requests_.store(0, std::memory_order_relaxed);
            rejected_requests_.store(0, std::memory_order_relaxed);
            sample_count_.store(0, std::memory_order_relaxed);
        }
        
        // Diagnostic information
        void print_diagnostics() const {
            std::cout << "\n🎛️  ADAPTIVE ADMISSION CONTROL DIAGNOSTICS" << std::endl;
            std::cout << "==========================================" << std::endl;
            std::cout << "Algorithm: " << get_algorithm_name() << std::endl;
            std::cout << "Target Latency: " << config_.target_latency_ns / 1000.0 << " μs" << std::endl;
            std::cout << "Current EWMA Latency: " << get_average_latency() / 1000.0 << " μs" << std::endl;
            std::cout << "P99 Latency: " << get_p99_latency() / 1000.0 << " μs" << std::endl;
            std::cout << "Admission Rate: " << get_admission_rate() * 100.0 << "%" << std::endl;
            std::cout << "Total Requests: " << total_requests_.load(std::memory_order_relaxed) << std::endl;
            std::cout << "Admitted: " << admitted_requests_.load(std::memory_order_relaxed) << std::endl;
            std::cout << "Rejected: " << rejected_requests_.load(std::memory_order_relaxed) << std::endl;
            
            if (config_.algorithm == AdmissionAlgorithm::AIMD) {
                std::cout << "AIMD Congestion Window: " << congestion_window_.load(std::memory_order_relaxed) << std::endl;
            } else if (config_.algorithm == AdmissionAlgorithm::VEGAS) {
                std::cout << "Vegas Base RTT: " << base_rtt_.load(std::memory_order_relaxed) / 1000.0 << " μs" << std::endl;
            }
        }

    private:
        bool ewma_based_admission() {
            uint64_t current_latency = ewma_latency_.load(std::memory_order_relaxed);
            return current_latency <= config_.target_latency_ns;
        }
        
        bool percentile_based_admission() {
            uint64_t p99 = get_p99_latency();
            return p99 <= config_.p99_threshold_ns;
        }
        
        bool vegas_based_admission() {
            double base = base_rtt_.load(std::memory_order_relaxed);
            uint64_t current = ewma_latency_.load(std::memory_order_relaxed);
            
            if (base == 0.0) {
                base_rtt_.store(current, std::memory_order_relaxed);
                return true;
            }
            
            double diff = static_cast<double>(current) - base;
            double congestion_threshold = base * 0.1;  // 10% increase threshold
            
            return diff <= congestion_threshold;
        }
        
        bool aimd_based_admission() {
            double window = congestion_window_.load(std::memory_order_relaxed);
            
            // Generate random number for probabilistic admission
            static thread_local std::random_device rd;
            static thread_local std::mt19937 gen(rd());
            static thread_local std::uniform_real_distribution<> dis(0.0, 1.0);
            
            return dis(gen) < (window / (window + 1.0));
        }
        
        bool gradient_based_admission() {
            double gradient = gradient_estimate_.load(std::memory_order_relaxed);
            double prob = admission_probability_.load(std::memory_order_relaxed);
            
            // Adjust admission probability based on gradient
            if (gradient > 0) {  // Latency increasing
                prob *= 0.95;    // Reduce admission rate
            } else {             // Latency decreasing
                prob = std::min(1.0, prob * 1.05);  // Increase admission rate
            }
            
            admission_probability_.store(prob, std::memory_order_relaxed);
            
            static thread_local std::random_device rd;
            static thread_local std::mt19937 gen(rd());
            static thread_local std::uniform_real_distribution<> dis(0.0, 1.0);
            
            return dis(gen) < prob;
        }
        
        void update_algorithm_state(uint64_t latency_ns) {
            uint64_t current_time = now_ns();
            
            switch (config_.algorithm) {
                case AdmissionAlgorithm::VEGAS:
                    update_vegas_state(latency_ns);
                    break;
                case AdmissionAlgorithm::AIMD:
                    update_aimd_state(latency_ns, current_time);
                    break;
                case AdmissionAlgorithm::GRADIENT:
                    update_gradient_state(latency_ns, current_time);
                    break;
                default:
                    break;
            }
        }
        
        void update_vegas_state(uint64_t latency_ns) {
            double current_rtt = static_cast<double>(latency_ns);
            double base = base_rtt_.load(std::memory_order_relaxed);
            
            if (base == 0.0 || current_rtt < base) {
                base_rtt_.store(current_rtt, std::memory_order_relaxed);
            }
        }
        
        void update_aimd_state(uint64_t latency_ns, uint64_t current_time) {
            double window = congestion_window_.load(std::memory_order_relaxed);
            
            if (latency_ns <= config_.target_latency_ns) {
                // Additive increase
                window += config_.aimd_increase_factor;
                congestion_window_.store(window, std::memory_order_relaxed);
            } else {
                // Multiplicative decrease
                window *= config_.aimd_decrease_factor;
                congestion_window_.store(std::max(0.1, window), std::memory_order_relaxed);
                last_congestion_event_.store(current_time, std::memory_order_relaxed);
            }
        }
        
        void update_gradient_state(uint64_t latency_ns, uint64_t current_time) {
            uint64_t last_time = last_update_time_.load(std::memory_order_relaxed);
            
            if (last_time > 0 && current_time > last_time) {
                uint64_t last_lat = last_latency_.load(std::memory_order_relaxed);
                double time_diff = static_cast<double>(current_time - last_time);
                double lat_diff = static_cast<double>(latency_ns) - static_cast<double>(last_lat);
                
                double gradient = lat_diff / time_diff;  // dLatency/dTime
                gradient_estimate_.store(gradient, std::memory_order_relaxed);
            }
            
            last_update_time_.store(current_time, std::memory_order_relaxed);
        }
        
        void update_admission_control() {
            // Periodic adjustment of admission parameters
            uint64_t current_p99 = get_p99_latency();
            
            if (current_p99 > config_.p99_threshold_ns * 1.2) {
                // Latency too high, reduce admission
                double prob = admission_probability_.load(std::memory_order_relaxed);
                admission_probability_.store(prob * 0.9, std::memory_order_relaxed);
            } else if (current_p99 < config_.target_latency_ns) {
                // Latency good, can increase admission
                double prob = admission_probability_.load(std::memory_order_relaxed);
                admission_probability_.store(std::min(1.0, prob * 1.1), std::memory_order_relaxed);
            }
        }
        
        std::string get_algorithm_name() const {
            switch (config_.algorithm) {
                case AdmissionAlgorithm::EWMA: return "EWMA";
                case AdmissionAlgorithm::PERCENTILE: return "P99-based";
                case AdmissionAlgorithm::VEGAS: return "Vegas";
                case AdmissionAlgorithm::AIMD: return "AIMD";
                case AdmissionAlgorithm::GRADIENT: return "Gradient-based";
                default: return "Unknown";
            }
        }
    };

}
