#pragma once
#include "types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hft {
enum class CommandType { MARKET_DATA };

struct Level {
  Price price;
  Quantity quantity;
};

// Padded to a cache line to prevent false sharing
struct alignas(64) Command {
  CommandType type;
  uint64_t timestamp_ns;
  std::vector<Level> bids;
  std::vector<Level> asks;
};
} // namespace hft
