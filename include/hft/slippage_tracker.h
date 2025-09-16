
#pragma once

#include <atomic>
#include <cstdint>

namespace hft {

    class SlippageTracker {
    private:
        std::atomic<int64_t> total_slippage_{0};
        std::atomic<uint64_t> trade_count_{0};

    public:
        void record_trade(int64_t intended_price, int64_t executed_price, uint64_t quantity) {
            int64_t slippage = (executed_price - intended_price) * quantity;
            total_slippage_.fetch_add(slippage, std::memory_order_relaxed);
            trade_count_.fetch_add(1, std::memory_order_relaxed);
        }

        int64_t get_total_slippage() const {
            return total_slippage_.load(std::memory_order_relaxed);
        }

        double get_average_slippage() const {
            uint64_t count = trade_count_.load(std::memory_order_relaxed);
            if (count == 0) return 0.0;
            return static_cast<double>(total_slippage_.load(std::memory_order_relaxed)) / count;
        }
    };

}
