#pragma once

#include "hft/types.h"
#include "hft/order_node.h"
#include "hft/deep_profiler.h"
#include "hft/platform.h"

#include <atomic>
#include <array>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace hft {

    struct SimdMatchResult {
        uint32_t indices[16];
        Quantity qtys[16];
        uint32_t count{0};
    };

    // Forward declaration for SIMD matcher implemented in matching_engine.h
    SimdMatchResult match_orders_simd(const OrderNode* const* orders,
                                      const Quantity* qtys,
                                      uint32_t order_count,
                                      Quantity incoming_qty) noexcept;

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
        
        inline bool add_order(OrderNode* node) noexcept {
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
        
        inline void remove_order(OrderNode* node) noexcept {
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
        
        inline void compact_if_needed() noexcept {
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
        inline uint32_t match_hot_orders(Quantity incoming_qty,
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

} // namespace hft


