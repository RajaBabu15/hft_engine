#pragma once

#include <chrono>
#include <unordered_map>
#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <sstream>
#include <mutex>
#include <cmath>

namespace hft {
class DeepProfiler {
public:
    struct ProfilePoint {
        uint64_t total_ns = 0;
        uint64_t call_count = 0;
        uint64_t min_ns = UINT64_MAX;
        uint64_t max_ns = 0;
        uint64_t sum_sq_ns = 0;         // for stdev
        uint64_t last_call_ns = 0;      // last observed duration
        uint64_t first_ts_ns = 0;       // first timestamp seen (steady_clock)
        uint64_t last_ts_ns = 0;        // last timestamp seen
        std::string function_name;
        std::string file_line;
            std::string last_context;       // last captured context string
        
        // bounded sample buffer for percentile estimation
        static constexpr size_t SAMPLE_CAP = 8192;
        std::vector<uint64_t> samples;
        
        ProfilePoint() { samples.reserve(SAMPLE_CAP); }
        ProfilePoint(const std::string& func, const std::string& location) 
            : function_name(func), file_line(location) { samples.reserve(SAMPLE_CAP); }

            // Track slowest N samples with their timestamps and thread ids
            struct SlowSample { uint64_t ns; uint64_t ts_ns; std::thread::id tid; std::string context; };
            static constexpr size_t TOP_SLOW_CAP = 32;
            std::vector<SlowSample> top_slowest; // kept sorted descending by ns
    };
    
    static DeepProfiler& instance() {
        static DeepProfiler profiler;
        return profiler;
    }
    
