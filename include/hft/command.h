#pragma once

#include "hft/types.h"
#include "hft/order.h"
#include <vector>

namespace hft {
    // Updated command types for sharded matching engine
    enum class CommandType : uint8_t {
        NEW_ORDER,
        CANCEL_ORDER,
        MARKET_DATA
    };

    struct Level {
        Price price;
        Quantity qty;
    };

    struct Command {
        CommandType type;
        
        // For new orders: move-in Order (POD, efficient to move)
        hft::Order order;          // moved into queue for NEW_ORDER
        
        // For cancel: order id
        OrderId order_id;
        
        // Market data fields (if needed)
        Timestamp ts;
        std::vector<Level> bids_;
        std::vector<Level> asks_;
        
        // Constructors
        Command() = default;
        
        static Command make_new_order(hft::Order &&o) {
            Command c;
            c.type = CommandType::NEW_ORDER;
            c.order = std::move(o);
            return c;
        }
        
        static Command make_cancel(OrderId id) {
            Command c;
            c.type = CommandType::CANCEL_ORDER;
            c.order_id = id;
            return c;
        }
    };
}
