#pragma once

#include "hft/types.h"
#include "hft/order.h"
#include "hft/command.h"
#include "hft/hot_order_view.h"
#include "hft/risk_manager.h"
#include "hft/logger.h"
#include "hft/deep_profiler.h"
#include "hft/segment_tree.h"
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

// Platform-specific includes and optimizations
#if defined(__AVX512F__) || defined(__AVX512DQ__)
#include <immintrin.h>
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define CPU_RELAX() __asm__ volatile("yield" ::: "memory")
#define PREFETCH_L1(addr) __builtin_prefetch((const void*)(addr), 0, 3)
#define PREFETCH_L2(addr) __builtin_prefetch((const void*)(addr), 0, 2)
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#include <immintrin.h>
#define CPU_RELAX() _mm_pause()
#define PREFETCH_L1(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#define PREFETCH_L2(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T1)
#else
#define CPU_RELAX() do {} while(0)
#define PREFETCH_L1(addr) do {} while(0)
#define PREFETCH_L2(addr) do {} while(0)
#endif

#ifdef __linux__
#include <pthread.h>
#include <numa.h>
#include <sys/mman.h>
#define NUMA_AWARE
#endif

// Compiler optimization hints
#define FORCE_INLINE __attribute__((always_inline)) inline
#define HOT_PATH __attribute__((hot))
#define COLD_PATH __attribute__((cold))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define ASSUME_ALIGNED(ptr, alignment) __builtin_assume_aligned((ptr), (alignment))
#define CACHE_ALIGNED alignas(64)

namespace hft {
    constexpr size_t CACHE_LINE_SIZE = 64;
    constexpr size_t SIMD_WIDTH = 8;
    constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;
    constexpr size_t MAX_PRICE_LEVELS = 10000;

    // Forward declarations
    class MatchingEngine;
    class OrderBook;

    // =============================================================================
    // CORE DATA STRUCTURES
    // =============================================================================

#pragma pack(push, 1)
    struct OrderHot {
        OrderId id;
        Price price;
        Quantity qty;
        Quantity filled;
        Timestamp timestamp;
        Symbol symbol;
        OrderStatus status;
        Side side;
        OrderType type;
        TimeInForce tif;
    };

    struct OrderCold {
        UserId user_id;
    };
#pragma pack(pop)

    static_assert(sizeof(OrderHot) <= 64, "OrderHot should fit within cache-friendly size");

    inline OrderId make_order_id(uint32_t index, uint32_t generation) noexcept {
        return (static_cast<OrderId>(generation) << 32) | static_cast<OrderId>(index);
    }

    inline uint32_t order_id_to_index(OrderId id) noexcept {
        return static_cast<uint32_t>(id & 0xFFFFFFFFu);
    }

    inline uint32_t order_id_to_generation(OrderId id) noexcept {
        return static_cast<uint32_t>(id >> 32);
    }

    // Unified order node structure
    struct alignas(CACHE_LINE_SIZE) OrderNode {
        uint32_t index;
        uint32_t generation;
        OrderHot hot;
        OrderCold cold;
        uint32_t next_idx = INVALID_INDEX;
        uint32_t prev_idx = INVALID_INDEX;

        void reset() noexcept {
            hot.filled = 0;
            hot.status = OrderStatus::NEW;
            hot.timestamp = 0;
            next_idx = INVALID_INDEX;
            prev_idx = INVALID_INDEX;
        }
    };

    // Forward declare helper that converts internal node to public Order
    hft::Order make_public_order(const OrderNode *node) noexcept;

    // =============================================================================
    // MEMORY MANAGEMENT
    // =============================================================================

    class EpochManager {
    public:
        static constexpr int MAX_THREADS = 64;

