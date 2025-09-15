#pragma once

#include <atomic>
#include <cstdint>

namespace hft {

    class LatencyController {
    private:
        std::atomic<uint64_t> last_latency_{0};
        std::atomic<uint64_t> avg_latency_{0};
        const uint64_t latency_threshold_;
        const double ewma_alpha_ = 0.2;

    public:
        LatencyController(uint64_t threshold_ns) : latency_threshold_(threshold_ns) {}

        void record_latency(uint64_t latency_ns) {
            last_latency_.store(latency_ns, std::memory_order_relaxed);
            uint64_t current_avg = avg_latency_.load(std::memory_order_relaxed);
            uint64_t new_avg = static_cast<uint64_t>((ewma_alpha_ * latency_ns) + (1.0 - ewma_alpha_) * current_avg);
            avg_latency_.store(new_avg, std::memory_order_relaxed);
        }

        bool should_throttle() const {
            return avg_latency_.load(std::memory_order_relaxed) > latency_threshold_;
        }

        uint64_t get_average_latency() const {
            return avg_latency_.load(std::memory_order_relaxed);
        }
    };

}