#pragma once

#include <cstdint>
#include <type_traits>
#include <chrono>
#include <thread>
#include <atomic>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#  include <x86intrin.h> // for __rdtsc / __rdtscp (only on x86)
#endif

namespace hft {

    using Price    = int64_t;   // price in ticks (integer)
    using Quantity = int64_t;   // quantity in base units
    using OrderId  = uint64_t;  // compact fixed-size id (no allocations)
    using Timestamp = uint64_t;  // ns
    using Symbol = uint64_t;  // mapped symbol id
    using UserId = uint64_t;  // user id

    static_assert(std::is_signed<Price>::value, "Price must be signed integer");
    static_assert(std::is_signed<Quantity>::value, "Quantity must be signed integer");

    enum class Side       : uint8_t { BUY = 0, SELL = 1 };
    enum class OrderType  : uint8_t { MARKET = 0, LIMIT = 1 };
    enum class OrderStatus: uint8_t { NEW = 0, PARTIALLY_FILLED = 1, FILLED = 2, CANCELLED = 3, REJECTED = 4 };
    enum class TimeInForce: uint8_t { GTC = 0, IOC = 1, FOK = 2 };
    
    inline std::atomic<double> tsc_ns_per_tick{0.0};

    // calibrate_tsc: only meaningful on x86 (TSC). On non-x86 this sets 0.0.
    inline void calibrate_tsc() noexcept {
    #if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
        using namespace std::chrono;
        auto start_time = steady_clock::now();
        unsigned long long start_tsc = __rdtsc();
        std::this_thread::sleep_for(milliseconds(200)); // short calibration
        unsigned long long end_tsc = __rdtsc();
        auto end_time = steady_clock::now();
        double elapsed_ns = static_cast<double>(duration_cast<nanoseconds>(end_time - start_time).count());
        double ticks = static_cast<double>(end_tsc - start_tsc);
        if (ticks > 0.0) {
            tsc_ns_per_tick.store(elapsed_ns / ticks, std::memory_order_release);
        } else {
            tsc_ns_per_tick.store(0.0, std::memory_order_release);
        }
    #else
        // Non-x86: indicate "no TSC" mode.
        tsc_ns_per_tick.store(0.0, std::memory_order_release);
    #endif
    }

    // now_ns(): prefer calibrated TSC on x86; otherwise fallback to steady_clock.
    inline Timestamp now_ns() noexcept {
    #if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
        double scale = tsc_ns_per_tick.load(std::memory_order_acquire);
        if (scale > 0.0) {
            unsigned long long tsc = __rdtsc();
            return static_cast<Timestamp>(static_cast<double>(tsc) * scale);
        } else {
            using namespace std::chrono;
            return static_cast<Timestamp>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
        }
    #else
        using namespace std::chrono;
        return static_cast<Timestamp>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    #endif
    }

} // namespace hft
