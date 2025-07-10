#pragma once
#include <cstdint>
#include <string>

namespace hft {
    using Price = int64_t;
    using Quantity = uint64_t;
    using OrderID = uint64_t;

    // Convert floating point price to a fixed-point integer
    inline Price price_to_int(double p) { return static_cast<Price>(p * 100.0); }
    inline double price_to_double(Price p) { return static_cast<double>(p) / 100.0; }
}