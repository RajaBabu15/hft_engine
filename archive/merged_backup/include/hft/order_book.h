#pragma once
#include "command.h"
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace hft {
class OrderBook {
public:
  void update(const Command &cmd);
  void print_book(int depth) const;

private:
  std::map<Price, Quantity, std::greater<Price>> bids_;
  std::map<Price, Quantity> asks_;
};
} // namespace hft