#pragma once
#include "command.h"
#include <map>
#include <vector>
#include <string>
#include <iostream>

namespace hft {
    class OrderBook {
    public:
        void update(const Command& cmd);
        void print_book(int depth) const;

    private:
        std::map<Price, Quantity, std::greater<Price>> bids_;
        std::map<Price, Quantity> asks_;
    };
}