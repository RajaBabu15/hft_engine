#pragma once

#include "hft/types.h"
#include "hft/latency_metrics.h"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <vector>

namespace hft {

// Token bucket rate limiter for admission control
class TokenBucket {
private:
    std::atomic<double> tokens_;
    std::atomic<uint64_t> last_refill_time_;
    double capacity_;
    double refill_rate_; // tokens per nanosecond
    
    uint64_t now_ns() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    void refill_tokens() noexcept {
        uint64_t now = now_ns();
        uint64_t last_refill = last_refill_time_.load(std::memory_order_relaxed);
        
        if (now > last_refill) {
            uint64_t time_passed = now - last_refill;
            double new_tokens = time_passed * refill_rate_;
            
            double current_tokens = tokens_.load(std::memory_order_relaxed);
            double updated_tokens = std::min(capacity_, current_tokens + new_tokens);
            
            if (tokens_.compare_exchange_weak(current_tokens, updated_tokens, 
                                            std::memory_order_relaxed)) {
                last_refill_time_.store(now, std::memory_order_relaxed);
            }
        }
    }

public:
    TokenBucket(double capacity, double rate_per_second) 
        : tokens_(capacity)
        , last_refill_time_(now_ns())
        , capacity_(capacity)
        , refill_rate_(rate_per_second / 1e9) // Convert to per nanosecond
    {}
    
