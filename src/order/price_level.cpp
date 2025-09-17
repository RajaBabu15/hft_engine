#include "hft/order/price_level.hpp"
namespace hft {
namespace order {
PriceLevel::PriceLevel(core::Price p) : price(p), total_quantity(0) {}
void PriceLevel::add_order(core::OrderID order_id, core::Quantity quantity) {
    order_queue.emplace_back(order_id, quantity);
    total_quantity += quantity;
}
void PriceLevel::remove_order(core::OrderID order_id, core::Quantity quantity) {
    auto it = std::find_if(order_queue.begin(), order_queue.end(),
                          [order_id](const OrderEntry& entry) { return entry.order_id == order_id; });
    if (it != order_queue.end()) {
        total_quantity -= it->quantity;
        order_queue.erase(it);
    }
}
void PriceLevel::reduce_quantity(core::OrderID order_id, core::Quantity quantity) {
    auto it = std::find_if(order_queue.begin(), order_queue.end(),
                          [order_id](const OrderEntry& entry) { return entry.order_id == order_id; });
    if (it != order_queue.end() && it->quantity >= quantity) {
        it->quantity -= quantity;
        total_quantity -= quantity;
    }
}
bool PriceLevel::empty() const {
    return order_queue.empty();
}
core::OrderID PriceLevel::front_order() const {
    return empty() ? 0 : order_queue.front().order_id;
}
}
}