        void add_timing(const std::string& key, uint64_t ns, const std::string& func, const std::string& location) {
            auto& point = profile_points_[key];
        if (point.function_name.empty()) {
            point.function_name = func;
            point.file_line = location;
        }
        
        point.total_ns += ns;
        point.call_count += 1;
        point.last_call_ns = ns;
        point.sum_sq_ns += ns * ns;
        
        if (ns < point.min_ns) point.min_ns = ns;
        if (ns > point.max_ns) point.max_ns = ns;
        
            // timestamps (steady_clock since epoch, best effort)
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const uint64_t now_ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        if (point.first_ts_ns == 0) point.first_ts_ns = now_ns;
        point.last_ts_ns = now_ns;
            point.last_context = current_context_string();
        
        if (point.samples.size() < ProfilePoint::SAMPLE_CAP) {
            point.samples.push_back(ns);
        }

            // Slow-path sampling
            if (ns >= slow_threshold_ns_) {
                ProfilePoint::SlowSample s{ns, now_ns, std::this_thread::get_id(), point.last_context};
                auto& v = point.top_slowest;
                if (v.size() < ProfilePoint::TOP_SLOW_CAP) {
                    v.push_back(std::move(s));
                    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b){ return a.ns > b.ns; });
                } else if (!v.empty() && ns > v.back().ns) {
                    v.back() = std::move(s);
                    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b){ return a.ns > b.ns; });
                }
            }
    }

        // Lightweight counters (for queue depth, book sizes, etc.)
        void increment_counter(const std::string& name, int64_t delta = 1) {
            counters_[name].fetch_add(delta, std::memory_order_relaxed);
        }
        void set_gauge(const std::string& name, int64_t value) {
            gauges_[name].store(value, std::memory_order_relaxed);
        }

        // Context API: push/pop key=value pairs to be captured with timings
        void push_context(const std::string& k, const std::string& v) {
            thread_local_context().emplace_back(k, v);
        }
        void pop_context() {
            auto& ctx = thread_local_context();
            if (!ctx.empty()) ctx.pop_back();
        }
        struct ContextScope {
            DeepProfiler& p; ContextScope(DeepProfiler& p, const std::string& k, const std::string& v): p(p){ p.push_context(k,v);} ~ContextScope(){ p.pop_context(); }
        };
    
    struct ProfileData {
        uint64_t total_time_ns;
        uint64_t call_count;
        uint64_t min_time_ns;
        uint64_t max_time_ns;
        double avg_time_ns;
        double stdev_ns;
        uint64_t median_ns;
        uint64_t p90_ns;
        uint64_t p99_ns;
        uint64_t last_call_ns;
        uint64_t first_ts_ns;
        uint64_t last_ts_ns;
        std::string function_name;
        std::string location;
    };
    
    void reset() {
        for (auto& [key, point] : profile_points_) {
            point.total_ns = 0;
            point.call_count = 0;
            point.min_ns = UINT64_MAX;
            point.max_ns = 0;
            point.sum_sq_ns = 0;
            point.last_call_ns = 0;
            point.first_ts_ns = 0;
            point.last_ts_ns = 0;
            point.samples.clear();
            point.top_slowest.clear();
        }
    }
    
    void clear() { 
        profile_points_.clear(); 
    }
    
    std::unordered_map<std::string, ProfileData> get_results() const {
        std::unordered_map<std::string, ProfileData> results;
        
        for (const auto& [key, point] : profile_points_) {
            if (point.call_count > 0) {
                ProfileData data{};
                data.total_time_ns = point.total_ns;
                data.call_count = point.call_count;
                data.min_time_ns = (point.min_ns == UINT64_MAX) ? 0 : point.min_ns;
                data.max_time_ns = point.max_ns;
                data.avg_time_ns = static_cast<double>(data.total_time_ns) / data.call_count;
                // stdev = sqrt(E[x^2] - (E[x])^2)
                double ex2 = static_cast<double>(point.sum_sq_ns) / point.call_count;
                double ex = data.avg_time_ns;
                data.stdev_ns = ex2 > ex * ex ? std::sqrt(ex2 - ex * ex) : 0.0;
                
                // percentiles from samples
                if (!point.samples.empty()) {
                    std::vector<uint64_t> s = point.samples;
                    std::sort(s.begin(), s.end());
                    auto idx = [&](double q){ size_t i = static_cast<size_t>(q * (s.size() - 1)); return s[i]; };
                    data.median_ns = idx(0.5);
                    data.p90_ns = idx(0.90);
                    data.p99_ns = idx(0.99);
                } else {
                    data.median_ns = data.p90_ns = data.p99_ns = 0;
                }
                data.last_call_ns = point.last_call_ns;
                data.first_ts_ns = point.first_ts_ns;
                data.last_ts_ns = point.last_ts_ns;
                data.function_name = point.function_name;
                data.location = point.file_line;
                
                results[key] = data;
            }
        }
        
        return results;
    }
    
    std::string generate_report() const {
        std::vector<std::pair<std::string, ProfilePoint>> sorted_points;
        
        for (const auto& [key, point] : profile_points_) {
            if (point.call_count > 0) sorted_points.emplace_back(key, point);
        }
        
        std::sort(sorted_points.begin(), sorted_points.end(), 
                 [](const auto& a, const auto& b) { return a.second.total_ns > b.second.total_ns; });
        
        std::ostringstream oss;
        oss << "\n=== DEEP LINE-LEVEL PROFILING REPORT ===\n";
        oss << std::setw(50) << std::left << "Location"
            << std::setw(15) << std::right << "Total(ms)"
            << std::setw(10) << "Calls" 
            << std::setw(12) << "Avg(ns)"
            << std::setw(12) << "Min(ns)"
            << std::setw(12) << "Max(ns)" << "\n";
        oss << std::string(111, '-') << "\n";
        
        uint64_t total_time = 0;
        for (const auto& [key, point] : sorted_points) total_time += point.total_ns;
        
        for (const auto& [key, point] : sorted_points) {
            uint64_t total_ns = point.total_ns;
            uint64_t calls = point.call_count;
            uint64_t min_ns = (point.min_ns == UINT64_MAX) ? 0 : point.min_ns;
            uint64_t max_ns = point.max_ns;
            double total_ms = total_ns / 1e6;
            double avg_ns = calls ? static_cast<double>(total_ns) / calls : 0;
            double percentage = total_time ? (total_ns * 100.0) / total_time : 0;
            std::string location = point.function_name + " (" + point.file_line + ")";
            if (location.length() > 49) location = location.substr(0, 46) + "...";
            
            oss << std::setw(50) << std::left << location
                << std::setw(12) << std::right << std::fixed << std::setprecision(3) << total_ms
                << " (" << std::setw(4) << std::setprecision(1) << percentage << "%)"
                << std::setw(8) << calls
                << std::setw(12) << std::setprecision(0) << avg_ns
                << std::setw(12) << min_ns
                << std::setw(12) << max_ns << "\n";
        }
        
        oss << std::string(111, '-') << "\n";
        // Counters summary
        if (!counters_.empty() || !gauges_.empty()) {
            oss << "\n=== COUNTERS / GAUGES ===\n";
            for (const auto& [k,v] : counters_) oss << std::setw(40) << std::left << k << std::setw(16) << std::right << v.load(std::memory_order_relaxed) << "\n";
            for (const auto& [k,v] : gauges_) oss << std::setw(40) << std::left << k << std::setw(16) << std::right << v.load(std::memory_order_relaxed) << "\n";
        }
        return oss.str();
    }

    std::string generate_detailed_report() const {
        auto results = get_results();
        // sort by total time
        std::vector<std::pair<std::string, ProfileData>> sorted;
        sorted.reserve(results.size());
        for (auto& kv : results) sorted.emplace_back(kv.first, kv.second);
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b){
            return a.second.total_time_ns > b.second.total_time_ns;
        });
        
        std::ostringstream oss;
        oss << "\n=== DEEP PROFILER (DETAILED) ===\n";
        oss << std::setw(52) << std::left << "Location"
            << std::setw(10) << std::right << "Calls"
            << std::setw(14) << "Total(ms)"
            << std::setw(12) << "Avg(ns)"
            << std::setw(12) << "Stdev(ns)"
            << std::setw(12) << "P50(ns)"
            << std::setw(12) << "P90(ns)"
            << std::setw(12) << "P99(ns)"
            << std::setw(12) << "Min(ns)"
            << std::setw(12) << "Max(ns)"
            << std::setw(12) << "Last(ns)" << "\n";
        oss << std::string(160, '-') << "\n";
        for (const auto& [key, d] : sorted) {
            std::string location = d.function_name + " (" + d.location + ")";
            if (location.length() > 51) location = location.substr(0, 48) + "...";
            oss << std::setw(52) << std::left << location
                << std::setw(10) << std::right << d.call_count
                << std::setw(14) << std::fixed << std::setprecision(3) << (d.total_time_ns / 1e6)
                << std::setw(12) << std::setprecision(0) << d.avg_time_ns
                << std::setw(12) << std::setprecision(0) << d.stdev_ns
                << std::setw(12) << d.median_ns
                << std::setw(12) << d.p90_ns
                << std::setw(12) << d.p99_ns
                << std::setw(12) << d.min_time_ns
                << std::setw(12) << d.max_time_ns
                << std::setw(12) << d.last_call_ns
                << "\n";

            // Slowest samples (top N) for this key
            const auto it = profile_points_.find(key);
            if (it != profile_points_.end() && !it->second.top_slowest.empty()) {
                oss << "      Slowest samples (ns, ts, tid, ctx):\n";
                size_t shown = 0;
                for (const auto& s : it->second.top_slowest) {
                    if (shown++ >= 8) break; // limit per key
                    oss << "        " << std::setw(10) << s.ns << "  ts=" << s.ts_ns << "  tid=" << s.tid << "  ctx=" << s.context << "\n";
                }
            }
        }
        oss << std::string(160, '-') << "\n";
        return oss.str();
    }
    
    private:
        std::unordered_map<std::string, ProfilePoint> profile_points_;
        std::unordered_map<std::string, std::atomic<int64_t>> counters_;
        std::unordered_map<std::string, std::atomic<int64_t>> gauges_;
        uint64_t slow_threshold_ns_ = 50000; // 50 us default

        // Per-thread context stack
        static std::vector<std::pair<std::string,std::string>>& thread_local_context() {
            thread_local std::vector<std::pair<std::string,std::string>> ctx; return ctx;
        }
        static std::string current_context_string() {
            const auto& ctx = thread_local_context();
            if (ctx.empty()) return {};
            std::ostringstream oss; bool first=true;
            for (const auto& kv : ctx) { if (!first) oss << ","; first=false; oss << kv.first << "=" << kv.second; }
            return oss.str();
        }
};