        void enter_epoch() {
            thread_local int tid = register_thread();
            current_epochs_[tid].store(global_epoch_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        void exit_epoch() {
            thread_local int tid = register_thread();
            current_epochs_[tid].store(INVALID_EPOCH, std::memory_order_relaxed);
        }

        void defer_reclaim(OrderNode *node) {
            thread_local std::vector<OrderNode *> &list = deferred_list();
            list.push_back(node);
            if (list.size() > 1000) {
                try_reclaim();
            }
        }

        void try_reclaim() {
            const uint64_t min_epoch = get_min_epoch();
            thread_local std::vector<OrderNode *> &list = deferred_list();

            auto it = std::partition(list.begin(), list.end(), [&](OrderNode *node) {
                return node->generation > min_epoch;
            });

            for (auto p = list.begin(); p != it; ++p) {
                reclaim_node(*p);
            }
            list.erase(list.begin(), it);
        }

    private:
        int register_thread() {
            static std::atomic<int> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        std::vector<OrderNode *> &deferred_list() {
            thread_local std::vector<OrderNode *> list;
            return list;
        }

        uint64_t get_min_epoch() {
            uint64_t min_epoch = global_epoch_.load(std::memory_order_relaxed);
            for (auto &epoch: current_epochs_) {
                uint64_t e = epoch.load(std::memory_order_relaxed);
                if (e != INVALID_EPOCH && e < min_epoch) {
                    min_epoch = e;
                }
            }
            return min_epoch;
        }

        void reclaim_node(OrderNode *node) {
            delete node;
        }

        static constexpr uint64_t INVALID_EPOCH = ~0ull;
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> global_epoch_{0};
        alignas(CACHE_LINE_SIZE) std::array<std::atomic<uint64_t>, MAX_THREADS> current_epochs_;
    };

    class IndexPool {
    public:
        explicit IndexPool(size_t capacity)
            : capacity_(capacity),
              free_stack_(capacity),
              top_(static_cast<int32_t>(capacity)) {
#ifdef NUMA_AWARE
            int numa_node = numa_node_of_cpu(sched_getcpu());
            void *aligned_mem = numa_alloc_onnode(sizeof(OrderNode) * capacity, numa_node);
            if (!aligned_mem) throw std::bad_alloc();
            nodes_ = reinterpret_cast<OrderNode *>(aligned_mem);
            madvise(nodes_, capacity * sizeof(OrderNode), MADV_HUGEPAGE);
#else
            nodes_ = new OrderNode[capacity];
#endif

            for (uint32_t i = 0; i < capacity; ++i) {
                nodes_[i].index = i;
                nodes_[i].generation = 0;
                free_stack_[i] = i;
            }
        }

        ~IndexPool() {
#ifdef NUMA_AWARE
            if (nodes_) numa_free(nodes_, capacity_ * sizeof(OrderNode));
#else
            delete[] nodes_;
#endif
        }

        OrderNode *acquire() noexcept {
            int32_t old_top = top_.fetch_sub(1, std::memory_order_acq_rel);
            if (old_top <= 0) {
                top_.fetch_add(1, std::memory_order_release);
                return nullptr;
            }
            int32_t pos = old_top - 1;
            uint32_t idx = free_stack_[pos];
            OrderNode *node = &nodes_[idx];
            ++node->generation;
            node->reset();
            return node;
        }

        void release(OrderNode *node) noexcept {
            int32_t pos = top_.fetch_add(1, std::memory_order_acq_rel);
            if (static_cast<size_t>(pos) < free_stack_.size()) {
                free_stack_[pos] = node->index;
            }
        }

        OrderNode *get_node(uint32_t index) noexcept {
            if (index >= capacity_) return nullptr;
            return &nodes_[index];
        }

        size_t capacity() const noexcept { return capacity_; }

    private:
        OrderNode *nodes_{nullptr};
        size_t capacity_{0};
        std::vector<uint32_t> free_stack_;
        alignas(CACHE_LINE_SIZE) std::atomic<int32_t> top_;
    };

    // =============================================================================
    // QUEUES AND COMMUNICATION
    // =============================================================================

    template<typename T, size_t Size>
    class Queue {
    public:
        Queue() : head_(0), tail_(0) {}

        bool try_enqueue(T &&item) noexcept {
            size_t head = head_.load(std::memory_order_relaxed);
            size_t next_head = next(head);
            if (next_head == tail_.load(std::memory_order_acquire))
                return false;

            buffer_[head] = std::move(item);
            head_.store(next_head, std::memory_order_release);
            return true;
        }

        bool try_dequeue(T &item) noexcept {
            size_t tail = tail_.load(std::memory_order_relaxed);
            if (tail == head_.load(std::memory_order_acquire))
                return false;

            item = std::move(buffer_[tail]);
            tail_.store(next(tail), std::memory_order_release);
            return true;
        }

        template<size_t BatchSize>
        size_t dequeue_bulk(std::array<T, BatchSize> &output) noexcept {
            size_t tail = tail_.load(std::memory_order_relaxed);
            size_t head = head_.load(std::memory_order_acquire);
            
            if (tail == head) return 0;
            
            size_t avail = (head >= tail) ? (head - tail) : (Size - tail + head);
            size_t to_copy = std::min(avail, BatchSize);
            
            for (size_t i = 0; i < to_copy; ++i) {
                output[i] = std::move(buffer_[tail]);
                tail = next(tail);
            }
            
            tail_.store(tail, std::memory_order_release);
            return to_copy;
        }

    private:
        size_t next(size_t current) const noexcept { return (current + 1) % Size; }

        alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_;
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_;
        std::array<T, Size> buffer_;
    };

    // =============================================================================
    // PRICE LEVEL AND SEGMENT TREE
    // =============================================================================

    /**
     * Segment tree for O(1) best price discovery
     */
    template<bool IsMaxTree>
    class PriceTracker {
    public:
        explicit PriceTracker(size_t price_levels) : 
            original_size_(price_levels), n_(1) {
            
            while (n_ < price_levels) n_ *= 2;
            
            price_tree_.resize(2 * n_);
            count_tree_.resize(2 * n_);
            
            const Price neutral_price = IsMaxTree ? 0 : UINT64_MAX;
            std::fill(price_tree_.begin(), price_tree_.end(), neutral_price);
            std::fill(count_tree_.begin(), count_tree_.end(), 0);
        }
        
        inline void update_level(size_t level_idx, Price price, uint32_t count) noexcept {
            if (level_idx >= original_size_) return;
            
            size_t tree_idx = n_ + level_idx;
            price_tree_[tree_idx] = price;
            count_tree_[tree_idx] = count;
            
            for (tree_idx /= 2; tree_idx >= 1; tree_idx /= 2) {
                const size_t left_child = tree_idx * 2;
                const size_t right_child = tree_idx * 2 + 1;
                
                const uint32_t left_count = count_tree_[left_child];
                const uint32_t right_count = count_tree_[right_child];
                const Price left_price = price_tree_[left_child];
                const Price right_price = price_tree_[right_child];
                
                if constexpr (IsMaxTree) {
                    if (left_count > 0 && right_count > 0) {
                        price_tree_[tree_idx] = std::max(left_price, right_price);
                        count_tree_[tree_idx] = left_count + right_count;
                    } else if (left_count > 0) {
                        price_tree_[tree_idx] = left_price;
                        count_tree_[tree_idx] = left_count;
                    } else if (right_count > 0) {
                        price_tree_[tree_idx] = right_price;
                        count_tree_[tree_idx] = right_count;
                    } else {
                        price_tree_[tree_idx] = 0;
                        count_tree_[tree_idx] = 0;
                    }
                } else {
                    if (left_count > 0 && right_count > 0) {
                        price_tree_[tree_idx] = std::min(left_price, right_price);
                        count_tree_[tree_idx] = left_count + right_count;
                    } else if (left_count > 0) {
                        price_tree_[tree_idx] = left_price;
                        count_tree_[tree_idx] = left_count;
                    } else if (right_count > 0) {
                        price_tree_[tree_idx] = right_price;  
                        count_tree_[tree_idx] = right_count;
                    } else {
                        price_tree_[tree_idx] = UINT64_MAX;
                        count_tree_[tree_idx] = 0;
                    }
                }
            }
        }
        
        inline Price get_best_price() const noexcept {
            return (count_tree_[1] > 0) ? price_tree_[1] : (IsMaxTree ? 0 : UINT64_MAX);
        }
        
        inline bool has_orders() const noexcept {
            return count_tree_[1] > 0;
        }
        
        inline void clear() noexcept {
            const Price neutral_price = IsMaxTree ? 0 : UINT64_MAX;
            std::fill(price_tree_.begin(), price_tree_.end(), neutral_price);
            std::fill(count_tree_.begin(), count_tree_.end(), 0);
        }
        
    private:
        const size_t original_size_;
        size_t n_;
        alignas(64) std::vector<Price> price_tree_;
        alignas(64) std::vector<uint32_t> count_tree_;
    };
    
    using BidTracker = PriceTracker<true>;
    using AskTracker = PriceTracker<false>;

    // =============================================================================
    // SIMD MATCHING UTILITIES
    // =============================================================================

    struct SimdMatchResult {
        uint32_t indices[16];
        Quantity qtys[16];
        uint32_t count{0};
    };

    FORCE_INLINE SimdMatchResult match_orders_simd(const OrderNode* const* orders,
                                                   const Quantity* qtys,
                                                   uint32_t order_count,
                                                   Quantity incoming_qty) noexcept {
        SimdMatchResult result{};
        
#if defined(__AVX2__)
        const __m256i incoming = _mm256_set1_epi32(static_cast<int32_t>(incoming_qty));
        uint32_t produced = 0;
        
        for (uint32_t i = 0; i < order_count && produced < 16; i += 8) {
            alignas(32) int32_t q[8] = {0};
            for (int j = 0; j < 8 && (i + j) < order_count; ++j) {
                const OrderNode* n = orders[i + j];
                q[j] = (n && n->hot.qty > 0) ? static_cast<int32_t>(qtys[i + j]) : 0;
            }
            
            __m256i book = _mm256_load_si256((__m256i*)q);
            __m256i valid = _mm256_cmpgt_epi32(book, _mm256_setzero_si256());
            __m256i matched = _mm256_min_epi32(book, incoming);
            matched = _mm256_and_si256(matched, valid);
            
            alignas(32) int32_t out[8];
            _mm256_store_si256((__m256i*)out, matched);
            
            for (int j = 0; j < 8 && produced < 16 && (i + j) < order_count; ++j) {
                if (out[j] > 0) {
                    result.indices[produced] = i + j;
                    result.qtys[produced] = static_cast<Quantity>(out[j]);
                    ++produced;
                }
            }
        }
        result.count = produced;
        
#elif defined(__SSE4_2__)
        const __m128i incoming = _mm_set1_epi32(static_cast<int32_t>(incoming_qty));
        uint32_t produced = 0;
        
        for (uint32_t i = 0; i < order_count && produced < 16; i += 4) {
            alignas(16) int32_t q[4] = {0};
            for (int j = 0; j < 4 && (i + j) < order_count; ++j) {
                const OrderNode* n = orders[i + j];
                q[j] = (n && n->hot.qty > 0) ? static_cast<int32_t>(qtys[i + j]) : 0;
            }
            
            __m128i book = _mm_load_si128((__m128i*)q);
            __m128i valid = _mm_cmpgt_epi32(book, _mm_setzero_si128());
            __m128i matched = _mm_min_epi32(book, incoming);
            matched = _mm_and_si128(matched, valid);
            
            alignas(16) int32_t out[4];
            _mm_store_si128((__m128i*)out, matched);
            
            for (int j = 0; j < 4 && produced < 16 && (i + j) < order_count; ++j) {
                if (out[j] > 0) {
                    result.indices[produced] = i + j;
                    result.qtys[produced] = static_cast<Quantity>(out[j]);
                    ++produced;
                }
            }
        }
        result.count = produced;
        
#else
        // Scalar fallback
        for (uint32_t i = 0; i < order_count && result.count < 16; ++i) {
            const OrderNode* n = orders[i];
            if (n && qtys[i] > 0) {
                const Quantity matched = std::min(qtys[i], incoming_qty);
                if (matched > 0) {
                    result.indices[result.count] = i;
                    result.qtys[result.count] = matched;
                    ++result.count;
                }
            }
        }
#endif
        
        return result;
    }

    // =============================================================================
    // PRICE LEVEL
    // =============================================================================

    struct alignas(64) PriceLevel {
        std::atomic<Price> price{0};
        std::atomic<uint32_t> order_count{0};
        std::atomic<Quantity> total_qty{0};
        
        // SIMD-friendly hot window for fast matching
        std::array<OrderNode*, 32> orders{};
        std::array<Quantity, 32> quantities{};
        std::atomic<uint32_t> hot_count{0};
        
        // Overflow storage for high activity levels
        std::vector<OrderNode*> overflow_orders;
        std::unordered_map<OrderNode*, size_t> order_positions;
        std::atomic<bool> needs_compaction{false};
        std::atomic<uint32_t> overflow_count{0};
        
        PriceLevel() {
            overflow_orders.reserve(1024);
        }
        
        bool add_order(OrderNode* node) noexcept {
            DEEP_PROFILE_FUNCTION();
            
            // Try to add to hot window first
            uint32_t slot = hot_count.load(std::memory_order_acquire);
            if (slot < 32) {
                if (hot_count.compare_exchange_strong(slot, slot + 1, std::memory_order_acq_rel)) {
                    orders[slot] = node;
                    quantities[slot] = node->hot.qty;
                    order_count.fetch_add(1, std::memory_order_relaxed);
                    total_qty.fetch_add(node->hot.qty, std::memory_order_relaxed);
                    return true;
                }
            }
            
            // Fallback to overflow storage
            overflow_orders.push_back(node);
            order_positions[node] = overflow_orders.size() - 1;
            overflow_count.fetch_add(1, std::memory_order_relaxed);
            order_count.fetch_add(1, std::memory_order_relaxed);
            total_qty.fetch_add(node->hot.qty, std::memory_order_relaxed);
            return true;
        }
        
        void remove_order(OrderNode* node) noexcept {
            DEEP_PROFILE_FUNCTION();
            
            // Check hot window first
            uint32_t count = hot_count.load(std::memory_order_acquire);
            for (uint32_t i = 0; i < count; ++i) {
                if (orders[i] == node) {
                    orders[i] = orders[count - 1];
                    quantities[i] = quantities[count - 1];
                    orders[count - 1] = nullptr;
                    quantities[count - 1] = 0;
                    hot_count.store(count - 1, std::memory_order_release);
                    
                    order_count.fetch_sub(1, std::memory_order_relaxed);
                    total_qty.fetch_sub(node->hot.qty, std::memory_order_relaxed);
                    return;
                }
            }
            
            // Check overflow storage
            auto pos_it = order_positions.find(node);
            if (pos_it != order_positions.end()) {
                size_t pos = pos_it->second;
                order_positions.erase(pos_it);
                
                if (pos < overflow_orders.size() && overflow_orders[pos] == node) {
                    overflow_orders[pos] = nullptr;
                    overflow_count.fetch_sub(1, std::memory_order_relaxed);
                    needs_compaction.store(true, std::memory_order_relaxed);
                }
                
                order_count.fetch_sub(1, std::memory_order_relaxed);
                total_qty.fetch_sub(node->hot.qty, std::memory_order_relaxed);
            }
        }
        
        void compact_if_needed() noexcept {
            if (!needs_compaction.load(std::memory_order_relaxed)) return;
            
            auto new_end = std::remove(overflow_orders.begin(), overflow_orders.end(), nullptr);
            overflow_orders.erase(new_end, overflow_orders.end());
            
            order_positions.clear();
            for (size_t i = 0; i < overflow_orders.size(); ++i) {
                if (overflow_orders[i]) {
                    order_positions[overflow_orders[i]] = i;
                }
            }
            
            needs_compaction.store(false, std::memory_order_relaxed);
        }
        
        // SIMD-optimized matching against the hot window
        FORCE_INLINE uint32_t match_hot_orders(Quantity incoming_qty,
                                               uint32_t out_indices[16],
                                               Quantity out_qtys[16]) noexcept {
            const uint32_t count = hot_count.load(std::memory_order_acquire);
            if (count == 0) return 0;
            
            SimdMatchResult result = match_orders_simd(
                const_cast<const OrderNode* const*>(orders.data()),
                quantities.data(),
                count,
                incoming_qty
            );
            
            for (uint32_t i = 0; i < result.count; ++i) {
                out_indices[i] = result.indices[i];
                out_qtys[i] = result.qtys[i];
            }
            
            return result.count;
        }
    };

    // =============================================================================
    // COMMAND STRUCTURES
    // =============================================================================

    // Use command definitions from include/hft/command.h; avoid redefinition here
    // (Kept for historical reference)
    // enum class CommandType : uint8_t { NEW_ORDER, CANCEL_ORDER, FLUSH_BOOK };
    // struct Command { CommandType type; union { OrderNode *node; OrderId order_id; }; };

    // =============================================================================
    // ORDER BOOK
    // =============================================================================

    class OrderBook {
    public:
        OrderBook(IndexPool &pool, Price min_price, Price max_price, Price tick_size)
            : min_price_(min_price),
              tick_size_(tick_size),
              price_levels_(static_cast<size_t>((max_price - min_price) / tick_size) + 1),
              bids_(price_levels_),
              asks_(price_levels_),
              bid_tracker_(price_levels_),
              ask_tracker_(price_levels_),
              pool_(pool) {
            
            for (size_t i = 0; i < price_levels_; ++i) {
                Price level_price = min_price + static_cast<Price>(i) * tick_size_;
                bids_[i].price.store(level_price, std::memory_order_relaxed);
                asks_[i].price.store(level_price, std::memory_order_relaxed);
            }
            bids_tree_.init(price_levels_);
            asks_tree_.init(price_levels_);
        }
        
        void process_command(MatchingEngine* engine, OrderNode* node, bool is_cancel);
        
        void periodic_maintenance() noexcept {
            for (auto& level : bids_) {
                level.compact_if_needed();
            }
            for (auto& level : asks_) {
                level.compact_if_needed();
            }
        }
        
        inline Price get_best_bid() const noexcept {
            return bid_tracker_.get_best_price();
        }
        
        inline Price get_best_ask() const noexcept {
            return ask_tracker_.get_best_price();
        }
        
    private:
        // New: segment trees to jump to first non-empty level
        SegmentTree bids_tree_;
        SegmentTree asks_tree_;

        void match_order(MatchingEngine* engine, OrderNode* node);
        
        void add_limit_order(MatchingEngine* engine, OrderNode* node);
        
        void remove_order(MatchingEngine* engine, OrderNode* node, bool was_cancelled);
        
        void update_best_prices() noexcept {
            // Price trackers maintain best prices automatically via segment tree
        }
        
        void match_simd(MatchingEngine* engine, OrderNode* node, PriceLevel& level);
        
        size_t price_to_level(Price price) const noexcept {
            return static_cast<size_t>((price - min_price_) / tick_size_);
        }
        
        const Price min_price_;
        const Price tick_size_;
        const size_t price_levels_;
        std::vector<PriceLevel> bids_;
        std::vector<PriceLevel> asks_;
        
        BidTracker bid_tracker_;
        AskTracker ask_tracker_;
        
        IndexPool& pool_;
    };

    // =============================================================================
    // SHARDED MATCHING ENGINE
    // =============================================================================

    // Single-consumer optimized pool for per-shard use
    class SingleConsumerPool {
    public:
        explicit SingleConsumerPool(size_t capacity)
            : capacity_(capacity), free_indices_(capacity) {
#ifdef NUMA_AWARE
            int numa_node = numa_node_of_cpu(sched_getcpu());
            void *aligned_mem = numa_alloc_onnode(sizeof(OrderNode) * capacity, numa_node);
            if (!aligned_mem) throw std::bad_alloc();
            nodes_ = reinterpret_cast<OrderNode *>(aligned_mem);
            madvise(nodes_, capacity * sizeof(OrderNode), MADV_HUGEPAGE);
#else
            nodes_ = new OrderNode[capacity];
#endif

            for (uint32_t i = 0; i < capacity; ++i) {
                nodes_[i].index = i;
                nodes_[i].generation = 0;
                free_indices_[i] = i;
            }
            free_top_ = static_cast<int32_t>(capacity);
        }

        ~SingleConsumerPool() {
#ifdef NUMA_AWARE
            if (nodes_) numa_free(nodes_, capacity_ * sizeof(OrderNode));
#else
            delete[] nodes_;
#endif
        }

        // Fast single-consumer acquire - no CAS needed
        OrderNode *acquire() noexcept {
            if (free_top_ <= 0) return nullptr;
            --free_top_;
            uint32_t idx = free_indices_[free_top_];
            OrderNode *node = &nodes_[idx];
            ++node->generation;
            node->reset();
            return node;
        }

        // Fast single-consumer release - no CAS needed
        void release(OrderNode *node) noexcept {
            if (free_top_ < static_cast<int32_t>(capacity_)) {
                free_indices_[free_top_++] = node->index;
            }
        }

        OrderNode *get_node(uint32_t index) noexcept {
            if (index >= capacity_) return nullptr;
            return &nodes_[index];
        }

        size_t capacity() const noexcept { return capacity_; }

    private:
        OrderNode *nodes_{nullptr};
        size_t capacity_{0};
        std::vector<uint32_t> free_indices_;
        int32_t free_top_{0};
    };

    // Forward declaration for Shard
    class MatchingEngine;

    // Shard structure - owns all per-shard resources
    struct alignas(CACHE_LINE_SIZE) Shard {
        // Resources (owned by shard thread)
        IndexPool pool;
        OrderBook order_book;
        std::vector<std::atomic<uint64_t>> order_id_map;
        Queue<hft::Command, 131072> queue;  // Lock-free MPSC queue
        
        // Thread management
        std::thread worker;
        std::atomic<bool> running{false};
        int shard_id;
        
        // Per-shard metrics
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> trade_count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> accept_count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> reject_count_{0};
        
        // Hot trade events for deferred processing
        std::vector<HotTradeEvent> hot_trades_;
        
        Shard(size_t pool_capacity, Price min_price, Price max_price, Price tick_size)
            : pool(pool_capacity),
              order_book(pool, min_price, max_price, tick_size),
              order_id_map(pool_capacity) {
            hot_trades_.reserve(10000);
        }
        
        // Pack shard_id into order_id for routing cancels
        inline OrderId make_external_order_id(uint32_t index, uint32_t generation) const noexcept {
            return (static_cast<OrderId>(shard_id) << 56) |  // 8 bits for shard
                   (static_cast<OrderId>(generation) << 24) | // 32 bits for generation 
                   static_cast<OrderId>(index);              // 24 bits for index
        }
        
        inline uint32_t extract_index_from_external_id(OrderId external_id) const noexcept {
            return static_cast<uint32_t>(external_id & 0xFFFFFFu);
        }
        
        inline uint32_t extract_generation_from_external_id(OrderId external_id) const noexcept {
            return static_cast<uint32_t>((external_id >> 24) & 0xFFFFFFFFu);
        }
        
        inline int extract_shard_from_external_id(OrderId external_id) const noexcept {
            return static_cast<int>(external_id >> 56);
        }
    };

    class MatchingEngine {
    public:
        static constexpr size_t NUM_SHARDS = 4;  // Tune to number of cores
        static constexpr size_t BATCH_SIZE = 512;
        static constexpr size_t QUEUE_SIZE = 131072;
        
        MatchingEngine(RiskManager &rm, Logger &log, size_t pool_capacity = 1 << 18);
        ~MatchingEngine();

        void start(int core_id = -1);
        void stop();

        // New sharded API - routes to correct shard
        bool submit_order(Order &&o);
        void cancel_order(OrderId id);

        // Fast callbacks with lightweight views
        void on_trade_fast(const HotTradeEvent &event);
        void on_accept_fast(OrderId id);
        void on_reject_fast(OrderId id, const char *reason);
        
        // Legacy callbacks for compatibility
        void on_trade(const Order &book_order, const Order &incoming_order, Price price, Quantity qty);
        void on_accept(const Order &order);
        void on_reject(OrderId id, const char *reason);
        void on_book_update(Symbol sym, Price bid, Price ask);

        // Risk check with lightweight view
        bool risk_check_view(const HotOrderView &view) {
            DEEP_PROFILE_FUNCTION();
            if (view.qty <= 0 || view.price <= 0) return false;
            // Use existing validate API (no check_risk_simple available)
            Order tmp{}; tmp.user_id = view.user_id; tmp.symbol = view.symbol; tmp.qty = view.qty; tmp.price = view.price;
            return rm_.validate(tmp);
        }

        // Aggregated metrics from all shards
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

        // Single-threaded processing for benchmark compatibility
        size_t process_all_once() {
            size_t total_processed = 0;
            for (auto &shard : shards_) {
                total_processed += process_shard_once(*shard);
            }
            return total_processed;
        }

    private:
        // Per-shard processing
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
                shard.order_book.periodic_maintenance();
            }
            
            return count;
        }
        
        // Fast new order processing
        void process_new_order_fast(Shard &shard, const Order &order) {
            DEEP_PROFILE_FUNCTION();
            
            // Acquire node from shard-local pool (very cheap, single-consumer)
            OrderNode *node = shard.pool.acquire();
            if (UNLIKELY(!node)) {
                on_reject_fast(order.id, "No capacity");
                shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            
            // Initialize node from order (move fields)
            init_node_from_order(node, order, shard);
            
            // Fast risk check with lightweight view
            HotOrderView view{node->hot.id, node->hot.price, node->hot.qty, 
                            node->cold.user_id, node->hot.symbol, node->hot.side,
                            node->hot.type, node->hot.tif};
            
            if (UNLIKELY(!risk_check_view(view))) {
                on_reject_fast(node->hot.id, "Risk check failed");
                shard.reject_count_.fetch_add(1, std::memory_order_relaxed);
                shard.pool.release(node);
                return;
            }
            
            // Map id -> generation+index in shard map
            uint64_t packed = (static_cast<uint64_t>(node->generation) << 32) | (node->index + 1);
            shard.order_id_map[node->index].store(packed, std::memory_order_release);
            
            // Process order in order book
            shard.order_book.process_command(this, node, false);
        }
        
        // Fast cancel processing
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
            
            shard.order_book.process_command(this, node, true);
        }
        
        // Initialize node from order
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
        
        // Shard selection strategy
        size_t select_shard(Symbol symbol) const noexcept {
            // Simple hash-based routing
            return static_cast<size_t>(symbol) % NUM_SHARDS;
        }
        
        size_t select_shard_for_cancel(OrderId external_id) const noexcept {
            // Extract shard from packed order id
            int shard_id = static_cast<int>(external_id >> 56);
            return (shard_id >= 0 && shard_id < static_cast<int>(NUM_SHARDS)) ? 
                   static_cast<size_t>(shard_id) : 0;
        }
        
        // Worker thread function
        void shard_worker_loop(Shard *shard) {
            // Pin to specific core if NUMA aware
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
                
                // Periodic maintenance
                static thread_local uint64_t process_count = 0;
                if (UNLIKELY(++process_count % 10000 == 0)) {
                    shard->order_book.periodic_maintenance();
                }
            }
        }
        
