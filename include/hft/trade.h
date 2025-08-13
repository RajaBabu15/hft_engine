#pragma once

#include "hft/types.h"

namespace hft {
    struct Trade {
        OrderId buy_order_id;
        OrderId sell_order_id;
        Symbol symbol;
        Quantity qty;
        Price price;
        Timestamp timestamp = now_ns();
        
        Trade() = default;
        
        Trade(OrderId buy_id, OrderId sell_id, Symbol sym, Quantity q, Price p)
            : buy_order_id(buy_id), sell_order_id(sell_id), symbol(sym), qty(q), price(p), timestamp(now_ns()) {}
    };
}
