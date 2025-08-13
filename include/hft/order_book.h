#pragma once

#include "hft/types.h"
#include "hft/order_node.h"
#include "hft/index_pool.h"
#include "hft/price_tracker.h"
#include "hft/matching_engine_types.h"
#include "hft/segment_tree.h"
#include <vector>

namespace hft {

    class MatchingEngine; // forward decl

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
            for (auto& level : bids_) level.compact_if_needed();
            for (auto& level : asks_) level.compact_if_needed();
        }

        inline Price get_best_bid() const noexcept { return bid_tracker_.get_best_price(); }
        inline Price get_best_ask() const noexcept { return ask_tracker_.get_best_price(); }

    private:
        SegmentTree bids_tree_;
        SegmentTree asks_tree_;

        void match_order(MatchingEngine* engine, OrderNode* node);
        void add_limit_order(MatchingEngine* engine, OrderNode* node);
        void remove_order(MatchingEngine* engine, OrderNode* node, bool was_cancelled);
        void update_best_prices() noexcept {}
        void match_simd(MatchingEngine* engine, OrderNode* node, PriceLevel& level);
        size_t price_to_level(Price price) const noexcept { return static_cast<size_t>((price - min_price_) / tick_size_); }

        const Price min_price_;
        const Price tick_size_;
        const size_t price_levels_;
        std::vector<PriceLevel> bids_;
        std::vector<PriceLevel> asks_;
        BidTracker bid_tracker_;
        AskTracker ask_tracker_;
        IndexPool& pool_;
    };

} // namespace hft
