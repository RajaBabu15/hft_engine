#pragma once

#include "hft/types.h"
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
};