#pragma once

#include <atomic>
#include "hft/platform.h"
#include <array>
#include <algorithm>

namespace hft {

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

        CACHE_ALIGNED std::atomic<size_t> head_;
        CACHE_ALIGNED std::atomic<size_t> tail_;
        std::array<T, Size> buffer_;
    };

} // namespace hft


