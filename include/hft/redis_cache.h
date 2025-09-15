#pragma once

#include "hft/types.h"
#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>

namespace hft {

struct MarketDataCache {
    Price bid;
    Price ask;
    Quantity bid_size;
    Quantity ask_size;
    uint64_t timestamp_ns;
    uint32_t update_count;
};

struct PositionCache {
    int64_t position;
    double realized_pnl;
    double unrealized_pnl;
    uint64_t last_update_ns;
};

class RedisCache {
private:
    redisContext* context_;
    std::string host_;
    int port_;
    std::mutex connection_mutex_;
    std::atomic<uint64_t> hits_{0};
    std::atomic<uint64_t> misses_{0};
    std::atomic<uint64_t> operations_{0};
    std::atomic<bool> connected_{false};

public:
    explicit RedisCache(const std::string& host = "localhost", int port = 6379) 
        : host_(host), port_(port), context_(nullptr) {
        connect();
    }

    ~RedisCache() {
        disconnect();
    }

    bool connect() {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        
        if (context_) {
            redisFree(context_);
        }
        
        context_ = redisConnect(host_.c_str(), port_);
        if (context_ == nullptr || context_->err) {
            if (context_) {
                std::cerr << "Redis connection error: " << context_->errstr << std::endl;
                redisFree(context_);
                context_ = nullptr;
            } else {
                std::cerr << "Redis connection error: Can't allocate redis context" << std::endl;
            }
            connected_.store(false, std::memory_order_release);
            return false;
        }
        
        connected_.store(true, std::memory_order_release);
        return true;
    }

    void disconnect() {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
        connected_.store(false, std::memory_order_release);
    }

    bool is_connected() const {
        return connected_.load(std::memory_order_acquire);
    }

    // High-performance market data caching
    bool cache_market_data(Symbol symbol, const MarketDataCache& data) {
        if (!is_connected()) return false;
        
        std::lock_guard<std::mutex> lock(connection_mutex_);
        operations_.fetch_add(1, std::memory_order_relaxed);
        
        std::string key = "md:" + std::to_string(symbol);
        std::string value = std::to_string(data.bid) + ":" + 
                           std::to_string(data.ask) + ":" +
                           std::to_string(data.bid_size) + ":" +
                           std::to_string(data.ask_size) + ":" +
                           std::to_string(data.timestamp_ns) + ":" +
                           std::to_string(data.update_count);
        
        redisReply* reply = (redisReply*)redisCommand(context_, "SETEX %s 60 %s", 
                                                      key.c_str(), value.c_str());
        if (reply) {
            bool success = reply->type == REDIS_REPLY_STATUS && 
                          std::string(reply->str) == "OK";
            freeReplyObject(reply);
            return success;
        }
        return false;
    }

