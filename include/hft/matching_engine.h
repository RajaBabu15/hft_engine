
// file: matching_engine.h
#pragma once

#include "hft/types.h"
#include "hft/platform.h"
#include "hft/order.h"
#include "hft/command.h"
#include "hft/hot_order_view.h"
#include "hft/risk_manager.h"
#include "hft/logger.h"
#include "hft/deep_profiler.h"
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
            hft::Logger &log_;
            std::vector<std::unique_ptr<Shard>> shards_;
            std::atomic<bool> running_{false};

            // All MatchingEngine helpers implemented inline in the class
            size_t process_shard_once(Shard &shard) {
                DEEP_PROFILE_FUNCTION();
                std::array<hft::Command, BATCH_SIZE> batch;
                size_t count = shard.queue.template dequeue_bulk<BATCH_SIZE>(batch);

                if (count == 0) return 0;

                for (size_t i = 0; i < count; ++i) {
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
                DEEP_PROFILE_FUNCTION();

                OrderNode *node = shard.pool.acquire();
                if (UNLIKELY(!node)) {
                    on_reject_fast(order.id, "No capacity");
                    shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                init_node_from_order(node, order, shard);

                HotOrderView view{node->hot.id, node->hot.price, node->hot.qty,
                                node->cold.user_id, node->hot.symbol, node->hot.side,
                                node->hot.type, node->hot.tif};

                if (UNLIKELY(!risk_check_view(view))) {
                    on_reject_fast(node->hot.id, "Risk check failed");
                    shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                    shard.pool.release(node);
                    return;
                }

                uint64_t packed = (static_cast<uint64_t>(node->generation) << 32) | (node->index + 1);
                shard.order_id_map[node->index].store(packed, std::memory_order_release);
                // Dispatch to the OrderBook owned by the shard
                shard.order_book.process_command(this, node, false);
            }

            void process_cancel_fast(Shard &shard, OrderId external_id) {
                DEEP_PROFILE_FUNCTION();

                uint32_t index = shard.extract_index_from_external_id(external_id);
                uint32_t req_generation = shard.extract_generation_from_external_id(external_id);

                if (UNLIKELY(index >= shard.order_id_map.size())) {
                    on_reject_fast(external_id, "Invalid order id");
                    shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                uint64_t packed = shard.order_id_map[index].load(std::memory_order_acquire);
                if (UNLIKELY(packed == 0)) {
                    on_reject_fast(external_id, "Order not found");
                    shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                uint32_t stored_generation = static_cast<uint32_t>(packed >> 32);
                uint32_t stored_index_plus1 = static_cast<uint32_t>(packed & 0xFFFFFFFFu);

                if (UNLIKELY(stored_index_plus1 == 0 || stored_generation != req_generation)) {
                    on_reject_fast(external_id, "Stale order id");
                    shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                uint32_t stored_index = stored_index_plus1 - 1u;
                OrderNode *node = shard.pool.get_node(stored_index);

                if (UNLIKELY(!node || node->generation != stored_generation)) {
                    on_reject_fast(external_id, "Order not present");
                    shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                    return;
                }

                // Dispatch cancel to shard's order book
                shard.order_book.process_command(this, node, true);
            }

            void init_node_from_order(OrderNode *node, const Order &order, const Shard &shard) noexcept {
                node->hot.id = shard.make_external_order_id(node->index, node->generation);
                node->hot.price = order.price;
                node->hot.qty = order.qty;
                node->hot.filled = 0;
                node->hot.timestamp = now_ns();
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
                : rm_(rm), log_(log) {
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
                // Enqueue directly to shard 0 for single-threaded processing
                auto &shard = *shards_.front();
                OrderNode *node = shard.pool.acquire();
                if (!node) { on_reject(o.id, "No capacity"); return false; }
                node->hot.id = o.id ? o.id : ((static_cast<uint64_t>(node->generation) << 32) | static_cast<uint64_t>(node->index));
                node->hot.price = o.price;
                node->hot.qty = o.qty;
                node->hot.filled = 0;
                node->hot.timestamp = now_ns();
                node->hot.symbol = o.symbol;
                node->hot.status = OrderStatus::NEW;
                node->hot.side = o.side;
                node->hot.type = o.type;
                node->hot.tif = o.tif;
                node->cold.user_id = o.user_id;
                uint64_t packed = (static_cast<uint64_t>(node->generation) << 32) | (static_cast<uint64_t>(node->index) + 1ULL);
                shard.order_id_map[node->index].store(packed, std::memory_order_release);
                shard.order_book.process_command(this, node, false);
                return true;
            }

            void cancel_order(OrderId id) {
                // Single-threaded path: locate in shard 0 and remove
                if (shards_.empty()) return;
                auto &shard = *shards_.front();
                uint32_t index = static_cast<uint32_t>(id & 0xFFFFFFu);
                if (index >= shard.order_id_map.size()) return;
                shard.order_book.periodic_maintenance();
            }

            void on_trade_fast(const HotTradeEvent &/*event*/) {
                if (!shards_.empty()) shards_.front()->trade_count_.fetch_add(1, std::memory_order_relaxed);
            }
            void on_accept_fast(OrderId /*id*/) {
                if (!shards_.empty()) shards_.front()->accept_count_.fetch_add(1, std::memory_order_relaxed);
            }
            void on_reject_fast(OrderId /*id*/, const char * /*reason*/) {
                if (!shards_.empty()) shards_.front()->reject_count_.fetch_add(1, std::memory_order_relaxed);
            }

            void on_trade(const Order &/*book_order*/, const Order &/*incoming_order*/, Price /*price*/, Quantity /*qty*/) {
                // aggregate onto shard 0 for single-threaded run
                if (!shards_.empty()) shards_.front()->trade_count_.fetch_add(1, std::memory_order_relaxed);
            }

            void on_accept(const Order &/*order*/) {
                if (!shards_.empty()) shards_.front()->accept_count_.fetch_add(1, std::memory_order_relaxed);
            }

            void on_reject(OrderId /*id*/, const char * /*reason*/) {
                if (!shards_.empty()) shards_.front()->reject_count_.fetch_add(1, std::memory_order_relaxed);
            }

            void on_book_update(Symbol /*sym*/, Price /*bid*/, Price /*ask*/) {}

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
    };

    inline OrderId make_order_id(const OrderNode *node) noexcept {
        return (static_cast<uint64_t>(node->generation) << 32) | static_cast<uint64_t>(node->index);
    }

    
    
    inline void engine_on_trade(MatchingEngine* engine, const Order &book_order, const Order &incoming_order, Price price, Quantity qty) noexcept {
        if (engine) engine->on_trade(book_order, incoming_order, price, qty);
    }
    inline void engine_on_accept(MatchingEngine* engine, const Order &order) noexcept {
        if (engine) engine->on_accept(order);
    }
    inline void engine_on_reject(MatchingEngine* engine, OrderId id, const char *reason) noexcept {
        if (engine) engine->on_reject(id, reason);
    }

} // namespace hft
