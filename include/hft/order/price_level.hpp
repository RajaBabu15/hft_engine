#pragma once
#include "hft/core/types.hpp"
#include <vector>
#include <algorithm>
namespace hft {
namespace order {
struct OrderEntry {
    core::OrderID order_id;
    core::Quantity quantity;
    OrderEntry(core::OrderID id, core::Quantity qty) : order_id(id), quantity(qty) {}
};
struct PriceLevel {
    core::Price price;
    core::Quantity total_quantity;
    std::vector<OrderEntry> order_queue;
    PriceLevel(core::Price p = 0.0);
    void add_order(core::OrderID order_id, core::Quantity quantity);
    void remove_order(core::OrderID order_id, core::Quantity quantity);
    void reduce_quantity(core::OrderID order_id, core::Quantity quantity);
    bool empty() const;
    core::OrderID front_order() const;
};
}
}