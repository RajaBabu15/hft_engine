#pragma once

#include "hft/types.h"
#include "hft/platform.h"
#include <cstdint>

namespace hft {

    constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

    // Keep hot/cold parts compact and cache-friendly
    #pragma pack(push, 1)
    struct OrderHot {
        OrderId id;
        Price price;
        Quantity qty;
        Quantity filled;
        Timestamp timestamp;
        Symbol symbol;
        OrderStatus status;
        Side side;
        OrderType type;
        TimeInForce tif;
    };

    struct OrderCold {
        UserId user_id;
    };
    #pragma pack(pop)

    static_assert(sizeof(OrderHot) <= 64, "OrderHot should fit within cache-friendly size");

    // Unified order node structure
    struct CACHE_ALIGNED OrderNode {
        uint32_t index;
        uint32_t generation;
        OrderHot hot;
        OrderCold cold;
        uint32_t next_idx = INVALID_INDEX;
        uint32_t prev_idx = INVALID_INDEX;

        void reset() noexcept {
            hot.filled = 0;
            hot.status = OrderStatus::NEW;
            hot.timestamp = 0;
            next_idx = INVALID_INDEX;
            prev_idx = INVALID_INDEX;
        }
    };

} // namespace hft


