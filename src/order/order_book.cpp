#include "hft/order/order_book.hpp"
namespace hft {
namespace order {
OrderBook::OrderBook(const core::Symbol& symbol)
    : symbol_(symbol), best_bid_(0.0), best_ask_(0.0), best_prices_valid_(false) {}
void OrderBook::update_best_prices() const {
    best_bid_ = bid_levels_.empty() ? 0.0 : bid_levels_.begin()->first;
    best_ask_ = ask_levels_.empty() ? 0.0 : ask_levels_.begin()->first;
    best_prices_valid_ = true;
}
bool OrderBook::add_order(const Order& order) {
    orders_[order.id] = order;
    if (order.side == core::Side::BUY) {
        bid_levels_[order.price].add_order(order.id, order.quantity);
    } else {
        ask_levels_[order.price].add_order(order.id, order.quantity);
    }
    best_prices_valid_ = false;
    return true;
}
bool OrderBook::cancel_order(core::OrderID order_id) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;
    }
    const Order& order = it->second;
    core::Quantity remaining = order.remaining_quantity();
    if (order.side == core::Side::BUY) {
        auto level_it = bid_levels_.find(order.price);
        if (level_it != bid_levels_.end()) {
            level_it->second.remove_order(order_id, remaining);
            if (level_it->second.empty()) {
                bid_levels_.erase(level_it);
            }
        }
    } else {
        auto level_it = ask_levels_.find(order.price);
        if (level_it != ask_levels_.end()) {
            level_it->second.remove_order(order_id, remaining);
            if (level_it->second.empty()) {
                ask_levels_.erase(level_it);
            }
        }
    }
    orders_.erase(it);
    best_prices_valid_ = false;
    return true;
}
core::Price OrderBook::get_best_bid() const {
    if (!best_prices_valid_) {
        update_best_prices();
    }
    return best_bid_;
}
core::Price OrderBook::get_best_ask() const {
    if (!best_prices_valid_) {
        update_best_prices();
    }
    return best_ask_;
}
core::Price OrderBook::get_mid_price() {
    core::Price bid = get_best_bid();
    core::Price ask = get_best_ask();
    return (bid + ask) / 2.0;
}
core::Quantity OrderBook::get_bid_quantity(core::Price price) const {
    auto it = bid_levels_.find(price);
    return (it != bid_levels_.end()) ? it->second.total_quantity : 0;
}
core::Quantity OrderBook::get_ask_quantity(core::Price price) const {
    auto it = ask_levels_.find(price);
    return (it != ask_levels_.end()) ? it->second.total_quantity : 0;
}
const core::Symbol& OrderBook::get_symbol() const {
    return symbol_;
}
std::vector<std::pair<core::Price, core::Quantity>> OrderBook::get_bids(size_t depth) const {
    std::vector<std::pair<core::Price, core::Quantity>> result;
    size_t count = 0;
    for (const auto& level : bid_levels_) {
        if (count >= depth) break;
        result.emplace_back(level.first, level.second.total_quantity);
        ++count;
    }
    return result;
}
std::vector<std::pair<core::Price, core::Quantity>> OrderBook::get_asks(size_t depth) const {
    std::vector<std::pair<core::Price, core::Quantity>> result;
    size_t count = 0;
    for (const auto& level : ask_levels_) {
        if (count >= depth) break;
        result.emplace_back(level.first, level.second.total_quantity);
        ++count;
    }
    return result;
}
std::vector<Order> OrderBook::get_orders_at_price_level(core::Price price, core::Side side) const {
    return get_orders_at_price(price, side);
}
std::vector<Order> OrderBook::get_orders_at_price(core::Price price, core::Side side) const {
    std::vector<Order> result;
    if (side == core::Side::BUY) {
        auto level_it = bid_levels_.find(price);
        if (level_it != bid_levels_.end()) {
            for (const auto& order_entry : level_it->second.order_queue) {
                auto order_it = orders_.find(order_entry.order_id);
                if (order_it != orders_.end()) {
                    result.push_back(order_it->second);
                }
            }
        }
    } else {
        auto level_it = ask_levels_.find(price);
        if (level_it != ask_levels_.end()) {
            for (const auto& order_entry : level_it->second.order_queue) {
                auto order_it = orders_.find(order_entry.order_id);
                if (order_it != orders_.end()) {
                    result.push_back(order_it->second);
                }
            }
        }
    }
    return result;
}
std::vector<Order> OrderBook::get_all_buys() const {
    std::vector<Order> result;
    for (const auto& level : bid_levels_) {
        for (const auto& order_entry : level.second.order_queue) {
            auto order_it = orders_.find(order_entry.order_id);
            if (order_it != orders_.end()) {
                result.push_back(order_it->second);
            }
        }
    }
    return result;
}
std::vector<Order> OrderBook::get_all_sells() const {
    std::vector<Order> result;
    for (const auto& level : ask_levels_) {
        for (const auto& order_entry : level.second.order_queue) {
            auto order_it = orders_.find(order_entry.order_id);
            if (order_it != orders_.end()) {
                result.push_back(order_it->second);
            }
        }
    }
    return result;
}
bool OrderBook::fill_order(core::OrderID order_id, core::Quantity quantity) {
    auto it = orders_.find(order_id);
    if (it == orders_.end()) {
        return false;
    }
    Order& order = it->second;
    if (order.filled_quantity + quantity > order.quantity) {
        return false;
    }
    order.filled_quantity += quantity;
    if (order.side == core::Side::BUY) {
        auto level_it = bid_levels_.find(order.price);
        if (level_it != bid_levels_.end()) {
            level_it->second.reduce_quantity(order_id, quantity);
            if (order.remaining_quantity() == 0) {
                level_it->second.remove_order(order_id, 0);
            }
            if (level_it->second.empty()) {
                bid_levels_.erase(level_it);
            }
        }
    } else {
        auto level_it = ask_levels_.find(order.price);
        if (level_it != ask_levels_.end()) {
            level_it->second.reduce_quantity(order_id, quantity);
            if (order.remaining_quantity() == 0) {
                level_it->second.remove_order(order_id, 0);
            }
            if (level_it->second.empty()) {
                ask_levels_.erase(level_it);
            }
        }
    }
    if (order.remaining_quantity() == 0) {
        orders_.erase(it);
    }
    best_prices_valid_ = false;
    return true;
}
bool OrderBook::has_order(core::OrderID order_id) const {
    return orders_.find(order_id) != orders_.end();
}
Order OrderBook::get_order(core::OrderID order_id) const {
    auto it = orders_.find(order_id);
    if (it != orders_.end()) {
        return it->second;
    }
    return Order();
}
}
}
