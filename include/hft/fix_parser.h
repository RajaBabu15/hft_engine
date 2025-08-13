#pragma once

#include "hft/order.h"
#include <string>

namespace hft {
    class FIXParser {
        public:
            Order parse_order(const std::string &fix_msg) {
                Order o;
                o.init(1, 1, Side::BUY, OrderType::LIMIT, 50000, 1);
                return o;
            }
    };
} 