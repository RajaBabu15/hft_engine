#pragma once

#include "hft/types.h"
#include "hft/deep_profiler.h"
#include "hft/bench_tsc.h"
#include <array>
#include <atomic>
#include <string>
#include <iostream>
#include <iomanip>

namespace hft {

// Ultra-fast timing using direct TSC instructions for microsecond-level precision
class UltraProfiler {
public:
    static constexpr size_t MAX_TIMING_POINTS = 1024;
    static constexpr size_t SAMPLE_BUFFER_SIZE = 65536;
    
    struct UltraTimingPoint {
        std::atomic<uint64_t> total_tsc{0};
        std::atomic<uint64_t> call_count{0};
        std::atomic<uint64_t> min_tsc{UINT64_MAX};
        std::atomic<uint64_t> max_tsc{0};
        alignas(64) std::array<uint64_t, 256> recent_samples{};
        std::atomic<size_t> sample_index{0};
        const char* name;
        
        UltraTimingPoint() = default;
        
        HFT_FORCE_INLINE void record_tsc(uint64_t tsc_delta) noexcept {
            total_tsc.fetch_add(tsc_delta, std::memory_order_relaxed);
            call_count.fetch_add(1, std::memory_order_relaxed);
            
            // Update min/max with relaxed ordering for performance
            uint64_t current_min = min_tsc.load(std::memory_order_relaxed);
            while (tsc_delta < current_min && 
                   !min_tsc.compare_exchange_weak(current_min, tsc_delta, std::memory_order_relaxed)) {}
            
            uint64_t current_max = max_tsc.load(std::memory_order_relaxed);
            while (tsc_delta > current_max && 
                   !max_tsc.compare_exchange_weak(current_max, tsc_delta, std::memory_order_relaxed)) {}
            
            // Store recent sample in ring buffer
            size_t idx = sample_index.fetch_add(1, std::memory_order_relaxed) % recent_samples.size();
            recent_samples[idx] = tsc_delta;
        }
        
        double get_avg_ns() const noexcept {
            uint64_t total = total_tsc.load(std::memory_order_relaxed);
            uint64_t count = call_count.load(std::memory_order_relaxed);
            if (count == 0) return 0.0;
            
            const double tsc_to_ns = get_tsc_to_ns_scale();
            return (static_cast<double>(total) * tsc_to_ns) / static_cast<double>(count);
        }
        
        uint64_t get_min_ns() const noexcept {
            uint64_t min_tsc_val = min_tsc.load(std::memory_order_relaxed);
            if (min_tsc_val == UINT64_MAX) return 0;
            return static_cast<uint64_t>(min_tsc_val * get_tsc_to_ns_scale());
        }
        
        uint64_t get_max_ns() const noexcept {
            uint64_t max_tsc_val = max_tsc.load(std::memory_order_relaxed);
            return static_cast<uint64_t>(max_tsc_val * get_tsc_to_ns_scale());
        }
        
        static double get_tsc_to_ns_scale() noexcept {
            auto& state = get_tsc_state();
            double scale = state.ns_per_tick.load(std::memory_order_relaxed);
            return (scale > 0.0) ? scale : 1.0; // fallback if TSC not calibrated
        }
        
    private:
    };
    
    static UltraProfiler& instance() {
        static UltraProfiler profiler;
        return profiler;
    }
    
    // Register a new timing point and get its ID
    static size_t register_timing_point(const char* name) {
        return register_timing_point_impl(name);
    }
    
    HFT_FORCE_INLINE static void record_timing(size_t point_id, uint64_t tsc_delta) noexcept {
        if (HFT_LIKELY(point_id < MAX_TIMING_POINTS)) {
            instance().timing_points_[point_id].record_tsc(tsc_delta);
        }
    }
    
    const UltraTimingPoint& get_timing_point(size_t point_id) const {
        return timing_points_[point_id];
    }
    
