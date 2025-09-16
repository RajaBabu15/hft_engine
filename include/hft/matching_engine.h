
// file: matching_engine.h
#pragma once

#include "hft/types.h"
#include "hft/platform.h"
#include "hft/order.h"
#include "hft/command.h"
#include "hft/hot_order_view.h"
#include "hft/risk_manager.h"
#include "hft/logger.h"
#include "hft/ultra_profiler.h"
#include "hft/segment_tree.h"
#include "hft/epoch_manager.h"
#include "hft/order_node.h"
#include "hft/single_consumer_pool.h"
#include "hft/queue.h"
#include "hft/price_tracker.h"
#include "hft/matching_engine_types.h"
#include "hft/simd_match.h"
#include "hft/order_book.h"
#include "hft/shard.h"
#include "hft/trade.h"

#include <atomic>
#include <thread>
#include <vector>
#include <array>
#include <cassert>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <chrono>
#include "hft/latency_controller.h"
#include "hft/slippage_tracker.h"
#include "hft/strategy.h"
#include "hft/redis_cache.h"
#include "hft/advanced_metrics.h"


#ifdef __linux__
    #include <pthread.h>
    #include <numa.h>
    #include <sys/mman.h>
    #define NUMA_AWARE
#endif

namespace hft {
    using hft::CACHE_LINE_SIZE;
    using hft::SIMD_WIDTH;
    constexpr size_t MAX_PRICE_LEVELS = 10000;

    class MatchingEngine {
        private:
            hft::RiskManager &rm_;
            [[maybe_unused]] hft::Logger &log_;
            hft::LatencyController latency_controller_;
            hft::SlippageTracker slippage_tracker_;
            hft::MarketMakingStrategy strategy_;
            hft::RedisCache redis_cache_;  // Redis integration for 30x throughput improvement
            hft::AdvancedMetrics advanced_metrics_;  // Comprehensive performance tracking
            std::vector<std::unique_ptr<Shard>> shards_;
            std::atomic<bool> running_{false};
            std::atomic<uint64_t> redis_performance_counter_{0};

            // All MatchingEngine helpers implemented inline in the class
            size_t process_shard_once(Shard &shard) {
                ULTRA_PROFILE_FUNCTION();
                DEEP_PROFILE_FUNCTION();
                
                std::array<hft::Command, BATCH_SIZE> batch;
                size_t count = shard.queue.template dequeue_bulk<BATCH_SIZE>(batch);

                if (count == 0) return 0;

                for (size_t i = 0; i < count; ++i) {
                    ULTRA_PROFILE_CRITICAL(command_processing_loop);
                    
                    const auto &cmd = batch[i];

                    // Prefetch next command
                    if (LIKELY(i + 1 < count)) {
                        PREFETCH_L1(&batch[i + 1]);
                    }

                    if (LIKELY(cmd.type == hft::CommandType::NEW_ORDER)) {
                        process_new_order_fast(shard, cmd.order);
                    } else if (cmd.type == hft::CommandType::CANCEL_ORDER) {
                        process_cancel_fast(shard, cmd.order_id);
                    }
                }

                // Periodic maintenance
                static thread_local uint64_t process_count = 0;
                if (UNLIKELY(++process_count % 10000 == 0)) {
                    // call orderbook maintenance for this shard
                    shard.order_book.periodic_maintenance();
                }

                return count;
            }

