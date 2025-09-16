#pragma once

#include "hft/types.h"
#include "hft/order_node.h"
#include "hft/single_consumer_pool.h"
#include "hft/queue.h"
#include "hft/order_book.h"
#include <atomic>
#include <memory>
#include <vector>

namespace hft {

    struct alignas(CACHE_LINE_SIZE) Shard {
        SingleConsumerPool pool;
        OrderBook order_book;
        std::vector<std::atomic<uint64_t>> order_id_map;
        Queue<hft::Command, 131072> queue;  // Lock-free MPSC queue

        std::thread worker;
        std::atomic<bool> running{false};
        int shard_id;

        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> trade_count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> accept_count_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> reject_count_{0};
        std::vector<HotTradeEvent> hot_trades_;

        Shard(size_t pool_capacity, Price min_price, Price max_price, Price tick_size)
            : pool(pool_capacity),
              order_book(pool, min_price, max_price, tick_size),
              order_id_map(pool_capacity) {
            hot_trades_.reserve(10000);
        }

        inline OrderId make_external_order_id(uint32_t index, uint32_t generation) const noexcept {
            return (static_cast<OrderId>(shard_id) << 56) |
                   (static_cast<OrderId>(generation) << 24) |
                   static_cast<OrderId>(index);
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

} // namespace hft