    bool try_consume(double tokens = 1.0) noexcept {
        refill_tokens();
        
        double current_tokens = tokens_.load(std::memory_order_relaxed);
        while (current_tokens >= tokens) {
            if (tokens_.compare_exchange_weak(current_tokens, current_tokens - tokens,
                                            std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }
    
    void set_rate(double rate_per_second) noexcept {
        refill_rate_ = rate_per_second / 1e9;
    }
    
    void set_capacity(double capacity) noexcept {
        capacity_ = capacity;
        
        // Ensure tokens don't exceed new capacity
        double current_tokens = tokens_.load(std::memory_order_relaxed);
        while (current_tokens > capacity) {
            if (tokens_.compare_exchange_weak(current_tokens, capacity, 
                                            std::memory_order_relaxed)) {
                break;
            }
        }
    }
    
    double get_available_tokens() const noexcept {
        const_cast<TokenBucket*>(this)->refill_tokens();
        return tokens_.load(std::memory_order_relaxed);
    }
    
    double get_capacity() const noexcept {
        return capacity_;
    }
};

// Adaptive admission controller with dynamic threshold adjustment
class AdaptiveAdmissionControl {
private:
    TokenBucket order_bucket_;
    TokenBucket cancel_bucket_;
    TokenBucket market_data_bucket_;
    
    // System load metrics
    std::atomic<uint64_t> recent_latency_sum_{0};
    std::atomic<uint64_t> recent_latency_count_{0};
    std::atomic<uint64_t> last_adjustment_time_{0};
    
    // Configuration parameters
    double base_order_rate_;
    double base_cancel_rate_;
    double base_market_data_rate_;
    
    double max_order_rate_;
    double min_order_rate_;
    
    // Adaptive thresholds
    static constexpr uint64_t TARGET_LATENCY_NS = 10'000; // 10 microseconds
    static constexpr uint64_t HIGH_LATENCY_THRESHOLD_NS = 50'000; // 50 microseconds
    static constexpr uint64_t ADJUSTMENT_INTERVAL_NS = 1'000'000'000; // 1 second
    
    uint64_t now_ns() const noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    void maybe_adjust_thresholds() noexcept {
        uint64_t now = now_ns();
        uint64_t last_adjustment = last_adjustment_time_.load(std::memory_order_relaxed);
        
        if (now - last_adjustment < ADJUSTMENT_INTERVAL_NS) {
            return; // Too soon to adjust
        }
        
        if (!last_adjustment_time_.compare_exchange_weak(last_adjustment, now, 
                                                        std::memory_order_relaxed)) {
            return; // Another thread is adjusting
        }
        
        uint64_t latency_count = recent_latency_count_.exchange(0, std::memory_order_relaxed);
        uint64_t latency_sum = recent_latency_sum_.exchange(0, std::memory_order_relaxed);
        
        if (latency_count == 0) return;
        
        uint64_t avg_latency = latency_sum / latency_count;
        
        // Adjust order rate based on average latency
        double current_order_rate = get_current_order_rate();
        double adjustment_factor = 1.0;
        
        if (avg_latency > HIGH_LATENCY_THRESHOLD_NS) {
            // High latency - reduce rate aggressively
            adjustment_factor = 0.7;
        } else if (avg_latency > TARGET_LATENCY_NS) {
            // Moderate latency - reduce rate moderately
            adjustment_factor = 0.9;
        } else if (avg_latency < TARGET_LATENCY_NS / 2) {
            // Low latency - can increase rate
            adjustment_factor = 1.1;
        }
        
        double new_order_rate = std::clamp(current_order_rate * adjustment_factor,
                                         min_order_rate_, max_order_rate_);
        
        order_bucket_.set_rate(new_order_rate);
        
        // Adjust cancel rate proportionally
        double cancel_ratio = base_cancel_rate_ / base_order_rate_;
        cancel_bucket_.set_rate(new_order_rate * cancel_ratio);
    }
    
    double get_current_order_rate() const noexcept {
        // This is an approximation since we can't directly query the token bucket rate
        return base_order_rate_; // Could be enhanced to track actual rate
    }

public:
    struct Config {
        double order_rate_per_second = 10000.0;
        double cancel_rate_per_second = 5000.0;
        double market_data_rate_per_second = 100000.0;
        
        double order_burst_capacity = 100.0;
        double cancel_burst_capacity = 50.0;
        double market_data_burst_capacity = 1000.0;
        
        double min_order_rate_per_second = 1000.0;
        double max_order_rate_per_second = 50000.0;
    };
    
    AdaptiveAdmissionControl(const Config& config)
        : order_bucket_(config.order_burst_capacity, config.order_rate_per_second)
        , cancel_bucket_(config.cancel_burst_capacity, config.cancel_rate_per_second)
        , market_data_bucket_(config.market_data_burst_capacity, config.market_data_rate_per_second)
        , base_order_rate_(config.order_rate_per_second)
        , base_cancel_rate_(config.cancel_rate_per_second)
        , base_market_data_rate_(config.market_data_rate_per_second)
        , max_order_rate_(config.max_order_rate_per_second)
        , min_order_rate_(config.min_order_rate_per_second)
        , last_adjustment_time_(now_ns())
    {}
    
    AdaptiveAdmissionControl() : AdaptiveAdmissionControl(Config{}) {}
    
    // Check if order submission is allowed
    bool allow_order_submission() noexcept {
        maybe_adjust_thresholds();
        return order_bucket_.try_consume(1.0);
    }
    
    // Check if order cancellation is allowed
    bool allow_order_cancellation() noexcept {
        return cancel_bucket_.try_consume(1.0);
    }
    
    // Check if market data processing is allowed
    bool allow_market_data_processing() noexcept {
        return market_data_bucket_.try_consume(1.0);
    }
    
    // Report latency for adaptive adjustment
    void report_latency(uint64_t latency_ns) noexcept {
        recent_latency_sum_.fetch_add(latency_ns, std::memory_order_relaxed);
        recent_latency_count_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Bulk operations for better performance
    bool allow_order_batch(size_t count) noexcept {
        maybe_adjust_thresholds();
        return order_bucket_.try_consume(static_cast<double>(count));
    }
    
    // Get current system status
    struct Status {
        double order_tokens_available;
        double cancel_tokens_available;
        double market_data_tokens_available;
        double order_capacity;
        double cancel_capacity;
        double market_data_capacity;
        uint64_t recent_avg_latency_ns;
    };
    
    Status get_status() const noexcept {
        Status status;
        status.order_tokens_available = order_bucket_.get_available_tokens();
        status.cancel_tokens_available = cancel_bucket_.get_available_tokens();
        status.market_data_tokens_available = market_data_bucket_.get_available_tokens();
        status.order_capacity = order_bucket_.get_capacity();
        status.cancel_capacity = cancel_bucket_.get_capacity();
        status.market_data_capacity = market_data_bucket_.get_capacity();
        
        uint64_t count = recent_latency_count_.load(std::memory_order_relaxed);
        if (count > 0) {
            status.recent_avg_latency_ns = recent_latency_sum_.load(std::memory_order_relaxed) / count;
        } else {
            status.recent_avg_latency_ns = 0;
        }
        
        return status;
    }
    
    // Manual rate adjustment for testing/configuration
    void set_order_rate(double rate_per_second) noexcept {
        base_order_rate_ = rate_per_second;
        order_bucket_.set_rate(rate_per_second);
    }
    
    void set_cancel_rate(double rate_per_second) noexcept {
        base_cancel_rate_ = rate_per_second;
        cancel_bucket_.set_rate(rate_per_second);
    }
    
    void set_market_data_rate(double rate_per_second) noexcept {
        base_market_data_rate_ = rate_per_second;
        market_data_bucket_.set_rate(rate_per_second);
    }
    
    // Emergency controls
    void emergency_stop() noexcept {
        order_bucket_.set_rate(0.0);
        cancel_bucket_.set_rate(0.0);
        market_data_bucket_.set_rate(0.0);
    }
    
    void emergency_reset() noexcept {
        order_bucket_.set_rate(base_order_rate_);
        cancel_bucket_.set_rate(base_cancel_rate_);
        market_data_bucket_.set_rate(base_market_data_rate_);
    }
};

} // namespace hft