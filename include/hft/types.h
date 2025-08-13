#pragma once

#include <cstdint>
#include <type_traits>
#include <chrono>
#include <thread>
#include <atomic>
#include <limits>
#include <string>
#include <fstream>
#include <iostream>
#include <cstring>
#include <ctime>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
    #include <x86intrin.h> 
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define HFT_LIKELY(x)   (__builtin_expect(!!(x), 1))
    #define HFT_UNLIKELY(x) (__builtin_expect(!!(x), 0))
    #define HFT_FORCE_INLINE __attribute__((always_inline)) inline
    #define HFT_HOT __attribute__((hot))
    #define HFT_PURE __attribute__((pure))
    #define HFT_CONST __attribute__((const))
#elif defined(_MSC_VER)
    #define HFT_LIKELY(x)   (x)
    #define HFT_UNLIKELY(x) (x)
    #define HFT_FORCE_INLINE __forceinline
    #define HFT_HOT
    #define HFT_PURE
    #define HFT_CONST
#else
    #define HFT_LIKELY(x)   (x)
    #define HFT_UNLIKELY(x) (x)
    #define HFT_FORCE_INLINE inline
    #define HFT_HOT
    #define HFT_PURE
    #define HFT_CONST
#endif

namespace hft {

    using Price     = int64_t;
    using Quantity  = int64_t;
    using OrderId   = uint64_t;
    using Timestamp = uint64_t; // ns
    using Symbol    = uint64_t;
    using UserId    = uint64_t;

    static_assert(std::is_signed<Price>::value,    "Price must be signed integer");
    static_assert(std::is_signed<Quantity>::value, "Quantity must be signed integer");

    enum class Side       : uint8_t { BUY = 0, SELL = 1 };
    enum class OrderType  : uint8_t { MARKET = 0, LIMIT = 1 };
    enum class OrderStatus: uint8_t { NEW = 0, PARTIALLY_FILLED = 1, FILLED = 2, CANCELLED = 3, REJECTED = 4 };
    enum class TimeInForce: uint8_t { GTC = 0, IOC = 1, FOK = 2 };
    
    struct alignas(64) TSCState {
        std::atomic<double> ns_per_tick{0.0};      // scale factor (0 = disabled)
        std::atomic<int64_t> offset_ns{0};         // offset for steady clock alignment
        
        // Cold path data - calibration control
        std::atomic<bool> calibrated{false};       // calibration completion flag
        
        // Padding to prevent false sharing with next cache line
        char padding[64 - sizeof(std::atomic<double>) - sizeof(std::atomic<int64_t>) - sizeof(std::atomic<bool>)];
    };

    inline TSCState& get_tsc_state() noexcept {
        static TSCState state;
        return state;
    }

    namespace detail {
        constexpr char CAL_MAGIC[8] = { 'H','F','T','T','S','C', 0, 0 };
        constexpr uint32_t CAL_VERSION = 1;

        thread_local std::string cached_path;
        thread_local bool path_valid = false;
    }

    inline bool load_tsc_calibration(const std::string& path) noexcept {
        std::ifstream ifs(path, std::ios::binary);
        if (HFT_UNLIKELY(!ifs.is_open())) return false;

        struct FileHeader {
            char magic[8];
            uint32_t version;
            double scale;
            int64_t offset;
            int64_t saved_time_ns;
        } header;
        
        ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (HFT_UNLIKELY(ifs.gcount() != sizeof(header))) return false;

        if (HFT_UNLIKELY(std::memcmp(header.magic, detail::CAL_MAGIC, sizeof(header.magic)) != 0)) return false;
        if (HFT_UNLIKELY(header.version != detail::CAL_VERSION)) return false;
        if (HFT_UNLIKELY(!(header.scale > 0.0))) return false;

        auto& state = get_tsc_state();
        state.ns_per_tick.store(header.scale, std::memory_order_relaxed);
        state.offset_ns.store(header.offset, std::memory_order_relaxed);
        state.calibrated.store(true, std::memory_order_release); 
        
        return true;
    }

    inline bool save_tsc_calibration(const std::string& path) noexcept {
        auto& state = get_tsc_state();

        double scale = state.ns_per_tick.load(std::memory_order_relaxed);
        int64_t offset = state.offset_ns.load(std::memory_order_relaxed);
        bool calibrated = state.calibrated.load(std::memory_order_acquire);
        
        if (HFT_UNLIKELY(!(scale > 0.0) || !calibrated)) return false;

        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (HFT_UNLIKELY(!ofs.is_open())) return false;

        struct FileHeader {
            char magic[8];
            uint32_t version;
            double scale;
            int64_t offset;
            int64_t saved_time_ns;
        } header;
        
        std::memcpy(header.magic, detail::CAL_MAGIC, sizeof(header.magic));
        header.version = detail::CAL_VERSION;
        header.scale = scale;
        header.offset = offset;
        header.saved_time_ns = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
                
        ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
        return ofs.good();
    }

    inline bool calibrate_tsc_with_persistence(const std::string& path, bool force_recalibrate = false, unsigned duration_ms = 200) noexcept {
        if (HFT_LIKELY(!force_recalibrate)) {
            if (load_tsc_calibration(path)) return true;
        }
        bool ok = calibrate_tsc(duration_ms);
        if (ok) {
            save_tsc_calibration(path);
        }
        return ok;
    }

