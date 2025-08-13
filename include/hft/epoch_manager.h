#pragma once

#include "hft/types.h"
#include <atomic>
#include <array>
#include <vector>
#include <algorithm>

namespace hft {

    struct OrderNode; // forward decl, defined elsewhere

    struct DeferredNode {
        OrderNode *node;
        uint64_t generation;
    };

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

        void defer_reclaim(OrderNode *node, uint64_t generation) {
            thread_local std::vector<DeferredNode> &list = deferred_list();
            list.push_back(DeferredNode{node, generation});
            if (list.size() > 1000) {
                try_reclaim();
            }
        }

        void try_reclaim() {
            const uint64_t min_epoch = get_min_epoch();
            thread_local std::vector<DeferredNode> &list = deferred_list();

            auto it = std::partition(list.begin(), list.end(), [&](const DeferredNode &item) {
                return item.generation > min_epoch;
            });

            for (auto p = list.begin(); p != it; ++p) {
                reclaim_node(p->node);
            }
            list.erase(list.begin(), it);
        }

    private:
        int register_thread() {
            static std::atomic<int> counter{0};
            return counter.fetch_add(1, std::memory_order_relaxed);
        }

        std::vector<DeferredNode> &deferred_list() {
            thread_local std::vector<DeferredNode> list;
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

        void reclaim_node(OrderNode * /*node*/) {}

        static constexpr uint64_t INVALID_EPOCH = ~0ull;
        alignas(64) std::atomic<uint64_t> global_epoch_{0};
        alignas(64) std::array<std::atomic<uint64_t>, MAX_THREADS> current_epochs_{};
    };

} // namespace hft


