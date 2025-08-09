// Single translation unit merging core headers and implementations
// - Merged from: include/hft/types.h, include/hft/command.h, include/hft/order_book.h, include/hft/matching_engine.h
// - Implementations from: src/order_book.cpp, src/matching_engine.cpp

#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/lockfree/queue.hpp>

namespace hft {

// ===== types.h =====
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

enum class TimeInForce { GTC, IOC, FOK };

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
  Timestamp timestamp{};
};

struct Trade {
  OrderId order_id;
  Symbol symbol;
  Side side;
  Price price;
  Quantity quantity;
  Timestamp timestamp{};
};

// ===== command.h =====
enum class CommandType { MARKET_DATA };

struct Level {
  Price price;
  Quantity quantity;
};

struct alignas(64) Command {
  CommandType type;
  uint64_t timestamp_ns;
  std::vector<Level> bids;
  std::vector<Level> asks;
};

// ===== order_book.h/.cpp =====
class OrderBook {
public:
  void update(const Command &cmd) {
    bids_.clear();
    asks_.clear();
    for (const auto &level : cmd.bids) {
      bids_[level.price] = level.quantity;
    }
    for (const auto &level : cmd.asks) {
      asks_[level.price] = level.quantity;
    }
  }

  void print_book(int depth) const {
    auto bid_it = bids_.cbegin();
    auto ask_it = asks_.cbegin();

    std::cout << "\033[2J\033[1;1H"; // Clear screen
    std::cout << "--- ORDER BOOK ---" << std::endl;
    std::cout << "------------------------------------" << std::endl;
    std::cout << "|       BIDS       |       ASKS       |" << std::endl;
    std::cout << "| Price    | Qty     | Price    | Qty     |" << std::endl;
    std::cout << "------------------------------------" << std::endl;

    for (int i = 0; i < depth; ++i) {
      std::cout << "| ";
      if (bid_it != bids_.end()) {
        std::cout << std::fixed << std::setprecision(2) << std::setw(8)
                  << bid_it->first << " | " << std::setw(7)
                  << bid_it->second << " |";
        ++bid_it;
      } else {
        std::cout << std::setw(8) << " " << " | " << std::setw(7) << " "
                  << " |";
      }

      if (ask_it != asks_.end()) {
        std::cout << " " << std::fixed << std::setprecision(2) << std::setw(8)
                  << ask_it->first << " | " << std::setw(7)
                  << ask_it->second << " |";
        ++ask_it;
      } else {
        std::cout << " " << std::setw(8) << " " << " | " << std::setw(7)
                  << " " << " |";
      }
      std::cout << std::endl;
    }
    std::cout << "------------------------------------" << std::endl;
  }

private:
  std::map<Price, Quantity, std::greater<Price>> bids_;
  std::map<Price, Quantity> asks_;
};

// ===== matching_engine.h/.cpp =====
class MatchingEngine {
public:
  explicit MatchingEngine(int book_depth) : book_depth_(book_depth) {}
  ~MatchingEngine() { stop(); }

  bool post_command(Command &&cmd) {
    Command *heap_cmd = new Command(std::move(cmd));
    return command_queue_.push(heap_cmd);
  }

  void run() {
    running_ = true;
    writer_thread_ = std::thread(&MatchingEngine::writer_thread_func, this);
  }

  void stop() {
    running_ = false;
    if (writer_thread_.joinable()) {
      writer_thread_.join();
    }
  }

private:
  void writer_thread_func() {
    while (running_) {
      Command *cmdPtr = nullptr;
      if (command_queue_.pop(cmdPtr)) {
        order_book_.update(*cmdPtr);
        order_book_.print_book(book_depth_);
        delete cmdPtr;
      } else {
        std::this_thread::yield();
      }
    }
  }

  boost::lockfree::queue<Command *> command_queue_{1024 * 64};
  OrderBook order_book_;
  int book_depth_{};

  std::atomic<bool> running_{false};
  std::thread writer_thread_{};
};

} // namespace hft

// ===== Demo main (replaces src/main.cpp) =====
int main() {
  using namespace hft;

  std::cout << "\xF0\x9F\x9A\x80 HFT Engine Starting..." << std::endl;

  MatchingEngine engine(10);

  std::cout << "\xF0\x9F\x93\x8A Order Book initialized with depth: 10"
            << std::endl;

  engine.run();

  std::cout << "\xE2\x9C\x85 Matching Engine is running..." << std::endl;
  std::cout << "\xF0\x9F\x93\x88 Ready to process market data..."
            << std::endl;

  // Market data update 1
  Command md1{};
  md1.type = CommandType::MARKET_DATA;
  md1.timestamp_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count());
  md1.bids.push_back({50000.0, 1.5});
  md1.bids.push_back({49950.0, 2.0});
  md1.bids.push_back({49900.0, 3.0});
  md1.asks.push_back({50100.0, 2.0});
  md1.asks.push_back({50150.0, 1.5});
  md1.asks.push_back({50200.0, 2.5});
  engine.post_command(std::move(md1));
  std::cout << "Posted market data update 1" << std::endl;

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Market data update 2
  Command md2{};
  md2.type = CommandType::MARKET_DATA;
  md2.timestamp_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count());
  md2.bids.push_back({50050.0, 1.0});
  md2.bids.push_back({50000.0, 2.0});
  md2.bids.push_back({49950.0, 1.5});
  md2.asks.push_back({50100.0, 1.0});
  md2.asks.push_back({50150.0, 2.0});
  md2.asks.push_back({50200.0, 1.5});
  engine.post_command(std::move(md2));
  std::cout << "Posted market data update 2" << std::endl;

  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Market data update 3
  Command md3{};
  md3.type = CommandType::MARKET_DATA;
  md3.timestamp_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::high_resolution_clock::now().time_since_epoch())
          .count());
  md3.bids.push_back({50025.0, 2.5});
  md3.bids.push_back({50000.0, 1.0});
  md3.bids.push_back({49975.0, 3.0});
  md3.asks.push_back({50075.0, 1.5});
  md3.asks.push_back({50100.0, 2.0});
  md3.asks.push_back({50125.0, 1.0});
  engine.post_command(std::move(md3));
  std::cout << "Posted market data update 3" << std::endl;

  std::cout << "\nProcessing market data..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));

  std::cout << "\nDemo Complete!" << std::endl;
  engine.stop();
  std::cout << "Stopped gracefully" << std::endl;
  return 0;
}


