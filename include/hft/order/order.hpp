#pragma once

#include "hft/core/types.hpp"
#include "hft/core/clock.hpp"

namespace hft {
namespace order {

// ============================================================================
// Order Structure
// ============================================================================

struct Order {
    core::OrderID id;
    core::Symbol symbol;
    core::Side side;
    core::OrderType type;
    core::Price price;
    core::Quantity quantity;
    core::Quantity filled_quantity;
    core::OrderStatus status;
    core::TimePoint timestamp;
    
    Order();
    Order(core::OrderID id_, const core::Symbol& symbol_, core::Side side_, 
          core::OrderType type_, core::Price price_, core::Quantity quantity_);
    
    void reset();
    core::Quantity remaining_quantity() const;
    bool is_complete() const;
};

} // namespace order
} // namespace hft