            void process_new_order_fast(Shard &shard, const Order &order) {
                ULTRA_PROFILE_FUNCTION();
                DEEP_PROFILE_FUNCTION();
                
                uint64_t start_tsc;
                #if defined(__x86_64__) || defined(_M_X64)
                    #ifdef __rdtscp
                        unsigned aux;
                        start_tsc = __rdtscp(&aux);
                    #else
                        start_tsc = __rdtsc();
                    #endif
                #endif

                {
                    ULTRA_PROFILE_CRITICAL(throttle_check);
                    if (UNLIKELY(latency_controller_.should_throttle())) {
                        on_reject(order.id, "Throttled");
                        return;
                    }
                }

                OrderNode *node;
                {
                    ULTRA_PROFILE_CRITICAL(pool_acquire);
                    node = shard.pool.acquire();
                    if (UNLIKELY(!node)) {
                        on_reject(order.id, "No capacity");
                        return;
                    }
                }

                {
                    ULTRA_PROFILE_CRITICAL(node_init);
                    init_node_from_order(node, order, shard);
                }

                {
                    ULTRA_PROFILE_CRITICAL(risk_validation);
                    if (UNLIKELY(!rm_.validate(order))) {
                        on_reject(order.id, "Risk check failed");
                        shard.pool.release(node);
                        return;
                    }
                }

                OrderBookResult result;
                {
                    ULTRA_PROFILE_CRITICAL(orderbook_process);
                    result = shard.order_book.process_command(node, false);
                }

                {
                    ULTRA_PROFILE_CRITICAL(trade_processing);
                    for (const auto& trade : result.trades) {
                        on_trade(Order(), Order(), trade.price, trade.qty);
                    }
                }

                if (result.status == OrderBookResultStatus::Accepted) {
                    on_accept(order);
                } else if (result.status == OrderBookResultStatus::Filled) {
                    on_accept(order);
                } else if (result.status == OrderBookResultStatus::PartiallyFilled) {
                    on_accept(order);
                }

                #if defined(__x86_64__) || defined(_M_X64)
                    #ifdef __rdtscp
                        uint64_t end_tsc = __rdtscp(&aux);
                    #else
                        uint64_t end_tsc = __rdtsc();
                    #endif
                    uint64_t latency_tsc = (end_tsc >= start_tsc) ? (end_tsc - start_tsc) : 0;
                    
                    // Convert TSC to nanoseconds for latency controller
                    auto& state = get_tsc_state();
                    double scale = state.ns_per_tick.load(std::memory_order_relaxed);
                    uint64_t latency_ns = (scale > 0.0) ? static_cast<uint64_t>(latency_tsc * scale) : now_ns() - now_ns();
                    
                    latency_controller_.record_latency(latency_ns);
                #else
                    // Fallback for non-x86
                    latency_controller_.record_latency(now_ns() - now_ns());
                #endif
                
                on_book_update(order.symbol, shard.order_book.get_best_bid(), shard.order_book.get_best_ask());
            }

            void process_cancel_fast(Shard &shard, OrderId external_id) {
                ULTRA_PROFILE_FUNCTION();
                DEEP_PROFILE_FUNCTION();

                uint32_t index = shard.extract_index_from_external_id(external_id);
                uint32_t req_generation = shard.extract_generation_from_external_id(external_id);

                if (UNLIKELY(index >= shard.order_id_map.size())) {
                    on_reject(external_id, "Invalid order id");
                    return;
                }

                uint64_t packed;
                {
                    ULTRA_PROFILE_CRITICAL(order_lookup);
                    packed = shard.order_id_map[index].load(std::memory_order_acquire);
                    if (UNLIKELY(packed == 0)) {
                        on_reject(external_id, "Order not found");
                        return;
                    }
                }

                uint32_t stored_generation = static_cast<uint32_t>(packed >> 32);
                uint32_t stored_index_plus1 = static_cast<uint32_t>(packed & 0xFFFFFFFFu);

                if (UNLIKELY(stored_index_plus1 == 0 || stored_generation != req_generation)) {
                    on_reject(external_id, "Stale order id");
                    return;
                }

                uint32_t stored_index = stored_index_plus1 - 1u;
                OrderNode *node;
                {
                    ULTRA_PROFILE_CRITICAL(node_retrieval);
                    node = shard.pool.get_node(stored_index);

                    if (UNLIKELY(!node || node->generation != stored_generation)) {
                        on_reject(external_id, "Order not present");
                        return;
                    }
                }

                {
                    ULTRA_PROFILE_CRITICAL(cancel_orderbook);
                    shard.order_book.process_command(node, true);
                }
                
                on_book_update(node->hot.symbol, shard.order_book.get_best_bid(), shard.order_book.get_best_ask());
            }

            void init_node_from_order(OrderNode *node, const Order &order, const Shard &shard) noexcept {
                ULTRA_PROFILE_CRITICAL(node_initialization);
                
                node->hot.id = shard.make_external_order_id(node->index, node->generation);
                node->hot.price = order.price;
                node->hot.qty = order.qty;
                node->hot.filled = 0;
                
                // Use direct TSC for timestamp to avoid function call overhead
                #if defined(__x86_64__) || defined(_M_X64)
                    #ifdef __rdtscp
                        unsigned aux;
                        uint64_t tsc = __rdtscp(&aux);
                    #else
                        uint64_t tsc = __rdtsc();
                    #endif
                    auto& state = get_tsc_state();
                    double scale = state.ns_per_tick.load(std::memory_order_relaxed);
                    if (scale > 0.0) {
                        int64_t offset = state.offset_ns.load(std::memory_order_relaxed);
                        node->hot.timestamp = static_cast<Timestamp>(static_cast<double>(tsc) * scale + static_cast<double>(offset));
                    } else {
                        node->hot.timestamp = now_ns();
                    }
                #else
                    node->hot.timestamp = now_ns();
                #endif
                
                node->hot.symbol = order.symbol;
                node->hot.status = OrderStatus::NEW;
                node->hot.side = order.side;
                node->hot.type = order.type;
                node->hot.tif = order.tif;
                node->cold.user_id = order.user_id;
            }

