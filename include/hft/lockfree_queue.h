#pragma once

#include "hft/types.h"
#include <atomic>
#include <memory>
#include <array>

// Forward declaration
namespace hft { struct Order; }

namespace hft {

// High-performance lock-free MPMC queue inspired by MoodyCamel's design
template<typename T, size_t Capacity = 8192>
class LockFreeQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
private:
    struct alignas(64) Slot {
        std::atomic<size_t> sequence;
        T data;
        
        Slot() : sequence(0) {}
    };
    
    alignas(64) std::array<Slot, Capacity> slots_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    
    static constexpr size_t MASK = Capacity - 1;

public:
    LockFreeQueue() {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }
    
    // Non-blocking enqueue
    bool try_enqueue(const T& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = slots_[head & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            
            if (seq == head) {
                if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed)) {
                    slot.data = item;
                    slot.sequence.store(head + 1, std::memory_order_release);
                    return true;
                }
            } else if (seq < head) {
                return false; // Queue is full
            } else {
                head = head_.load(std::memory_order_relaxed);
            }
        }
    }
    
    // Non-blocking enqueue with move semantics
    bool try_enqueue(T&& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = slots_[head & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            
            if (seq == head) {
                if (head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed)) {
                    slot.data = std::move(item);
                    slot.sequence.store(head + 1, std::memory_order_release);
                    return true;
                }
            } else if (seq < head) {
                return false; // Queue is full
            } else {
                head = head_.load(std::memory_order_relaxed);
            }
        }
    }
    
    // Non-blocking dequeue
    bool try_dequeue(T& item) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        
        for (;;) {
            Slot& slot = slots_[tail & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            
            if (seq == tail + 1) {
                if (tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed)) {
                    item = std::move(slot.data);
                    slot.sequence.store(tail + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (seq < tail + 1) {
                return false; // Queue is empty
            } else {
                tail = tail_.load(std::memory_order_relaxed);
            }
        }
    }
    
    // Batch enqueue for better throughput
    size_t try_enqueue_batch(const T* items, size_t count) noexcept {
        size_t enqueued = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!try_enqueue(items[i])) {
                break;
            }
            ++enqueued;
        }
        return enqueued;
    }
    
    // Batch dequeue for better throughput
    size_t try_dequeue_batch(T* items, size_t max_count) noexcept {
        size_t dequeued = 0;
        for (size_t i = 0; i < max_count; ++i) {
            if (!try_dequeue(items[i])) {
                break;
            }
            ++dequeued;
        }
        return dequeued;
    }
    
    // Check if queue is empty (approximate)
    bool empty() const noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);
        return head == tail;
    }
    
    // Get approximate queue size
    size_t size() const noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);
        return head - tail;
    }
    
    // Get capacity
    static constexpr size_t capacity() noexcept {
        return Capacity;
    }
};

// Specialized lock-free queue for orders
using OrderQueue = LockFreeQueue<hft::Order, 16384>;

// Lock-free message queue for FIX messages  
template<size_t MaxMessageSize = 1024>
struct FixMessage {
    alignas(64) char data[MaxMessageSize];
    size_t length;
    uint64_t timestamp;
    
    FixMessage() : length(0), timestamp(0) {}
};

using FixMessageQueue = LockFreeQueue<FixMessage<>, 8192>;

// Lock-free queue for market data updates
struct MarketDataUpdate {
    uint64_t symbol_id;
    uint64_t timestamp;
    int64_t price;
    int64_t quantity;
    uint8_t side; // 0 = bid, 1 = ask
    uint8_t update_type; // 0 = add, 1 = modify, 2 = delete
    
    MarketDataUpdate() : symbol_id(0), timestamp(0), price(0), quantity(0), side(0), update_type(0) {}
};

using MarketDataQueue = LockFreeQueue<MarketDataUpdate, 32768>;

} // namespace hft