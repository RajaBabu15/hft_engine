#pragma once

#include "hft/core/types.hpp"
#include <vector>
#include <algorithm>

namespace hft {
namespace order {

// ============================================================================
// Price Level Implementation
// ============================================================================

struct PriceLevel {
    core::Price price;
    core::Quantity total_quantity;
    std::vector<core::OrderID> order_queue; // Maintains time priority
    
    PriceLevel(core::Price p = 0.0);
    
    void add_order(core::OrderID order_id, core::Quantity quantity);
    void remove_order(core::OrderID order_id, core::Quantity quantity);
    bool empty() const;
    core::OrderID front_order() const;
};

} // namespace order
} // namespace hft