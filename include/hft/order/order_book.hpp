#pragma once
#include "hft/order/order.hpp"
#include "hft/order/price_level.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <utility>
namespace hft {
namespace order {
class OrderBook {
private:
    core::Symbol symbol_;
    std::map<core::Price, PriceLevel, std::greater<core::Price>> bid_levels_;
    std::map<core::Price, PriceLevel, std::less<core::Price>> ask_levels_;
    std::unordered_map<core::OrderID, Order> orders_;
    mutable core::Price best_bid_;
    mutable core::Price best_ask_;
    mutable bool best_prices_valid_;
    void update_best_prices() const;
public:
    explicit OrderBook(const core::Symbol& symbol);
    bool add_order(const Order& order);
    bool cancel_order(core::OrderID order_id);
    core::Price get_best_bid() const;
    core::Price get_best_ask() const;
    core::Price get_mid_price();
    core::Quantity get_bid_quantity(core::Price price) const;
    core::Quantity get_ask_quantity(core::Price price) const;
    const core::Symbol& get_symbol() const;
    std::vector<std::pair<core::Price, core::Quantity>> get_bids(size_t depth = 10) const;
    std::vector<std::pair<core::Price, core::Quantity>> get_asks(size_t depth = 10) const;
    std::vector<Order> get_orders_at_price_level(core::Price price, core::Side side) const;
    std::vector<Order> get_orders_at_price(core::Price price, core::Side side) const;
    std::vector<Order> get_all_buys() const;
    std::vector<Order> get_all_sells() const;
    bool fill_order(core::OrderID order_id, core::Quantity quantity);
    bool has_order(core::OrderID order_id) const;
    Order get_order(core::OrderID order_id) const;
};
}
}