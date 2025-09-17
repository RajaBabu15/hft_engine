#include "hft/core/redis_client.hpp"
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#ifdef __APPLE__
#include <pthread.h>
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#endif
namespace hft {
namespace core {
RedisConnection::RedisConnection(const std::string& host, int port)
    : context_(nullptr), host_(host), port_(port), connected_(false), pipeline_mode_(false) {
}
RedisConnection::~RedisConnection() {
    disconnect();
}
bool RedisConnection::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_) {
        return true;
    }
    context_ = redisConnect(host_.c_str(), port_);
    if (context_ == nullptr || context_->err) {
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
        connected_ = false;
        return false;
    }
    connected_ = true;
    return true;
}
void RedisConnection::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
    connected_ = false;
}
redisReply* RedisConnection::command(const char* format, ...) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_connected()) {
        return nullptr;
    }
    va_list args;
    va_start(args, format);
    redisReply* reply = static_cast<redisReply*>(redisvCommand(context_, format, args));
    va_end(args);
    operations_count.fetch_add(1, std::memory_order_relaxed);
    return reply;
}
bool RedisConnection::set(const std::string& key, const std::string& value) {
    redisReply* reply = command("SET %s %s", key.c_str(), value.c_str());
    if (reply && reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK") {
        freeReplyObject(reply);
        return true;
    }
    if (reply) freeReplyObject(reply);
    return false;
}
std::string RedisConnection::get(const std::string& key) {
    redisReply* reply = command("GET %s", key.c_str());
    if (reply && reply->type == REDIS_REPLY_STRING) {
        std::string result(reply->str, reply->len);
        freeReplyObject(reply);
        return result;
    }
    if (reply) freeReplyObject(reply);
    return "";
}
bool RedisConnection::exists(const std::string& key) {
    redisReply* reply = command("EXISTS %s", key.c_str());
    bool result = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = reply->integer > 0;
    }
    if (reply) freeReplyObject(reply);
    return result;
}
bool RedisConnection::del(const std::string& key) {
    redisReply* reply = command("DEL %s", key.c_str());
    bool result = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = reply->integer > 0;
    }
    if (reply) freeReplyObject(reply);
    return result;
}
bool RedisConnection::hset(const std::string& key, const std::string& field, const std::string& value) {
    redisReply* reply = command("HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    bool result = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = true;
    }
    if (reply) freeReplyObject(reply);
    return result;
}
std::string RedisConnection::hget(const std::string& key, const std::string& field) {
    redisReply* reply = command("HGET %s %s", key.c_str(), field.c_str());
    if (reply && reply->type == REDIS_REPLY_STRING) {
        std::string result(reply->str, reply->len);
        freeReplyObject(reply);
        return result;
    }
    if (reply) freeReplyObject(reply);
    return "";
}
bool RedisConnection::hdel(const std::string& key, const std::string& field) {
    redisReply* reply = command("HDEL %s %s", key.c_str(), field.c_str());
    bool result = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = reply->integer > 0;
    }
    if (reply) freeReplyObject(reply);
    return result;
}
bool RedisConnection::lpush(const std::string& key, const std::string& value) {
    redisReply* reply = command("LPUSH %s %s", key.c_str(), value.c_str());
    bool result = false;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = reply->integer > 0;
    }
    if (reply) freeReplyObject(reply);
    return result;
}
std::string RedisConnection::rpop(const std::string& key) {
    redisReply* reply = command("RPOP %s", key.c_str());
    if (reply && reply->type == REDIS_REPLY_STRING) {
        std::string result(reply->str, reply->len);
        freeReplyObject(reply);
        return result;
    }
    if (reply) freeReplyObject(reply);
    return "";
}
int RedisConnection::llen(const std::string& key) {
    redisReply* reply = command("LLEN %s", key.c_str());
    int result = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = static_cast<int>(reply->integer);
    }
    if (reply) freeReplyObject(reply);
    return result;
}
long long RedisConnection::incr(const std::string& key) {
    redisReply* reply = command("INCR %s", key.c_str());
    long long result = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = reply->integer;
    }
    if (reply) freeReplyObject(reply);
    return result;
}
long long RedisConnection::incrby(const std::string& key, long long increment) {
    redisReply* reply = command("INCRBY %s %lld", key.c_str(), increment);
    long long result = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        result = reply->integer;
    }
    if (reply) freeReplyObject(reply);
    return result;
}
void RedisConnection::pipeline_start() {
    std::lock_guard<std::mutex> lock(mutex_);
    pipeline_mode_ = true;
    pipeline_replies_.clear();
}
void RedisConnection::pipeline_add(const char* format, ...) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_mode_ || !is_connected()) {
        return;
    }
    va_list args;
    va_start(args, format);
    redisvAppendCommand(context_, format, args);
    va_end(args);
    pipeline_operations_count.fetch_add(1, std::memory_order_relaxed);
}
std::vector<redisReply*> RedisConnection::pipeline_execute() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pipeline_mode_ || !is_connected()) {
        return {};
    }
    std::vector<redisReply*> replies;
    size_t expected_replies = pipeline_operations_count.load();
    for (size_t i = 0; i < expected_replies; ++i) {
        redisReply* reply = nullptr;
        if (redisGetReply(context_, reinterpret_cast<void**>(&reply)) == REDIS_OK) {
            replies.push_back(reply);
        }
    }
    pipeline_mode_ = false;
    pipeline_operations_count.store(0);
    return replies;
}
RedisConnectionPool::RedisConnectionPool(const std::string& host, int port, size_t pool_size)
    : host_(host), port_(port), pool_size_(pool_size) {
    initialize_pool();
}
RedisConnectionPool::~RedisConnectionPool() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    while (!available_connections_.empty()) {
        available_connections_.pop();
    }
}
void RedisConnectionPool::initialize_pool() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    for (size_t i = 0; i < pool_size_; ++i) {
        auto connection = std::make_unique<RedisConnection>(host_, port_);
        if (connection->connect()) {
            available_connections_.push(std::move(connection));
        }
    }
}
std::unique_ptr<RedisConnection> RedisConnectionPool::get_connection() {
    std::unique_lock<std::mutex> lock(pool_mutex_);
    pool_condition_.wait(lock, [this] { return !available_connections_.empty(); });
    auto connection = std::move(available_connections_.front());
    available_connections_.pop();
    active_connections_.fetch_add(1, std::memory_order_relaxed);
    return connection;
}
void RedisConnectionPool::return_connection(std::unique_ptr<RedisConnection> conn) {
    if (conn && conn->is_connected()) {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        available_connections_.push(std::move(conn));
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
        pool_condition_.notify_one();
    }
}
HighPerformanceRedisClient::HighPerformanceRedisClient(const std::string& host, int port, size_t pool_size) {
    connection_pool_ = std::make_unique<RedisConnectionPool>(host, port, pool_size);
    
    // Start async worker threads (without thread affinity for compatibility)
    size_t num_workers = std::min(4u, std::thread::hardware_concurrency());
    worker_threads_.reserve(num_workers);
    
    for (size_t i = 0; i < num_workers; ++i) {
        worker_threads_.emplace_back(&HighPerformanceRedisClient::async_worker, this);
    }
}
HighPerformanceRedisClient::~HighPerformanceRedisClient() {
    shutdown_async_workers();
}
std::string HighPerformanceRedisClient::make_order_key(uint64_t order_id) const {
    return "order:" + std::to_string(order_id);
}
std::string HighPerformanceRedisClient::make_market_data_key(const std::string& symbol) const {
    return "market:" + symbol;
}
std::string HighPerformanceRedisClient::make_metric_key(const std::string& metric_name) const {
    return "metric:" + metric_name;
}
template<typename Func>
auto HighPerformanceRedisClient::with_connection_timing(Func&& func) -> decltype(func(std::declval<RedisConnection&>())) {
    RedisTimer timer(total_redis_time_ns_);
    auto connection = connection_pool_->get_connection();
    auto result = func(*connection);
    connection_pool_->return_connection(std::move(connection));
    total_operations_.fetch_add(1, std::memory_order_relaxed);
    return result;
}
bool HighPerformanceRedisClient::cache_order_state(uint64_t order_id, const std::string& symbol, const std::string& state) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_order_key(order_id);
        bool success = conn.hset(key, "symbol", symbol) && conn.hset(key, "state", state);
        if (success) {
            cache_hits_.fetch_add(1, std::memory_order_relaxed);
        } else {
            cache_misses_.fetch_add(1, std::memory_order_relaxed);
        }
        return success;
    });
}
std::string HighPerformanceRedisClient::get_order_state(uint64_t order_id) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_order_key(order_id);
        std::string state = conn.hget(key, "state");
        if (!state.empty()) {
            cache_hits_.fetch_add(1, std::memory_order_relaxed);
        } else {
            cache_misses_.fetch_add(1, std::memory_order_relaxed);
        }
        return state;
    });
}
bool HighPerformanceRedisClient::delete_order_state(uint64_t order_id) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_order_key(order_id);
        return conn.del(key);
    });
}
bool HighPerformanceRedisClient::cache_market_data(const std::string& symbol, double bid, double ask, uint64_t timestamp) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_market_data_key(symbol);
        std::ostringstream bid_ss, ask_ss, ts_ss;
        bid_ss << std::fixed << std::setprecision(6) << bid;
        ask_ss << std::fixed << std::setprecision(6) << ask;
        ts_ss << timestamp;
        return conn.hset(key, "bid", bid_ss.str()) &&
               conn.hset(key, "ask", ask_ss.str()) &&
               conn.hset(key, "timestamp", ts_ss.str());
    });
}
bool HighPerformanceRedisClient::get_market_data(const std::string& symbol, double& bid, double& ask, uint64_t& timestamp) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_market_data_key(symbol);
        std::string bid_str = conn.hget(key, "bid");
        std::string ask_str = conn.hget(key, "ask");
        std::string ts_str = conn.hget(key, "timestamp");
        if (!bid_str.empty() && !ask_str.empty() && !ts_str.empty()) {
            try {
                bid = std::stod(bid_str);
                ask = std::stod(ask_str);
                timestamp = std::stoull(ts_str);
                cache_hits_.fetch_add(1, std::memory_order_relaxed);
                return true;
            } catch (const std::exception&) {
                cache_misses_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
        }
        cache_misses_.fetch_add(1, std::memory_order_relaxed);
        return false;
    });
}
bool HighPerformanceRedisClient::store_latency_metric(const std::string& metric_name, double value_us) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_metric_key(metric_name);
        std::ostringstream value_ss;
        value_ss << std::fixed << std::setprecision(6) << value_us;
        return conn.set(key, value_ss.str());
    });
}
bool HighPerformanceRedisClient::increment_counter(const std::string& counter_name, long long increment) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_metric_key(counter_name);
        conn.incrby(key, increment);
        return true;
    });
}
long long HighPerformanceRedisClient::get_counter(const std::string& counter_name) {
    return with_connection_timing([&](RedisConnection& conn) {
        std::string key = make_metric_key(counter_name);
        std::string value = conn.get(key);
        try {
            return value.empty() ? 0 : std::stoll(value);
        } catch (const std::exception&) {
            return 0LL;
        }
    });
}
bool HighPerformanceRedisClient::execute_batch(const std::vector<BatchOperation>& operations) {
    if (operations.empty()) return true;
    return with_connection_timing([&](RedisConnection& conn) {
        conn.pipeline_start();
        for (const auto& op : operations) {
            switch (op.type) {
                case BatchOperation::SET:
                    conn.pipeline_add("SET %s %s", op.key.c_str(), op.value.c_str());
                    break;
                case BatchOperation::HSET:
                    conn.pipeline_add("HSET %s %s %s", op.key.c_str(), op.field.c_str(), op.value.c_str());
                    break;
                case BatchOperation::INCR:
                    conn.pipeline_add("INCRBY %s %lld", op.key.c_str(), op.increment);
                    break;
                case BatchOperation::DEL:
                    conn.pipeline_add("DEL %s", op.key.c_str());
                    break;
            }
        }
        auto replies = conn.pipeline_execute();
        for (auto reply : replies) {
            if (reply) freeReplyObject(reply);
        }
        pipeline_batches_.fetch_add(1, std::memory_order_relaxed);
        return true;
    });
}
HighPerformanceRedisClient::PerformanceStats HighPerformanceRedisClient::get_performance_stats() const {
    PerformanceStats stats;
    stats.total_operations = total_operations_.load();
    stats.cache_hits = cache_hits_.load();
    stats.cache_misses = cache_misses_.load();
    stats.pipeline_batches = pipeline_batches_.load();
    uint64_t total_time_ns = total_redis_time_ns_.load();
    stats.avg_redis_latency_us = stats.total_operations > 0 ?
        (static_cast<double>(total_time_ns) / stats.total_operations) / 1000.0 : 0.0;
    uint64_t total_cache_ops = stats.cache_hits + stats.cache_misses;
    stats.cache_hit_rate = total_cache_ops > 0 ?
        static_cast<double>(stats.cache_hits) / total_cache_ops : 0.0;
    return stats;
}
void HighPerformanceRedisClient::reset_performance_stats() {
    total_operations_.store(0);
    cache_hits_.store(0);
    cache_misses_.store(0);
    pipeline_batches_.store(0);
    total_redis_time_ns_.store(0);
}

