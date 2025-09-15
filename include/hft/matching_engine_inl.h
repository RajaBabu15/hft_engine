#pragma once

namespace hft {

    // ---- OrderBook inline implementations ----

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

    // ---- MatchingEngine inline implementations ----

    inline MatchingEngine::MatchingEngine(RiskManager &rm, Logger &log, size_t pool_capacity)
        : rm_(rm), log_(log)
    {
        shards_.emplace_back(std::make_unique<Shard>(pool_capacity, 0, 1000000, 1));
    }

    inline MatchingEngine::~MatchingEngine() { stop(); }

    inline void MatchingEngine::start(int /*core_id*/) {
        running_.store(true, std::memory_order_relaxed);
    }

    inline void MatchingEngine::stop() {
        running_.store(false, std::memory_order_relaxed);
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


