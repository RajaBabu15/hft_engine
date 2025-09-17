#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <queue>
#include <condition_variable>
#include <future>
#include <functional>
#include "lock_free_queue.hpp"
namespace hft {
namespace core {
class RedisConnection {
private:
    redisContext* context_;
    std::string host_;
    int port_;
    bool connected_;
    mutable std::mutex mutex_;
public:
    RedisConnection(const std::string& host = "127.0.0.1", int port = 6379);
    ~RedisConnection();
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_ && context_ && context_->err == 0; }
    redisReply* command(const char* format, ...);
    bool set(const std::string& key, const std::string& value);
    std::string get(const std::string& key);
    bool exists(const std::string& key);
    bool del(const std::string& key);
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    std::string hget(const std::string& key, const std::string& field);
    bool hdel(const std::string& key, const std::string& field);
    bool lpush(const std::string& key, const std::string& value);
    std::string rpop(const std::string& key);
    int llen(const std::string& key);
    long long incr(const std::string& key);
    long long incrby(const std::string& key, long long increment);
    void pipeline_start();
    void pipeline_add(const char* format, ...);
    std::vector<redisReply*> pipeline_execute();
    std::atomic<uint64_t> operations_count{0};
    std::atomic<uint64_t> pipeline_operations_count{0};
private:
    std::vector<redisReply*> pipeline_replies_;
    bool pipeline_mode_;
};
class RedisConnectionPool {
private:
    std::queue<std::unique_ptr<RedisConnection>> available_connections_;
    std::mutex pool_mutex_;
    std::condition_variable pool_condition_;
    std::string host_;
    int port_;
    size_t pool_size_;
    std::atomic<size_t> active_connections_{0};
public:
    RedisConnectionPool(const std::string& host = "127.0.0.1", int port = 6379, size_t pool_size = 10);
    ~RedisConnectionPool();
    std::unique_ptr<RedisConnection> get_connection();
    void return_connection(std::unique_ptr<RedisConnection> conn);
    size_t size() const { return pool_size_; }
    size_t active_connections() const { return active_connections_.load(); }
private:
    void initialize_pool();
};
struct AsyncRedisTask {
    enum Type { CACHE_ORDER_STATE, CACHE_MARKET_DATA, STORE_METRIC, INCREMENT_COUNTER };
    Type type;
    uint64_t order_id;
    std::string key;
    std::string field;
    std::string value;
    std::string symbol;
    std::string state;
    double bid, ask;
    uint64_t timestamp;
    long long increment;
};

class HighPerformanceRedisClient {
private:
    std::unique_ptr<RedisConnectionPool> connection_pool_;
    std::atomic<uint64_t> total_operations_{0};
    std::atomic<uint64_t> cache_hits_{0};
    std::atomic<uint64_t> cache_misses_{0};
    std::atomic<uint64_t> pipeline_batches_{0};
    mutable std::atomic<uint64_t> total_redis_time_ns_{0};
    
    // Async processing
    LockFreeQueue<AsyncRedisTask, 4096> async_queue_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> shutdown_{false};
public:
    HighPerformanceRedisClient(const std::string& host = "127.0.0.1", int port = 6379, size_t pool_size = 20);
    ~HighPerformanceRedisClient();
    bool cache_order_state(uint64_t order_id, const std::string& symbol, const std::string& state);
    void cache_order_state_async(uint64_t order_id, const std::string& symbol, const std::string& state);
    std::string get_order_state(uint64_t order_id);
    bool delete_order_state(uint64_t order_id);
    bool cache_market_data(const std::string& symbol, double bid, double ask, uint64_t timestamp);
    bool get_market_data(const std::string& symbol, double& bid, double& ask, uint64_t& timestamp);
    bool store_latency_metric(const std::string& metric_name, double value_us);
    bool increment_counter(const std::string& counter_name, long long increment = 1);
    long long get_counter(const std::string& counter_name);
    struct BatchOperation {
        enum Type { SET, HSET, INCR, DEL };
        Type type;
        std::string key;
        std::string field;
        std::string value;
        long long increment;
    };
    bool execute_batch(const std::vector<BatchOperation>& operations);
    struct PerformanceStats {
        uint64_t total_operations;
        uint64_t cache_hits;
        uint64_t cache_misses;
        uint64_t pipeline_batches;
        double avg_redis_latency_us;
        double cache_hit_rate;
    };
    PerformanceStats get_performance_stats() const;
    void reset_performance_stats();
    size_t pool_size() const { return connection_pool_->size(); }
    size_t active_connections() const { return connection_pool_->active_connections(); }
    void shutdown_async_workers();
private:
    void async_worker();
    void process_async_task(const AsyncRedisTask& task);
    std::string make_order_key(uint64_t order_id) const;
    std::string make_market_data_key(const std::string& symbol) const;
    std::string make_metric_key(const std::string& metric_name) const;
    template<typename Func>
    auto with_connection_timing(Func&& func) -> decltype(func(std::declval<RedisConnection&>()));
};
class RedisTimer {
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    std::atomic<uint64_t>& total_time_ref_;
public:
    RedisTimer(std::atomic<uint64_t>& total_time_ref)
        : start_time_(std::chrono::high_resolution_clock::now()), total_time_ref_(total_time_ref) {}
    ~RedisTimer() {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
        total_time_ref_.fetch_add(duration.count(), std::memory_order_relaxed);
    }
};
}
}
