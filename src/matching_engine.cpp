#include "hft/matching_engine.h"
#include <chrono>
#include <x86intrin.h>

namespace hft {

MatchingEngine::MatchingEngine(int book_depth) : book_depth_(book_depth) {}

MatchingEngine::~MatchingEngine() { stop(); }

bool MatchingEngine::post_command(Command &&cmd) {
  Command *heap_cmd = new Command(std::move(cmd));
  return command_queue_.push(heap_cmd);
}

void MatchingEngine::run() {
  running_ = true;
  writer_thread_ = std::thread(&MatchingEngine::writer_thread_func, this);
}

void MatchingEngine::stop() {
  running_ = false;
  if (writer_thread_.joinable()) {
    writer_thread_.join();
  }
}

void MatchingEngine::writer_thread_func() {
  while (running_) {
    Command *cmdPtr = nullptr;
    if (command_queue_.pop(cmdPtr)) {
      order_book_.update(*cmdPtr);
      order_book_.print_book(book_depth_);
      delete cmdPtr;
    } else {
      _mm_pause();
    }
  }
}
} // namespace hft