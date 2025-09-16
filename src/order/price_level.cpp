#include "hft/order/price_level.hpp"

namespace hft {
namespace order {

PriceLevel::PriceLevel(core::Price p) : price(p), total_quantity(0) {}

void PriceLevel::add_order(core::OrderID order_id, core::Quantity quantity) {
    order_queue.push_back(order_id);
    total_quantity += quantity;
}

void PriceLevel::remove_order(core::OrderID order_id, core::Quantity quantity) {
    auto it = std::find(order_queue.begin(), order_queue.end(), order_id);
    if (it != order_queue.end()) {
        order_queue.erase(it);
        total_quantity -= quantity;
    }
}

bool PriceLevel::empty() const {
    return order_queue.empty();
}

core::OrderID PriceLevel::front_order() const {
    return empty() ? 0 : order_queue.front();
}

} // namespace order
} // namespace hft