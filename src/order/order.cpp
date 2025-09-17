#include "hft/order/order.hpp"
namespace hft {
namespace order {
Order::Order() : id(0), side(core::Side::BUY), type(core::OrderType::LIMIT), price(0.0),
          quantity(0), filled_quantity(0), status(core::OrderStatus::PENDING),
          timestamp(core::HighResolutionClock::now()) {}
Order::Order(core::OrderID id_, const core::Symbol& symbol_, core::Side side_,
      core::OrderType type_, core::Price price_, core::Quantity quantity_)
    : id(id_), symbol(symbol_), side(side_), type(type_), price(price_),
      quantity(quantity_), filled_quantity(0), status(core::OrderStatus::PENDING),
      timestamp(core::HighResolutionClock::now()) {}
void Order::reset() {
    id = 0;
    symbol.clear();
    price = 0.0;
    quantity = 0;
    filled_quantity = 0;
    status = core::OrderStatus::PENDING;
    timestamp = core::HighResolutionClock::now();
}
core::Quantity Order::remaining_quantity() const {
    return quantity - filled_quantity;
}
bool Order::is_complete() const {
    return filled_quantity >= quantity;
}
}
}