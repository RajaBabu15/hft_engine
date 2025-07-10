#pragma once
#include <cstdint>
#include <string>

namespace hft {
using Price = double;
using Quantity = double;
using OrderId = std::string;
using Symbol = std::string;
using Timestamp = uint64_t;

enum class Side { BUY, SELL };

enum class OrderType { MARKET, LIMIT, STOP_LIMIT };

enum class OrderStatus {
  NEW,
  PARTIALLY_FILLED,
  FILLED,
  CANCELED,
  REJECTED,
  EXPIRED
};

enum class TimeInForce {
  GTC, // Good Till Canceled
  IOC, // Immediate or Cancel
  FOK  // Fill or Kill
};

struct Order {
  OrderId id;
  Symbol symbol;
  Side side;
  OrderType type;
  Price price;
  Quantity quantity;
  Quantity filled_quantity = 0.0;
  OrderStatus status = OrderStatus::NEW;
  TimeInForce time_in_force = TimeInForce::GTC;
  Timestamp timestamp;
};

struct Trade {
  OrderId order_id;
  Symbol symbol;
  Side side;
  Price price;
  Quantity quantity;
  Timestamp timestamp;
};
} // namespace hft