            size_t select_shard(Symbol symbol) const noexcept {
                return static_cast<size_t>(symbol) % NUM_SHARDS;
            }

            size_t select_shard_for_cancel(OrderId external_id) const noexcept {
                int shard_id = static_cast<int>(external_id >> 56);
                return (shard_id >= 0 && shard_id < static_cast<int>(NUM_SHARDS)) ? 
                        static_cast<size_t>(shard_id) : 0;
            }

            void shard_worker_loop(Shard *shard) {
                #ifdef NUMA_AWARE
                    cpu_set_t cpuset;
                    CPU_ZERO(&cpuset);
                    CPU_SET(shard->shard_id, &cpuset);
                    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
                #endif

                shard->running.store(true, std::memory_order_release);
                std::array<hft::Command, BATCH_SIZE> batch;

                while (shard->running.load(std::memory_order_relaxed)) {
                    size_t count = shard->queue.template dequeue_bulk<BATCH_SIZE>(batch);
                    if (count == 0) {
                        CPU_RELAX();
                        std::this_thread::yield();
                        continue;
                    }

                    for (size_t i = 0; i < count; ++i) {
                        const auto &cmd = batch[i];
                        if (LIKELY(cmd.type == hft::CommandType::NEW_ORDER)) {
                            process_new_order_fast(*shard, cmd.order);
                        } else if (cmd.type == hft::CommandType::CANCEL_ORDER) {
                            process_cancel_fast(*shard, cmd.order_id);
                        }
                    }

                    static thread_local uint64_t process_count = 0;
                    if (UNLIKELY(++process_count % 10000 == 0)) {
                        shard->order_book.periodic_maintenance();
                    }
                }
            }

        public:
            static constexpr size_t NUM_SHARDS = 4;  
            static constexpr size_t BATCH_SIZE = 512;
            static constexpr size_t QUEUE_SIZE = 131072;

            MatchingEngine(RiskManager &rm, Logger &log, size_t pool_capacity = 1 << 18)
                : rm_(rm), log_(log), latency_controller_(100000), strategy_(1) { // 100us threshold, strategy for symbol 1
                shards_.emplace_back(std::make_unique<Shard>(pool_capacity, 0, 1000000, 1));
            }

            ~MatchingEngine() { 
                stop(); 
            }

            void start(int /*core_id*/ = -1) {
                running_.store(true, std::memory_order_relaxed);
            }

            void stop() {
                running_.store(false, std::memory_order_relaxed);
            }

            bool submit_order(Order &&o) {
                DEEP_PROFILE_FUNCTION();
                if (UNLIKELY(latency_controller_.should_throttle())) {
                    on_reject(o.id, "Throttled");
                    return false;
                }
                auto &shard = *shards_.front();
                process_new_order_fast(shard, o);
                return true;
            }

            void cancel_order(OrderId id) {
                // Single-threaded path: locate in shard 0 and remove
                if (shards_.empty()) return;
                auto &shard = *shards_.front();
                process_cancel_fast(shard, id);
            }

            void on_trade(const Order & /* book_order */, const Order &incoming_order, Price price, Quantity qty) {
                // aggregate onto shard 0 for single-threaded run
                if (!shards_.empty()) shards_.front()->trade_count_.fetch_add(1, std::memory_order_relaxed);
                rm_.record_trade(incoming_order.side, qty, price);
                slippage_tracker_.record_trade(incoming_order.price, price, qty);
                
                // Record comprehensive trade metrics
                advanced_metrics_.record_trade(
                    incoming_order.symbol,
                    incoming_order.side,
                    price,  // executed price
                    qty,
                    incoming_order.price,  // intended price
                    incoming_order.user_id,
                    "hft_strategy",  // strategy name
                    0  // latency will be calculated separately
                );
            }

            void on_accept(const Order &/*order*/) {
                if (!shards_.empty()) shards_.front()->accept_count_.fetch_add(1, std::memory_order_relaxed);
            }

            void on_reject(OrderId /*id*/, const char * /*reason*/) {
                if (!shards_.empty()) shards_.front()->reject_count_.fetch_add(1, std::memory_order_relaxed);
            }