void HighPerformanceRedisClient::cache_order_state_async(uint64_t order_id, const std::string& symbol, const std::string& state) {
    AsyncRedisTask task{};
    task.type = AsyncRedisTask::CACHE_ORDER_STATE;
    task.order_id = order_id;
    task.symbol = symbol;
    task.state = state;
    async_queue_.push(task);  // Lock-free push
}

void HighPerformanceRedisClient::async_worker() {
    while (!shutdown_.load(std::memory_order_acquire)) {
        AsyncRedisTask task;
        if (async_queue_.pop(task)) {
            process_async_task(task);
        } else {
            std::this_thread::yield();  // Minimal CPU usage when idle
        }
    }
}

void HighPerformanceRedisClient::process_async_task(const AsyncRedisTask& task) {
    try {
        switch (task.type) {
            case AsyncRedisTask::CACHE_ORDER_STATE: {
                auto connection = connection_pool_->get_connection();
                std::string key = make_order_key(task.order_id);
                connection->hset(key, "symbol", task.symbol);
                connection->hset(key, "state", task.state);
                connection_pool_->return_connection(std::move(connection));
                cache_hits_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            case AsyncRedisTask::CACHE_MARKET_DATA: {
                auto connection = connection_pool_->get_connection();
                std::string key = make_market_data_key(task.symbol);
                connection->hset(key, "bid", std::to_string(task.bid));
                connection->hset(key, "ask", std::to_string(task.ask));
                connection->hset(key, "timestamp", std::to_string(task.timestamp));
                connection_pool_->return_connection(std::move(connection));
                break;
            }
            default:
                break;
        }
        total_operations_.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        cache_misses_.fetch_add(1, std::memory_order_relaxed);
    }
}

void HighPerformanceRedisClient::shutdown_async_workers() {
    shutdown_.store(true, std::memory_order_release);
    for (auto& worker : worker_threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    worker_threads_.clear();
}
}
}
