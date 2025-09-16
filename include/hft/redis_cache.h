#pragma once

#include "hft/types.h"
#include "hft/order.h"
#include "hft/deep_profiler.h"
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>
#include <thread>

namespace hft {

// High-performance Redis client for HFT applications
class RedisCache {
public:
    struct Config {
        std::string host = "localhost";
        int port = 6379;
        int timeout_ms = 100;  // Ultra-low timeout for HFT
        int pool_size = 8;     // Connection pool size
        bool enable_pipeline = true;
        bool enable_compression = false;  // Disabled for latency
    };
    
    struct CacheStats {
        std::atomic<uint64_t> cache_hits{0};
        std::atomic<uint64_t> cache_misses{0};
        std::atomic<uint64_t> cache_sets{0};
        std::atomic<uint64_t> cache_errors{0};
        std::atomic<uint64_t> total_latency_ns{0};
        std::atomic<uint64_t> operation_count{0};
        
        double get_hit_ratio() const {
            uint64_t hits = cache_hits.load(std::memory_order_relaxed);
            uint64_t misses = cache_misses.load(std::memory_order_relaxed);
            return hits + misses > 0 ? static_cast<double>(hits) / (hits + misses) : 0.0;
        }
        
        double get_average_latency_us() const {
            uint64_t count = operation_count.load(std::memory_order_relaxed);
            uint64_t latency = total_latency_ns.load(std::memory_order_relaxed);
            return count > 0 ? static_cast<double>(latency) / (count * 1000.0) : 0.0;
        }
    };

private:
    Config config_;
    mutable CacheStats stats_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> enabled_{true};  // Can disable Redis for A/B testing
    
    // Simulated Redis connection pool (in production, use actual Redis client)
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, std::string> local_cache_;  // Simulated Redis store
    
    // Performance optimization: pre-allocated string buffers
    thread_local static std::string key_buffer_;
    thread_local static std::string value_buffer_;

public:
    RedisCache() : config_({}), connected_(false), enabled_(true) {
        connect();
    }
    
    explicit RedisCache(const Config& config) 
        : config_(config), connected_(false), enabled_(true) {
        connect();
    }
    
    ~RedisCache() {
        disconnect();
    }
    
    bool connect() {
        // Simulated Redis connection for demo
        // In production: use hiredis or redis-plus-plus
        std::cout << "📊 Connecting to Redis at " << config_.host 
                  << ":" << config_.port << std::endl;
        
        // Simulate connection delay
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        connected_.store(true, std::memory_order_release);
        std::cout << "✅ Redis connection established (simulated)" << std::endl;
        return true;
    }
    
    void disconnect() {
        if (connected_.load(std::memory_order_acquire)) {
            connected_.store(false, std::memory_order_release);
            std::cout << "📊 Redis disconnected" << std::endl;
        }
    }
    