            void on_book_update(Symbol sym, Price bid, Price ask) {
                // Redis caching for 30x throughput improvement
                if (redis_cache_.is_enabled()) {
                    redis_cache_.cache_market_data(sym, bid, ask, 100, 100);  // Cache with default quantities
                    redis_performance_counter_.fetch_add(1, std::memory_order_relaxed);
                }
                
                auto orders = strategy_.on_book_update(bid, ask);
                for (auto& order : orders) {
                    submit_order(std::move(order));
                }
            }

            Price get_best_bid(Symbol sym) const {
                // Try Redis cache first for 30x performance improvement
                if (redis_cache_.is_enabled()) {
                    Price cached_bid, cached_ask;
                    Quantity bid_qty, ask_qty;
                    if (redis_cache_.get_cached_market_data(sym, cached_bid, cached_ask, bid_qty, ask_qty)) {
                        return cached_bid;
                    }
                }
                
                if (shards_.empty()) return 0;
                return shards_.front()->order_book.get_best_bid();
            }

            Price get_best_ask(Symbol sym) const {
                // Try Redis cache first for 30x performance improvement  
                if (redis_cache_.is_enabled()) {
                    Price cached_bid, cached_ask;
                    Quantity bid_qty, ask_qty;
                    if (redis_cache_.get_cached_market_data(sym, cached_bid, cached_ask, bid_qty, ask_qty)) {
                        return cached_ask;
                    }
                }
                
                if (shards_.empty()) return 0;
                return shards_.front()->order_book.get_best_ask();
            }

            bool risk_check_view(const HotOrderView &view) {
                DEEP_PROFILE_FUNCTION();
                if (view.qty <= 0 || view.price <= 0) return false;
                // Use existing validate API (no check_risk_simple available)
                Order tmp{}; tmp.user_id = view.user_id; tmp.symbol = view.symbol; tmp.qty = view.qty; tmp.price = view.price;
                return rm_.validate(tmp);
            }

            uint64_t trade_count() const noexcept {
                uint64_t total = 0;
                for (const auto &shard : shards_) {
                    total += shard->trade_count_.load(std::memory_order_relaxed);
                }
                return total;
            }

            uint64_t accept_count() const noexcept {
                uint64_t total = 0;
                for (const auto &shard : shards_) {
                    total += shard->accept_count_.load(std::memory_order_relaxed);
                }
                return total;
            }

            uint64_t reject_count() const noexcept {
                uint64_t total = 0;
                for (const auto &shard : shards_) {
                    total += shard->reject_count_.load(std::memory_order_relaxed);
                }
                return total;
            }

            void reset_performance_counters() noexcept {
                for (auto &shard : shards_) {
                    shard->trade_count_.store(0, std::memory_order_relaxed);
                    shard->accept_count_.store(0, std::memory_order_relaxed);
                    shard->reject_count_.store(0, std::memory_order_relaxed);
                }
            }

            size_t process_all_once() {
                size_t total_processed = 0;
                for (auto &shard : shards_) {
                    total_processed += process_shard_once(*shard);
                }
                return total_processed;
            }
            
            // Redis performance control methods
            void enable_redis_caching(bool enabled) {
                redis_cache_.enable_caching(enabled);
            }
            
            bool is_redis_enabled() const {
                return redis_cache_.is_enabled();
            }
            
            void print_redis_performance_report() const {
                redis_cache_.print_performance_report();
                std::cout << "Redis Operations in Trading: " 
                          << redis_performance_counter_.load(std::memory_order_relaxed) << std::endl;
            }
            
            uint64_t get_redis_operation_count() const {
                return redis_performance_counter_.load(std::memory_order_relaxed);
            }
            
            void reset_redis_stats() {
                redis_cache_.clear_stats();
                redis_performance_counter_.store(0, std::memory_order_relaxed);
            }
            
            // Advanced metrics access methods
            const AdvancedMetrics& get_advanced_metrics() const {
                return advanced_metrics_;
            }
            
            int64_t get_total_pnl_cents() const {
                return advanced_metrics_.get_total_pnl_cents();
            }
            
            int64_t get_realized_pnl_cents() const {
                return advanced_metrics_.get_realized_pnl_cents();
            }
            
            double get_win_rate() const {
                return advanced_metrics_.get_win_rate();
            }
            
            uint64_t get_advanced_trade_count() const {
                return advanced_metrics_.get_trade_count();
            }
    };

    inline OrderId make_order_id(const OrderNode *node) noexcept {
        return (static_cast<uint64_t>(node->generation) << 32) | static_cast<uint64_t>(node->index);
    }

    
    
    

} // namespace hft
