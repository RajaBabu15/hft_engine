#pragma once

#include "hft/order_node.h"

namespace hft {

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

} // namespace hft