    bool get_market_data(Symbol symbol, MarketDataCache& data) {
        if (!is_connected()) return false;
        
        std::lock_guard<std::mutex> lock(connection_mutex_);
        operations_.fetch_add(1, std::memory_order_relaxed);
        
        std::string key = "md:" + std::to_string(symbol);
        redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
        
        if (reply && reply->type == REDIS_REPLY_STRING) {
            std::string value(reply->str);
            freeReplyObject(reply);
            
            // Parse the cached data
            std::vector<std::string> parts;
            std::string delimiter = ":";
            size_t pos = 0;
            std::string token;
            while ((pos = value.find(delimiter)) != std::string::npos) {
                token = value.substr(0, pos);
                parts.push_back(token);
                value.erase(0, pos + delimiter.length());
            }
            parts.push_back(value);
            
            if (parts.size() == 6) {
                data.bid = std::stoull(parts[0]);
                data.ask = std::stoull(parts[1]);
                data.bid_size = std::stoull(parts[2]);
                data.ask_size = std::stoull(parts[3]);
                data.timestamp_ns = std::stoull(parts[4]);
                data.update_count = std::stoul(parts[5]);
                
                hits_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        
        if (reply) freeReplyObject(reply);
        misses_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Position caching for rapid P&L updates
    bool cache_position(Symbol symbol, uint32_t user_id, const PositionCache& pos) {
        if (!is_connected()) return false;
        
        std::lock_guard<std::mutex> lock(connection_mutex_);
        operations_.fetch_add(1, std::memory_order_relaxed);
        
        std::string key = "pos:" + std::to_string(user_id) + ":" + std::to_string(symbol);
        std::string value = std::to_string(pos.position) + ":" + 
                           std::to_string(pos.realized_pnl) + ":" +
                           std::to_string(pos.unrealized_pnl) + ":" +
                           std::to_string(pos.last_update_ns);
        
        redisReply* reply = (redisReply*)redisCommand(context_, "SETEX %s 3600 %s", 
                                                      key.c_str(), value.c_str());
        if (reply) {
            bool success = reply->type == REDIS_REPLY_STATUS && 
                          std::string(reply->str) == "OK";
            freeReplyObject(reply);
            return success;
        }
        return false;
    }

    bool get_position(Symbol symbol, uint32_t user_id, PositionCache& pos) {
        if (!is_connected()) return false;
        
        std::lock_guard<std::mutex> lock(connection_mutex_);
        operations_.fetch_add(1, std::memory_order_relaxed);
        
        std::string key = "pos:" + std::to_string(user_id) + ":" + std::to_string(symbol);
        redisReply* reply = (redisReply*)redisCommand(context_, "GET %s", key.c_str());
        
        if (reply && reply->type == REDIS_REPLY_STRING) {
            std::string value(reply->str);
            freeReplyObject(reply);
            
            std::vector<std::string> parts;
            std::string delimiter = ":";
            size_t pos_str = 0;
            std::string token;
            while ((pos_str = value.find(delimiter)) != std::string::npos) {
                token = value.substr(0, pos_str);
                parts.push_back(token);
                value.erase(0, pos_str + delimiter.length());
            }
            parts.push_back(value);
            
            if (parts.size() == 4) {
                pos.position = std::stoll(parts[0]);
                pos.realized_pnl = std::stod(parts[1]);
                pos.unrealized_pnl = std::stod(parts[2]);
                pos.last_update_ns = std::stoull(parts[3]);
                
                hits_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        
        if (reply) freeReplyObject(reply);
        misses_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Batch operations for high throughput
    bool pipeline_market_data_updates(const std::vector<std::pair<Symbol, MarketDataCache>>& updates) {
        if (!is_connected() || updates.empty()) return false;
        
        std::lock_guard<std::mutex> lock(connection_mutex_);
        operations_.fetch_add(updates.size(), std::memory_order_relaxed);
        
        // Start pipeline
        for (const auto& update : updates) {
            std::string key = "md:" + std::to_string(update.first);
            std::string value = std::to_string(update.second.bid) + ":" + 
                               std::to_string(update.second.ask) + ":" +
                               std::to_string(update.second.bid_size) + ":" +
                               std::to_string(update.second.ask_size) + ":" +
                               std::to_string(update.second.timestamp_ns) + ":" +
                               std::to_string(update.second.update_count);
            
            redisAppendCommand(context_, "SETEX %s 60 %s", key.c_str(), value.c_str());
        }
        
        // Get all replies
        bool all_success = true;
        for (size_t i = 0; i < updates.size(); ++i) {
            redisReply* reply;
            if (redisGetReply(context_, (void**)&reply) == REDIS_OK) {
                if (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK") {
                    // Success
                } else {
                    all_success = false;
                }
                freeReplyObject(reply);
            } else {
                all_success = false;
            }
        }
        
        return all_success;
    }

    // Performance metrics
    struct CacheStats {
        uint64_t hits;
        uint64_t misses;
        uint64_t operations;
        double hit_rate;
    };

    CacheStats get_stats() const {
        uint64_t h = hits_.load(std::memory_order_relaxed);
        uint64_t m = misses_.load(std::memory_order_relaxed);
        uint64_t ops = operations_.load(std::memory_order_relaxed);
        
        return {
            h, m, ops,
            (h + m > 0) ? (static_cast<double>(h) / (h + m) * 100.0) : 0.0
        };
    }

    void reset_stats() {
        hits_.store(0, std::memory_order_relaxed);
        misses_.store(0, std::memory_order_relaxed);
        operations_.store(0, std::memory_order_relaxed);
    }
};

} // namespace hft