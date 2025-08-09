#pragma once
#include <cstdint>
#include <string>

namespace hft {
// Legacy types for backward compatibility
using LegacyPrice = int64_t;
using LegacyQuantity = uint64_t;
using OrderID = uint64_t;

// Convert floating point price to a fixed-point integer
inline LegacyPrice price_to_int(double p) {
  return static_cast<LegacyPrice>(p * 100.0);
}
inline double price_to_double(LegacyPrice p) {
  return static_cast<double>(p) / 100.0;
}
} // namespace hft
