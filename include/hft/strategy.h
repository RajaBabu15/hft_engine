
#pragma once

#include "hft/order.h"
#include <vector>

namespace hft {

    class MarketMakingStrategy {
    private:
        Symbol symbol_;
        uint64_t order_id_counter_ = 0;
        Price last_best_bid_ = 0;
        Price last_best_ask_ = 0;

    public:
        MarketMakingStrategy(Symbol symbol) : symbol_(symbol) {}

        std::vector<Order> on_book_update(Price best_bid, Price best_ask) {
            std::vector<Order> orders;
            if (best_bid == 0 || best_ask == 0) return orders; // Wait for a valid market

            // Avoid reacting to the same market state
            if (best_bid == last_best_bid_ && best_ask == last_best_ask_) {
                return orders;
            }

            last_best_bid_ = best_bid;
            last_best_ask_ = best_ask;

            // Simple strategy: place a buy and a sell order with a small spread
            Price new_bid = best_bid + 1;
            Price new_ask = best_ask - 1;

            if (new_bid >= new_ask) return orders; // Avoid crossing the spread

            Order buy_order{};
            buy_order.id = ++order_id_counter_;
            buy_order.symbol = symbol_;
            buy_order.side = Side::BUY;
            buy_order.type = OrderType::LIMIT;
            buy_order.qty = 10;
            buy_order.price = new_bid;
            orders.push_back(buy_order);

            Order sell_order{};
            sell_order.id = ++order_id_counter_;
            sell_order.symbol = symbol_;
            sell_order.side = Side::SELL;
            sell_order.type = OrderType::LIMIT;
            sell_order.qty = 10;
            sell_order.price = new_ask;
            orders.push_back(sell_order);

            return orders;
        }
    };

}
