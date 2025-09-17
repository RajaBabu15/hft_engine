#pragma once
#include "hft/order/order.hpp"
#include "hft/order/price_level.hpp"
#include "hft/core/memory_optimized_segment_tree.hpp"
#include <map>
#include <unordered_map>
#include <vector>
#include <utility>
namespace hft {
namespace order {
class OrderBook {
private:
    core::Symbol symbol_;
    
    // Legacy std::map implementation
    std::map<core::Price, PriceLevel, std::greater<core::Price>> bid_levels_;
    std::map<core::Price, PriceLevel, std::less<core::Price>> ask_levels_;
    
    // High-performance segment tree implementation
    std::unique_ptr<core::MemoryOptimizedSegmentTree<PriceLevel>> bid_segment_tree_;
    std::unique_ptr<core::MemoryOptimizedSegmentTree<PriceLevel>> ask_segment_tree_;
    
    std::unordered_map<core::OrderID, Order> orders_;
    mutable core::Price best_bid_;
    mutable core::Price best_ask_;
    mutable bool best_prices_valid_;
    
    // Configuration flag for implementation choice
    bool use_segment_tree_;
    
    void update_best_prices() const;
    void update_best_prices_segment_tree() const;
public:
    explicit OrderBook(const core::Symbol& symbol, bool use_segment_tree = true);
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
