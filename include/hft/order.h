#pragma once

#include "hft/types.h"
#include "hft/order_node.h"
#include <cstddef>
#include <cstdint>

namespace hft {
    struct alignas(64) Order {
        UserId user_id{0};  //8
        OrderId id{0}; //8
        Symbol symbol{0}; //4
        Side side{Side::BUY}; //1
        OrderType type{OrderType::LIMIT}; //1
        Price price{0}; //8(ticks)
        Quantity qty{0}; //8
        Quantity filled{0}; //8
        OrderStatus status{OrderStatus::NEW}; //1
        TimeInForce tif{TimeInForce::GTC};  //1
        Timestamp ts{now_ns()}; //8


        inline void init(OrderId oid,Symbol sym,Side s,OrderType ot,Price p,Quantity q,TimeInForce t=TimeInForce::GTC) noexcept {
            id=oid;
            symbol=sym;
            side=s;
            type=ot;
            price=p;
            qty=q;
            filled=0;
            status=OrderStatus::NEW;
            tif=t;
            ts=now_ns();
        }

        inline bool is_filled() const noexcept { return filled>=qty;}
    };

    static_assert(std::is_trivially_copyable<Order>::value,"Order must be is_trivially_copyable");

    // Convert internal OrderNode to public Order view
    inline Order make_public_order(const OrderNode *node) noexcept {
        Order o{};
        o.id = (static_cast<uint64_t>(node->generation) << 32) | static_cast<uint64_t>(node->index);
        o.symbol = node->hot.symbol;
        o.side = node->hot.side;
        o.type = node->hot.type;
        o.price = node->hot.price;
        o.qty = node->hot.qty;
        o.filled = node->hot.filled;
        o.status = node->hot.status;
        o.tif = node->hot.tif;
        o.ts = node->hot.timestamp;
        o.user_id = node->cold.user_id;
        return o;
    }
};