// RAII timer for automatic profiling
class DeepTimer {
public:
    DeepTimer(const std::string& key, const std::string& func, const std::string& location) 
        : key_(key), func_(func), location_(location), start_(std::chrono::high_resolution_clock::now()) {}
    
    ~DeepTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        DeepProfiler::instance().add_timing(key_, ns, func_, location_);
    }
    
private:
    std::string key_;
    std::string func_; 
    std::string location_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace hft

// Helper macro for stringizing line numbers
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Convenience macros for line-level profiling
#define DEEP_PROFILE_LINE(name) \
    hft::DeepTimer _deep_timer_##__LINE__(name, __FUNCTION__, __FILE__ ":" TOSTRING(__LINE__))

#ifdef ENABLE_DEEP_PROFILE
#define DEEP_PROFILE_FUNCTION() \
    hft::DeepTimer _deep_timer_func(__FUNCTION__, __FUNCTION__, __FILE__ ":" TOSTRING(__LINE__))
#else
#define DEEP_PROFILE_FUNCTION() (void)0
#endif

#define DEEP_PROFILE_SCOPE(name) \
    hft::DeepTimer _deep_timer_scope(name, __FUNCTION__, __FILE__ ":" TOSTRING(__LINE__))

// Convenience macros for counters and context
#define DEEP_COUNTER_INC(name, delta) \
    hft::DeepProfiler::instance().increment_counter(name, delta)

#define DEEP_GAUGE_SET(name, value) \
    hft::DeepProfiler::instance().set_gauge(name, value)

#define DEEP_CTX_PUSH(key, value) \
    hft::DeepProfiler::instance().push_context(key, value)

#define DEEP_CTX_POP() \
    hft::DeepProfiler::instance().pop_context()

#define DEEP_CTX_SCOPE(key, value) \
    hft::DeepProfiler::ContextScope _deep_ctx_scope(hft::DeepProfiler::instance(), key, value)