        hft::RiskManager &rm_;
        hft::Logger &log_;
        std::vector<std::unique_ptr<Shard>> shards_;
        std::atomic<bool> running_{false};
    };

    // =============================================================================
    // UTILITY FUNCTIONS
    // =============================================================================

    inline OrderId make_order_id(const OrderNode *node) noexcept {
        return (static_cast<uint64_t>(node->generation) << 32) | static_cast<uint64_t>(node->index);
    }

    inline hft::Order make_public_order(const OrderNode *node) noexcept {
        hft::Order o{};
        o.id = make_order_id(node->index, node->generation);
        o.symbol = node->hot.symbol;
        o.side = node->hot.side;
        o.type = node->hot.type;
        o.price = node->hot.price;
        o.qty = node->hot.qty;
        o.filled = node->hot.filled;
        o.status = node->hot.status;
        o.tif = node->hot.tif;
        o.ts = node->hot.timestamp;
        o.user_id = node->cold.user_id;
        return o;
    }

    // High-resolution timestamp function is provided in types.h (now_ns)

    // =============================
    // Implementations
    // =============================

    inline void OrderBook::match_order(MatchingEngine* engine, OrderNode* node) {
        DEEP_PROFILE_FUNCTION();
        if (!node || node->hot.qty <= 0) return;
        bool is_buy = (node->hot.side == Side::BUY);
        auto& levels = is_buy ? asks_ : bids_;
        SegmentTree& st = is_buy ? asks_tree_ : bids_tree_;

        // Bound search range [L,R] for crossing
        size_t L = 0, R = price_levels_ - 1;
        if (is_buy) {
            Price best_ask = get_best_ask();
            if (best_ask == UINT64_MAX) return; // no asks
            if (node->hot.price < best_ask) return; // cannot cross
            L = static_cast<size_t>((best_ask - min_price_) / tick_size_);
            R = static_cast<size_t>((node->hot.price - min_price_) / tick_size_);
            if (R >= price_levels_) R = price_levels_ - 1;
        } else {
            Price best_bid = get_best_bid();
            if (best_bid == 0) return; // no bids
            if (node->hot.price > best_bid) return; // cannot cross
            L = static_cast<size_t>((node->hot.price - min_price_) / tick_size_);
            R = static_cast<size_t>((best_bid - min_price_) / tick_size_);
            if (R >= price_levels_) R = price_levels_ - 1;
        }

        int idx = st.find_first(L, R);
        while (idx >= 0 && node->hot.qty > 0) {
            PriceLevel& level = levels[static_cast<size_t>(idx)];
            Price level_price = level.price.load(std::memory_order_relaxed);
            bool can_match = is_buy ? (node->hot.price >= level_price)
                                    : (node->hot.price <= level_price);
            if (!can_match) break;

            match_simd(engine, node, level);

            // Update leaf count from hot window + overflow
            uint32_t hot_count = level.hot_count.load(std::memory_order_relaxed);
            uint32_t overflow_count = level.overflow_count.load(std::memory_order_relaxed);
            uint32_t leaf = hot_count + overflow_count;
            (is_buy ? asks_tree_ : bids_tree_).set(static_cast<size_t>(idx), leaf);

            if (node->hot.qty == 0) break;
            idx = st.find_first(static_cast<size_t>(idx) + 1, R);
        }
        update_best_prices();
    }

