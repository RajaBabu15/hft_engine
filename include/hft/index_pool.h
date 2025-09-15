#pragma once

#include "hft/order_node.h"
#include "hft/platform.h"
#include <atomic>
#include <vector>

namespace hft {

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
        CACHE_ALIGNED std::atomic<int32_t> top_;
    };

} // namespace hft


