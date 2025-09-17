#pragma once

#include "hft/order/order.hpp"
#include "hft/order/price_level.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <utility>

namespace hft {
namespace order {

// ============================================================================
// Order Book Implementation
// ============================================================================

class OrderBook {
private:
    core::Symbol symbol_;
    std::map<core::Price, PriceLevel, std::greater<core::Price>> bid_levels_; // Descending order
    std::map<core::Price, PriceLevel, std::less<core::Price>> ask_levels_;    // Ascending order
    std::unordered_map<core::OrderID, Order> orders_;
    
    // Cached best prices for O(1) access
    core::Price best_bid_;
    core::Price best_ask_;
    bool best_prices_valid_;
    
    void update_best_prices();
    
public:
    explicit OrderBook(const core::Symbol& symbol);
    
    bool add_order(const Order& order);
    bool cancel_order(core::OrderID order_id);
    
    core::Price get_best_bid();
    core::Price get_best_ask();
    core::Price get_mid_price();
    
    core::Quantity get_bid_quantity(core::Price price) const;
    core::Quantity get_ask_quantity(core::Price price) const;
    
    const core::Symbol& get_symbol() const;
    
    // Get market depth
    std::vector<std::pair<core::Price, core::Quantity>> get_bids(size_t depth = 10) const;
    std::vector<std::pair<core::Price, core::Quantity>> get_asks(size_t depth = 10) const;
    
    // Get orders at specific price level (for matching engine)
    std::vector<Order> get_orders_at_price_level(core::Price price, core::Side side) const;
    std::vector<Order> get_orders_at_price(core::Price price, core::Side side) const;
    
    // Get all orders for a side
    std::vector<Order> get_all_buys() const;
    std::vector<Order> get_all_sells() const;
    
    // Fill order partially
    bool fill_order(core::OrderID order_id, core::Quantity quantity);
    
    // Check if order exists
    bool has_order(core::OrderID order_id) const;
    Order get_order(core::OrderID order_id) const;
};

} // namespace order
} // namespace hft