    inline bool calibrate_tsc(unsigned duration_ms = 200) noexcept {
        #if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
            auto& state = get_tsc_state();
            
            bool expected = false;
            if (HFT_UNLIKELY(!state.calibrated.compare_exchange_strong(expected, true, std::memory_order_acq_rel))) {
                for (int spin = 0; spin < 50; ++spin) {
                    if (HFT_LIKELY(state.ns_per_tick.load(std::memory_order_relaxed) > 0.0)) {
                        return true;
                    }
                    #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause();
                    #else
                        std::this_thread::yield();
                    #endif
                }
                expected = true;
                if (state.calibrated.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
                    state.ns_per_tick.store(0.0, std::memory_order_relaxed);
                    state.offset_ns.store(0, std::memory_order_relaxed);
                }
                return false;
            }

            using namespace std::chrono;
            
            std::this_thread::sleep_for(milliseconds(1));
            
            unsigned long long tsc_start, tsc_end;
            unsigned aux;
            
            #if defined(__rdtscp)
                tsc_start = __rdtscp(&aux);
                auto start_time = steady_clock::now();
                
                if (HFT_UNLIKELY(duration_ms < 20)) duration_ms = 20;
                std::this_thread::sleep_for(milliseconds(duration_ms));
                
                auto end_time = steady_clock::now();
                tsc_end = __rdtscp(&aux);
            #else
                tsc_start = __rdtsc();
                auto start_time = steady_clock::now();
                
                if (HFT_UNLIKELY(duration_ms < 20)) duration_ms = 20;
                std::this_thread::sleep_for(milliseconds(duration_ms));
                
                auto end_time = steady_clock::now();
                tsc_end = __rdtsc();
            #endif

            const int64_t start_ns = duration_cast<nanoseconds>(start_time.time_since_epoch()).count();
            const int64_t end_ns = duration_cast<nanoseconds>(end_time.time_since_epoch()).count();
            const uint64_t tsc_delta = (tsc_end > tsc_start) ? (tsc_end - tsc_start) : 0ULL;
            const double elapsed_ns = static_cast<double>(end_ns - start_ns);
            
            if (HFT_UNLIKELY(tsc_delta == 0 || elapsed_ns <= 0.0)) {
                state.ns_per_tick.store(0.0, std::memory_order_relaxed);
                state.offset_ns.store(0, std::memory_order_relaxed);
                state.calibrated.store(false, std::memory_order_release);
                return false;
            }

            const double scale = elapsed_ns / static_cast<double>(tsc_delta);
            
            const double mid_ns = static_cast<double>(start_ns + end_ns) * 0.5;
            const double mid_tsc = static_cast<double>(tsc_start + tsc_end) * 0.5;
            const double offset_d = mid_ns - (mid_tsc * scale);

            const double max_i64 = static_cast<double>(std::numeric_limits<int64_t>::max());
            const double min_i64 = static_cast<double>(std::numeric_limits<int64_t>::min());
            const int64_t offset_clamped = static_cast<int64_t>(
                std::min(max_i64, std::max(min_i64, offset_d)));
                
            state.ns_per_tick.store(scale, std::memory_order_relaxed);
            state.offset_ns.store(offset_clamped, std::memory_order_relaxed);
            state.calibrated.store(true, std::memory_order_release);

            return true;
        #else
            auto& state = get_tsc_state();
            state.ns_per_tick.store(0.0, std::memory_order_relaxed);
            state.offset_ns.store(0, std::memory_order_relaxed);
            state.calibrated.store(false, std::memory_order_release);
            return false;
        #endif
    }


    HFT_HOT HFT_FORCE_INLINE Timestamp now_ns() noexcept {
        #if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
            auto& state = get_tsc_state();

            const double scale = state.ns_per_tick.load(std::memory_order_relaxed);
            
            if (HFT_LIKELY(scale > 0.0)) {
                #if defined(__rdtscp)
                    unsigned aux;
                    const uint64_t tsc = __rdtscp(&aux);
                #else
                    const uint64_t tsc = __rdtsc();
                #endif
                
                const int64_t offset = state.offset_ns.load(std::memory_order_relaxed);
                
                const double ns_d = static_cast<double>(tsc) * scale + static_cast<double>(offset);
                
                if (HFT_UNLIKELY(ns_d < 0.0)) return 0;
                if (HFT_UNLIKELY(ns_d > static_cast<double>(std::numeric_limits<uint64_t>::max()))) {
                    return std::numeric_limits<uint64_t>::max();
                }
                
                return static_cast<Timestamp>(ns_d);
            }
        #endif
            using namespace std::chrono;
            return static_cast<Timestamp>(
                duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    }

    HFT_PURE HFT_FORCE_INLINE bool tsc_enabled() noexcept {
        return get_tsc_state().ns_per_tick.load(std::memory_order_relaxed) > 0.0;
    }

    HFT_HOT HFT_FORCE_INLINE uint64_t raw_tsc() noexcept {
        #if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
            #if defined(__rdtscp)
                unsigned aux;
                return __rdtscp(&aux);
            #else
                return __rdtsc();
            #endif
        #else
            return 0; 
        #endif
    }

    HFT_PURE HFT_FORCE_INLINE Timestamp tsc_to_ns(uint64_t tsc) noexcept {
        auto& state = get_tsc_state();
        const double scale = state.ns_per_tick.load(std::memory_order_relaxed);
        
        if (HFT_LIKELY(scale > 0.0)) {
            const int64_t offset = state.offset_ns.load(std::memory_order_relaxed);
            const double ns_d = static_cast<double>(tsc) * scale + static_cast<double>(offset);
            
            if (HFT_UNLIKELY(ns_d < 0.0)) return 0;
            if (HFT_UNLIKELY(ns_d > static_cast<double>(std::numeric_limits<uint64_t>::max()))) {
                return std::numeric_limits<uint64_t>::max();
            }
            
            return static_cast<Timestamp>(ns_d);
        }
        return 0;
    }

};