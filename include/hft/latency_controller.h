#pragma once

#include <atomic>
#include <cstdint> // for uint64_t

namespace hft {
    class LatencyController {
    private:
        const uint64_t high_latency_threshold_ = 5000ULL; // ns
        std::atomic<int> batching_size_{1};
    public:
        void update_latency_ns(uint64_t ns) {
            if (ns > high_latency_threshold_) {
                batching_size_.store(1, std::memory_order_relaxed);
            } else {
                batching_size_.store(4, std::memory_order_relaxed);
            }
        }

        int batch_size() const {
            return batching_size_.load(std::memory_order_relaxed);
        }
    };
}