    // High-performance cache operations with sub-microsecond latency
    bool get(const std::string& key, std::string& value) const {
        if (!enabled_.load(std::memory_order_relaxed) || 
            !connected_.load(std::memory_order_relaxed)) {
            return false;
        }
        
        DEEP_PROFILE_FUNCTION();
        auto start = std::chrono::high_resolution_clock::now();
        
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto it = local_cache_.find(key);
            if (it != local_cache_.end()) {
                value = it->second;
                found = true;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        
        // Update statistics
        if (found) {
            stats_.cache_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            stats_.cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
        stats_.total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
        stats_.operation_count.fetch_add(1, std::memory_order_relaxed);
        
        return found;
    }
    
    bool set(const std::string& key, const std::string& value, int /* ttl_seconds */ = 0) {
        if (!enabled_.load(std::memory_order_relaxed) || 
            !connected_.load(std::memory_order_relaxed)) {
            return false;
        }
        
        DEEP_PROFILE_FUNCTION();
        auto start = std::chrono::high_resolution_clock::now();
        
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            local_cache_[key] = value;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        
        stats_.cache_sets.fetch_add(1, std::memory_order_relaxed);
        stats_.total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
        stats_.operation_count.fetch_add(1, std::memory_order_relaxed);
        
        return true;
    }
    
    // HFT-specific caching methods
    bool cache_order_book_level(Symbol symbol, Side side, int level, 
                               Price price, Quantity qty) {
        key_buffer_ = "ob:" + std::to_string(symbol) + ":" + 
                     (side == Side::BUY ? "B" : "A") + ":" + std::to_string(level);
        value_buffer_ = std::to_string(price) + ":" + std::to_string(qty);
        return set(key_buffer_, value_buffer_, 1);  // 1 second TTL
    }
    
    bool get_cached_order_book_level(Symbol symbol, Side side, int level,
                                    Price& price, Quantity& qty) const {
        key_buffer_ = "ob:" + std::to_string(symbol) + ":" + 
                     (side == Side::BUY ? "B" : "A") + ":" + std::to_string(level);
        
        std::string cached_value;
        if (get(key_buffer_, cached_value)) {
            size_t colon_pos = cached_value.find(':');
            if (colon_pos != std::string::npos) {
                price = std::stoull(cached_value.substr(0, colon_pos));
                qty = std::stoull(cached_value.substr(colon_pos + 1));
                return true;
            }
        }
        return false;
    }
    
    bool cache_market_data(Symbol symbol, Price best_bid, Price best_ask,
                          Quantity bid_qty, Quantity ask_qty) {
        key_buffer_ = "md:" + std::to_string(symbol);
        value_buffer_ = std::to_string(best_bid) + ":" + std::to_string(best_ask) + 
                       ":" + std::to_string(bid_qty) + ":" + std::to_string(ask_qty);
        return set(key_buffer_, value_buffer_, 1);
    }
    
    bool get_cached_market_data(Symbol symbol, Price& best_bid, Price& best_ask,
                               Quantity& bid_qty, Quantity& ask_qty) const {
        key_buffer_ = "md:" + std::to_string(symbol);
        std::string cached_value;
        if (get(key_buffer_, cached_value)) {
            std::istringstream iss(cached_value);
            std::string token;
            if (std::getline(iss, token, ':')) best_bid = std::stoull(token);
            if (std::getline(iss, token, ':')) best_ask = std::stoull(token);
            if (std::getline(iss, token, ':')) bid_qty = std::stoull(token);
            if (std::getline(iss, token, ':')) ask_qty = std::stoull(token);
            return true;
        }
        return false;
    }
    
    // Performance measurement and A/B testing
    void enable_caching(bool enabled) {
        enabled_.store(enabled, std::memory_order_release);
        std::cout << "📊 Redis caching " << (enabled ? "ENABLED" : "DISABLED") 
                  << " for performance comparison" << std::endl;
    }
    
    bool is_enabled() const {
        return enabled_.load(std::memory_order_relaxed);
    }
    
    void clear_stats() {
        stats_.cache_hits.store(0, std::memory_order_relaxed);
        stats_.cache_misses.store(0, std::memory_order_relaxed);
        stats_.cache_sets.store(0, std::memory_order_relaxed);
        stats_.cache_errors.store(0, std::memory_order_relaxed);
        stats_.total_latency_ns.store(0, std::memory_order_relaxed);
        stats_.operation_count.store(0, std::memory_order_relaxed);
    }
    
    const CacheStats& get_stats() const {
        return stats_;
    }
    
    void print_performance_report() const {
        std::cout << "\n📊 REDIS PERFORMANCE REPORT" << std::endl;
        std::cout << "============================" << std::endl;
        std::cout << "Cache Operations: " << stats_.operation_count.load(std::memory_order_relaxed) << std::endl;
        std::cout << "Cache Hits: " << stats_.cache_hits.load(std::memory_order_relaxed) << std::endl;
        std::cout << "Cache Misses: " << stats_.cache_misses.load(std::memory_order_relaxed) << std::endl;
        std::cout << "Cache Sets: " << stats_.cache_sets.load(std::memory_order_relaxed) << std::endl;
        std::cout << "Hit Ratio: " << std::fixed << std::setprecision(2) 
                  << stats_.get_hit_ratio() * 100 << "%" << std::endl;
        std::cout << "Average Latency: " << std::fixed << std::setprecision(3) 
                  << stats_.get_average_latency_us() << " μs" << std::endl;
        std::cout << "Status: " << (enabled_.load(std::memory_order_relaxed) ? "ENABLED" : "DISABLED") << std::endl;
    }
};

// Thread-local storage for performance optimization
thread_local std::string RedisCache::key_buffer_;
thread_local std::string RedisCache::value_buffer_;

} // namespace hft