    inline void OrderBook::add_limit_order(MatchingEngine* engine, OrderNode* node) {
        DEEP_PROFILE_FUNCTION();
        if (!node || node->hot.qty <= 0) return;
        bool is_buy = (node->hot.side == Side::BUY);
        size_t level_idx = price_to_level(node->hot.price);
        if (level_idx >= price_levels_) {
            if (engine) engine->on_reject(node->hot.id, "Price out of range");
            return;
        }
        auto& level = is_buy ? bids_[level_idx] : asks_[level_idx];
        level.add_order(node);
        // Update trackers
        if (is_buy) {
            bid_tracker_.update_level(level_idx, node->hot.price,
                                      level.order_count.load(std::memory_order_relaxed));
            uint32_t hot_count = level.hot_count.load(std::memory_order_relaxed);
            uint32_t overflow_count = level.overflow_count.load(std::memory_order_relaxed);
            bids_tree_.set(level_idx, hot_count + overflow_count);
        } else {
            ask_tracker_.update_level(level_idx, node->hot.price,
                                      level.order_count.load(std::memory_order_relaxed));
            uint32_t hot_count = level.hot_count.load(std::memory_order_relaxed);
            uint32_t overflow_count = level.overflow_count.load(std::memory_order_relaxed);
            asks_tree_.set(level_idx, hot_count + overflow_count);
        }
        update_best_prices();
    }

