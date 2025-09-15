#pragma once

#include "hft/types.h"
#include <vector>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>

namespace hft {

// Latency measurement points in the order lifecycle
enum class LatencyPoint : uint8_t {
    ORDER_SUBMISSION = 0,
    ORDER_VALIDATION,
    ORDER_MATCHING,
    ORDER_EXECUTION,
    ORDER_CANCELATION,
    MARKET_DATA_PROCESSING,
    MAX_POINTS
};

// Thread-safe histogram for latency percentiles
class LatencyHistogram {
private:
    static constexpr size_t BUCKET_COUNT = 1000;
    static constexpr uint64_t MAX_LATENCY_NS = 10'000'000; // 10ms max
    
    std::atomic<uint64_t> buckets_[BUCKET_COUNT];
    std::atomic<uint64_t> total_count_{0};
    std::atomic<uint64_t> total_sum_ns_{0};
    std::atomic<uint64_t> min_latency_ns_{UINT64_MAX};
    std::atomic<uint64_t> max_latency_ns_{0};
    
    size_t get_bucket_index(uint64_t latency_ns) const noexcept {
        if (latency_ns >= MAX_LATENCY_NS) return BUCKET_COUNT - 1;
        return (latency_ns * BUCKET_COUNT) / MAX_LATENCY_NS;
    }

public:
    LatencyHistogram() {
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            buckets_[i].store(0, std::memory_order_relaxed);
        }
    }
    
    void record_latency(uint64_t latency_ns) noexcept {
        size_t bucket = get_bucket_index(latency_ns);
        buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
        total_count_.fetch_add(1, std::memory_order_relaxed);
        total_sum_ns_.fetch_add(latency_ns, std::memory_order_relaxed);
        
        // Update min/max
        uint64_t current_max = max_latency_ns_.load(std::memory_order_relaxed);
        while (latency_ns > current_max && 
               !max_latency_ns_.compare_exchange_weak(current_max, latency_ns, 
                                                     std::memory_order_relaxed)) {}
        
        uint64_t current_min = min_latency_ns_.load(std::memory_order_relaxed);
        while (latency_ns < current_min && 
               !min_latency_ns_.compare_exchange_weak(current_min, latency_ns, 
                                                     std::memory_order_relaxed)) {}
    }
    
    // Calculate percentile (0.0 to 1.0)
    uint64_t get_percentile(double percentile) const noexcept {
        if (percentile < 0.0 || percentile > 1.0) return 0;
        
        uint64_t total = total_count_.load(std::memory_order_relaxed);
        if (total == 0) return 0;
        
        uint64_t target_count = static_cast<uint64_t>(total * percentile);
        uint64_t cumulative = 0;
        
        for (size_t i = 0; i < BUCKET_COUNT; ++i) {
            cumulative += buckets_[i].load(std::memory_order_relaxed);
            if (cumulative >= target_count) {
                return (i * MAX_LATENCY_NS) / BUCKET_COUNT;
            }
        }
        return MAX_LATENCY_NS;
    }
    
    uint64_t get_average_ns() const noexcept {
        uint64_t count = total_count_.load(std::memory_order_relaxed);
        if (count == 0) return 0;
        return total_sum_ns_.load(std::memory_order_relaxed) / count;
    }
    
    uint64_t get_min_ns() const noexcept {
        uint64_t min_val = min_latency_ns_.load(std::memory_order_relaxed);
        return min_val == UINT64_MAX ? 0 : min_val;
    }
    
    uint64_t get_max_ns() const noexcept {
        return max_latency_ns_.load(std::memory_order_relaxed);
    }
    
    uint64_t get_count() const noexcept {
        return total_count_.load(std::memory_order_relaxed);
    }
};

// Enhanced latency tracker with microsecond precision
class LatencyTracker {
private:
    std::unordered_map<LatencyPoint, std::unique_ptr<LatencyHistogram>> histograms_;
    std::mutex histogram_mutex_;
    
    // Fast timestamp function
    static uint64_t now_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

public:
    LatencyTracker() {
        for (int i = 0; i < static_cast<int>(LatencyPoint::MAX_POINTS); ++i) {
            histograms_[static_cast<LatencyPoint>(i)] = 
                std::make_unique<LatencyHistogram>();
        }
    }
    
