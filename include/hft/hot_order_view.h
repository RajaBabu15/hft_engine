#pragma once

#include "hft/types.h"

namespace hft {
    // Lightweight view for hot-path operations - no allocations
    struct HotOrderView {
        OrderId id;
        Price price;
        Quantity qty;
        UserId user_id;
        Symbol symbol;
        Side side;
        OrderType type;
        TimeInForce tif;
        
        // Constructor from OrderNode (will be defined later)
        HotOrderView() = default;
        
        // Fast constructor from hot fields
        HotOrderView(OrderId id, Price price, Quantity qty, UserId user_id, 
                    Symbol symbol, Side side, OrderType type, TimeInForce tif)
            : id(id), price(price), qty(qty), user_id(user_id),
              symbol(symbol), side(side), type(type), tif(tif) {}
    };
    
    // Lightweight trade event for deferred processing
    struct HotTradeEvent {
        OrderId book_order_id;
        OrderId incoming_order_id;
        Price price;
        Quantity qty;
        Timestamp timestamp;
        Symbol symbol;
        
        HotTradeEvent() = default;
        HotTradeEvent(OrderId book_id, OrderId incoming_id, Price p, Quantity q, Symbol s)
            : book_order_id(book_id), incoming_order_id(incoming_id), 
              price(p), qty(q), timestamp(now_ns()), symbol(s) {}
    };
}