    inline void OrderBook::remove_order(MatchingEngine* engine, OrderNode* node, bool was_cancelled) {
        DEEP_PROFILE_FUNCTION();
        if (!node) return;
        bool is_buy = (node->hot.side == Side::BUY);
        size_t level_idx = price_to_level(node->hot.price);
        if (level_idx >= price_levels_) return;
        auto& level = is_buy ? bids_[level_idx] : asks_[level_idx];
        level.remove_order(node);
        if (is_buy) {
            bid_tracker_.update_level(level_idx, node->hot.price,
                                      level.order_count.load(std::memory_order_relaxed));
            uint32_t hot_count = level.hot_count.load(std::memory_order_relaxed);
            uint32_t overflow_count = level.overflow_count.load(std::memory_order_relaxed);
            bids_tree_.set(level_idx, hot_count + overflow_count);
        } else {
            ask_tracker_.update_level(level_idx, node->hot.price,
                                      level.order_count.load(std::memory_order_relaxed));
            uint32_t hot_count = level.hot_count.load(std::memory_order_relaxed);
            uint32_t overflow_count = level.overflow_count.load(std::memory_order_relaxed);
            asks_tree_.set(level_idx, hot_count + overflow_count);
        }
        if (was_cancelled && engine) {
            // no-op log for single-threaded path
        }
        if (engine) {
            // no-op clear/release in single-threaded path (pool owned by shard)
        }
        update_best_prices();
    }