    // RAII-style latency measurement
    class LatencyMeasurement {
    private:
        LatencyTracker* tracker_;
        LatencyPoint point_;
        uint64_t start_time_;
        
    public:
        LatencyMeasurement(LatencyTracker* tracker, LatencyPoint point) noexcept
            : tracker_(tracker), point_(point), start_time_(now_ns()) {}
        
        ~LatencyMeasurement() {
            if (tracker_) {
                uint64_t end_time = now_ns();
                tracker_->record_latency(point_, end_time - start_time_);
            }
        }
        
        // Move constructor
        LatencyMeasurement(LatencyMeasurement&& other) noexcept
            : tracker_(other.tracker_), point_(other.point_), start_time_(other.start_time_) {
            other.tracker_ = nullptr;
        }
        
        // Disable copy
        LatencyMeasurement(const LatencyMeasurement&) = delete;
        LatencyMeasurement& operator=(const LatencyMeasurement&) = delete;
        LatencyMeasurement& operator=(LatencyMeasurement&&) = delete;
    };
    
    void record_latency(LatencyPoint point, uint64_t latency_ns) noexcept {
        auto it = histograms_.find(point);
        if (it != histograms_.end()) {
            it->second->record_latency(latency_ns);
        }
    }
    
    LatencyMeasurement measure(LatencyPoint point) noexcept {
        return LatencyMeasurement(this, point);
    }
    
    // Get latency statistics for a specific point
    struct LatencyStats {
        uint64_t count = 0;
        uint64_t avg_ns = 0;
        uint64_t min_ns = 0;
        uint64_t max_ns = 0;
        uint64_t p50_ns = 0;
        uint64_t p90_ns = 0;
        uint64_t p99_ns = 0;
        uint64_t p999_ns = 0;
    };
    
    LatencyStats get_stats(LatencyPoint point) const {
        LatencyStats stats;
        auto it = histograms_.find(point);
        if (it != histograms_.end()) {
            const auto& hist = *it->second;
            stats.count = hist.get_count();
            stats.avg_ns = hist.get_average_ns();
            stats.min_ns = hist.get_min_ns();
            stats.max_ns = hist.get_max_ns();
            stats.p50_ns = hist.get_percentile(0.50);
            stats.p90_ns = hist.get_percentile(0.90);
            stats.p99_ns = hist.get_percentile(0.99);
            stats.p999_ns = hist.get_percentile(0.999);
        }
        return stats;
    }
    
    void print_report() const {
        std::cout << "\n=== LATENCY METRICS REPORT ===" << std::endl;
        std::cout << "Point\t\t\tCount\t\tAvg(μs)\t\tP50(μs)\t\tP90(μs)\t\tP99(μs)\t\tMin(μs)\t\tMax(μs)" << std::endl;
        std::cout << "--------------------------------------------------------------------------------------------------------" << std::endl;
        
        const char* point_names[] = {
            "ORDER_SUBMISSION",
            "ORDER_VALIDATION", 
            "ORDER_MATCHING",
            "ORDER_EXECUTION",
            "ORDER_CANCELATION",
            "MARKET_DATA_PROC"
        };
        
        for (int i = 0; i < static_cast<int>(LatencyPoint::MAX_POINTS); ++i) {
            LatencyPoint point = static_cast<LatencyPoint>(i);
            auto stats = get_stats(point);
            if (stats.count > 0) {
                std::cout << point_names[i] << "\t\t" 
                         << stats.count << "\t\t"
                         << stats.avg_ns / 1000.0 << "\t\t"
                         << stats.p50_ns / 1000.0 << "\t\t"
                         << stats.p90_ns / 1000.0 << "\t\t" 
                         << stats.p99_ns / 1000.0 << "\t\t"
                         << stats.min_ns / 1000.0 << "\t\t"
                         << stats.max_ns / 1000.0 << std::endl;
            }
        }
        std::cout << "--------------------------------------------------------------------------------------------------------" << std::endl;
    }
};

// Macro for convenient latency measurement
#define MEASURE_LATENCY(tracker, point) \
    auto latency_measurement = (tracker).measure(point)

} // namespace hft