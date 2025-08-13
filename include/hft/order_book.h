// file: order_book.h
#pragma once

#include "hft/types.h"
#include "hft/order.h"
#include "hft/order_node.h"
#include "hft/single_consumer_pool.h"
#include "hft/price_tracker.h"
#include "hft/matching_engine_types.h"
#include "hft/segment_tree.h"
#include "hft/deep_profiler.h"

#include <vector>

namespace hft {

    class MatchingEngine; // forward
    Order make_public_order(const OrderNode *node) noexcept;
    void engine_on_trade(MatchingEngine* engine, const Order &book_order, const Order &incoming_order, Price price, Quantity qty) noexcept;
    void engine_on_accept(MatchingEngine* engine, const Order &order) noexcept;
    void engine_on_reject(MatchingEngine* engine, OrderId id, const char *reason) noexcept;

    using BidTracker = PriceTracker<true>;
    using AskTracker = PriceTracker<false>;

    class OrderBook {
        private:
            const Price min_price_;
            const Price tick_size_;
            const size_t price_levels_;
            std::vector<PriceLevel> bids_;
            std::vector<PriceLevel> asks_;
            BidTracker bid_tracker_;
            AskTracker ask_tracker_;
            SingleConsumerPool& pool_;
            SegmentTree bids_tree_;
            SegmentTree asks_tree_;

            // All OrderBook logic implemented inline in the class per your request
            void match_order(MatchingEngine* engine, OrderNode* node) {
                DEEP_PROFILE_FUNCTION();
                if (!node || node->hot.qty <= 0) return;
                bool is_buy = (node->hot.side == Side::BUY);
                auto& levels = is_buy ? asks_ : bids_;
                SegmentTree& st = is_buy ? asks_tree_ : bids_tree_;

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

                DEEP_PROFILE_LINE("OrderBook.match.boundary_compute");
                int idx = st.find_first(L, R);
                while (idx >= 0 && node->hot.qty > 0) {
                    PriceLevel& level = levels[static_cast<size_t>(idx)];
                    Price level_price = level.price.load(std::memory_order_relaxed);
                    bool can_match = is_buy ? (node->hot.price >= level_price)
                                            : (node->hot.price <= level_price);
                    if (!can_match) break;

                    {
                        DEEP_PROFILE_SCOPE("OrderBook.match.match_simd");
                        match_simd(engine, node, level);
                    }
                    {
                        DEEP_PROFILE_SCOPE("OrderBook.match.tree_update");
                        uint32_t hot_count = level.hot_count.load(std::memory_order_relaxed);
                        uint32_t overflow_count = level.overflow_count.load(std::memory_order_relaxed);
                        uint32_t leaf = hot_count + overflow_count;
                        (is_buy ? asks_tree_ : bids_tree_).set(static_cast<size_t>(idx), leaf);
                    }

                    if (node->hot.qty == 0) break;
                    idx = st.find_first(static_cast<size_t>(idx) + 1, R);
                }
                update_best_prices();
            }

            void add_limit_order(MatchingEngine* engine, OrderNode* node) {
                DEEP_PROFILE_FUNCTION();
                if (!node || node->hot.qty <= 0) return;
                bool is_buy = (node->hot.side == Side::BUY);
                size_t level_idx = price_to_level(node->hot.price);
                if (level_idx >= price_levels_) {
                    engine_on_reject(engine, node->hot.id, "Price out of range");
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

            void remove_order(MatchingEngine* engine, OrderNode* node, bool was_cancelled) {
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

            void update_best_prices() noexcept {
                // For now, trackers hold best prices; no-op here unless you want to publish book updates
            }

            void match_simd(MatchingEngine* engine, OrderNode* node, PriceLevel& level) {
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
                    engine_on_trade(engine,
                                    make_public_order(book_order),
                                    make_public_order(node),
                                    trade_price, trade_qty);
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

            size_t price_to_level(Price price) const noexcept { 
                return static_cast<size_t>((price - min_price_) / tick_size_); 
            }

        public:
            OrderBook(SingleConsumerPool &pool, Price min_price, Price max_price, Price tick_size)
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

            void process_command(MatchingEngine* engine, OrderNode* node, bool is_cancel) {
                DEEP_PROFILE_FUNCTION();
                if (is_cancel) {
                    remove_order(engine, node, true);
                    return;
                }
                // New order path: try to match, then add remaining
                match_order(engine, node);
                if (node && node->hot.qty > 0) {
                    add_limit_order(engine, node);
                    engine_on_accept(engine, make_public_order(node));
                }
            }

            void periodic_maintenance() noexcept {
                for (auto& level : bids_) level.compact_if_needed();
                for (auto& level : asks_) level.compact_if_needed();
            }

            inline Price get_best_bid() const noexcept { return bid_tracker_.get_best_price(); }
            inline Price get_best_ask() const noexcept { return ask_tracker_.get_best_price(); }
    };

} // namespace hft


