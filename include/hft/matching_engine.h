#pragma once
#include "command.h"
#include "order_book.h"
#include <atomic>
#include <boost/lockfree/queue.hpp>
#include <thread>

namespace hft {
class MatchingEngine {
public:
  MatchingEngine(int book_depth);
  ~MatchingEngine();

  bool post_command(Command &&cmd);
  void run();
  void stop();

private:
  void writer_thread_func();

  boost::lockfree::queue<Command *> command_queue_{1024 * 64};
  OrderBook order_book_;
  int book_depth_;

  std::atomic<bool> running_{false};
  std::thread writer_thread_;
};
} // namespace hft