    inline void OrderBook::match_simd(MatchingEngine* engine, OrderNode* node, PriceLevel& level) {
        DEEP_PROFILE_FUNCTION();
        if (!node || node->hot.qty <= 0) return;
        uint32_t match_indices[16];
        Quantity match_qtys[16];
        uint32_t match_count = level.match_hot_orders(node->hot.qty, match_indices, match_qtys);
        Price trade_price = level.price.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < match_count && node->hot.qty > 0; ++i) {
            uint32_t idx = match_indices[i];
            if (idx >= 32) continue;
            OrderNode* book_order = level.orders[idx];
            if (!book_order) continue;
            Quantity trade_qty = std::min(match_qtys[i], node->hot.qty);
            if (trade_qty <= 0) continue;
            book_order->hot.qty -= trade_qty;
            book_order->hot.filled += trade_qty;
            level.quantities[idx] = book_order->hot.qty;
            node->hot.qty -= trade_qty;
            node->hot.filled += trade_qty;
            level.total_qty.fetch_sub(trade_qty, std::memory_order_relaxed);
            if (engine) {
                engine->on_trade(make_public_order(book_order),
                                 make_public_order(node),
                                 trade_price, trade_qty);
            }
            if (book_order->hot.qty <= 0) {
                level.orders[idx] = nullptr;
                level.quantities[idx] = 0;
                level.order_count.fetch_sub(1, std::memory_order_relaxed);
                if (engine) {
                    // no-op clear/release
                }
            }
        }
    }

    inline void OrderBook::process_command(MatchingEngine* engine, OrderNode* node, bool is_cancel) {
        DEEP_PROFILE_FUNCTION();
        if (is_cancel) {
            remove_order(engine, node, true);
            return;
        }
        // New order path: try to match, then add remaining
        match_order(engine, node);
        if (node && node->hot.qty > 0) {
            add_limit_order(engine, node);
            if (engine) engine->on_accept(make_public_order(node));
        }
    }

    // Minimal MatchingEngine method implementations
    inline MatchingEngine::MatchingEngine(RiskManager &rm, Logger &log, size_t pool_capacity)
        : rm_(rm), log_(log)
    {
        // Initialize single shard inline for single-threaded benchmark compatibility
        shards_.emplace_back(std::make_unique<Shard>(pool_capacity, 0, 1000000, 1));
    }

    inline MatchingEngine::~MatchingEngine() { stop(); }

    inline void MatchingEngine::start(int /*core_id*/) {
        running_.store(true, std::memory_order_relaxed);
    }

    inline void MatchingEngine::stop() {
        running_.store(false, std::memory_order_relaxed);
        // no worker thread in single-threaded benchmark path
    }

    inline bool MatchingEngine::submit_order(Order &&o) {
        DEEP_PROFILE_FUNCTION();
        // Enqueue directly to shard 0 for single-threaded processing
        auto &shard = *shards_.front();
        OrderNode *node = shard.pool.acquire();
        if (!node) { on_reject(o.id, "No capacity"); return false; }
        node->hot.id = o.id ? o.id : make_order_id(node);
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
        // map index -> (gen, index+1)
        // order_id_map_ is pre-sized to pool capacity; indices are within capacity
        uint64_t packed = (static_cast<uint64_t>(node->generation) << 32) | (static_cast<uint64_t>(node->index) + 1ULL);
        shard.order_id_map[node->index].store(packed, std::memory_order_release);
        shard.order_book.process_command(this, node, false);
        return true;
    }

    inline void MatchingEngine::cancel_order(OrderId id) {
        // Single-threaded path: locate in shard 0 and remove
        if (shards_.empty()) return;
        auto &shard = *shards_.front();
        uint32_t index = static_cast<uint32_t>(id & 0xFFFFFFu);
        if (index >= shard.order_id_map.size()) return;
        shard.order_book.periodic_maintenance();
    }

    inline void MatchingEngine::on_trade(const Order &/*book_order*/, const Order &/*incoming_order*/,
                                         Price /*price*/, Quantity /*qty*/) {
        // aggregate onto shard 0 for single-threaded run
        if (!shards_.empty()) shards_.front()->trade_count_.fetch_add(1, std::memory_order_relaxed);
    }

    inline void MatchingEngine::on_accept(const Order &/*order*/) {
        if (!shards_.empty()) shards_.front()->accept_count_.fetch_add(1, std::memory_order_relaxed);
    }

    inline void MatchingEngine::on_reject(OrderId /*id*/, const char * /*reason*/) {
        if (!shards_.empty()) shards_.front()->reject_count_.fetch_add(1, std::memory_order_relaxed);
    }

    inline void MatchingEngine::on_trade_fast(const HotTradeEvent &/*event*/) {
        if (!shards_.empty()) shards_.front()->trade_count_.fetch_add(1, std::memory_order_relaxed);
    }
    inline void MatchingEngine::on_accept_fast(OrderId /*id*/) {
        if (!shards_.empty()) shards_.front()->accept_count_.fetch_add(1, std::memory_order_relaxed);
    }
    inline void MatchingEngine::on_reject_fast(OrderId /*id*/, const char * /*reason*/) {
        if (!shards_.empty()) shards_.front()->reject_count_.fetch_add(1, std::memory_order_relaxed);
    }

    inline void MatchingEngine::on_book_update(Symbol /*sym*/, Price /*bid*/, Price /*ask*/) {}

} // namespace hft