    void print_report() const {
        std::cout << "\n=== ULTRA PROFILER REPORT (TSC-based) ===\n";
        std::cout << std::setw(30) << std::left << "Function"
                  << std::setw(10) << std::right << "Calls"
                  << std::setw(12) << "Avg(ns)"
                  << std::setw(12) << "Min(ns)"
                  << std::setw(12) << "Max(ns)"
                  << std::setw(15) << "Total(ms)"
                  << "\n";
        std::cout << std::string(91, '-') << "\n";
        
        size_t active_points = 0;
        for (size_t i = 0; i < MAX_TIMING_POINTS; ++i) {
            const auto& point = timing_points_[i];
            uint64_t count = point.call_count.load(std::memory_order_relaxed);
            
            if (count > 0 && point.name) {
                active_points++;
                double avg_ns = point.get_avg_ns();
                uint64_t min_ns = point.get_min_ns();
                uint64_t max_ns = point.get_max_ns();
                double total_ms = (point.total_tsc.load(std::memory_order_relaxed) * 
                                   point.get_tsc_to_ns_scale()) / 1e6;
                
                std::cout << std::setw(30) << std::left << point.name
                          << std::setw(10) << std::right << count
                          << std::setw(12) << std::fixed << std::setprecision(0) << avg_ns
                          << std::setw(12) << min_ns
                          << std::setw(12) << max_ns
                          << std::setw(15) << std::setprecision(3) << total_ms
                          << "\n";
            }
        }
        
        if (active_points == 0) {
            std::cout << "No timing data recorded. Ensure ULTRA_PROFILE macros are being used.\n";
        }
        
        std::cout << std::string(91, '=') << "\n";
    }
    
private:
    alignas(64) std::array<UltraTimingPoint, MAX_TIMING_POINTS> timing_points_;
    std::atomic<size_t> next_point_id_{0};
    
    static size_t register_timing_point_impl(const char* name) {
        static std::atomic<size_t> counter{0};
        size_t id = counter.fetch_add(1, std::memory_order_relaxed);
        if (id < MAX_TIMING_POINTS) {
            auto& point = instance().timing_points_[id];
            point.name = name;
        }
        return id;
    }
};

// Ultra-fast RAII timer using direct TSC
class UltraTimer {
public:
    HFT_FORCE_INLINE explicit UltraTimer(size_t point_id) noexcept 
        : point_id_(point_id) {
        #if defined(__x86_64__) || defined(_M_X64)
            #ifdef __rdtscp
                unsigned aux;
                start_tsc_ = __rdtscp(&aux);
            #else
                start_tsc_ = __rdtsc();
            #endif
        #else
            start_tsc_ = 0; // fallback for non-x86
        #endif
    }
    
    HFT_FORCE_INLINE ~UltraTimer() noexcept {
        #if defined(__x86_64__) || defined(_M_X64)
            #ifdef __rdtscp
                unsigned aux;
                uint64_t end_tsc = __rdtscp(&aux);
            #else
                uint64_t end_tsc = __rdtsc();
            #endif
            
            if (HFT_LIKELY(end_tsc >= start_tsc_)) {
                UltraProfiler::record_timing(point_id_, end_tsc - start_tsc_);
            }
        #endif
    }
    
private:
    size_t point_id_;
    uint64_t start_tsc_;
};

} // namespace hft

// Macro to create static timing point IDs and ultra-fast timers
#define ULTRA_PROFILE_FUNCTION() \
    static const size_t __timing_point_id = hft::UltraProfiler::register_timing_point(__FUNCTION__); \
    hft::UltraTimer __ultra_timer(__timing_point_id)

#define ULTRA_PROFILE_SCOPE(name) \
    static const size_t __timing_point_id_##name = hft::UltraProfiler::register_timing_point(#name); \
    hft::UltraTimer __ultra_timer_##name(__timing_point_id_##name)

// For critical path timing with minimal overhead
#define ULTRA_PROFILE_CRITICAL(name) \
    static const size_t __critical_point_id = hft::UltraProfiler::register_timing_point("CRITICAL_" #name); \
    hft::UltraTimer __critical_timer(__critical_point_id)
