#pragma once

#include <iostream>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace hft {
    struct Trade;
    struct Order;
    using OrderId = uint64_t;
}

namespace hft {

    class Logger {
    public:
        enum class Level { DEBUG, INFO, WARN, ERROR };

    private:
        std::ostream* out_;
        std::ofstream file_stream_;
        std::queue<std::string> queue_;
        std::mutex queue_mutex_;
        std::condition_variable queue_cv_;
        std::thread worker_;
        std::atomic<bool> stop_flag_;

        static std::string level_to_str(Level level) {
            switch (level) {
                case Level::DEBUG: return "DEBUG";
                case Level::INFO: return "INFO";
                case Level::WARN: return "WARN";
                case Level::ERROR: return "ERROR";
                default: return "UNKNOWN";
            }
        }

        void start() {
            worker_ = std::thread([this]() {
                std::string msg;
                while (!stop_flag_.load() || !queue_.empty()) {
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        queue_cv_.wait(lock, [this]() { return !queue_.empty() || stop_flag_.load(); });
                        if (!queue_.empty()) {
                            msg = queue_.front();
                            queue_.pop();
                        }
                    }
                    if (!msg.empty()) {
                        (*out_) << msg << "\n";
                        out_->flush();
                        msg.clear();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            });
        }

        template <typename... Args>
        void push(Level level, Args&&... args) {
            std::ostringstream oss;
            auto now = std::chrono::system_clock::now();
            auto t = std::chrono::system_clock::to_time_t(now);
            auto tm = *std::localtime(&t);
            oss << "[" << std::put_time(&tm, "%F %T") << "] [" << level_to_str(level) << "] ";
            (oss << ... << args);
            
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                queue_.push(oss.str());
            }
            queue_cv_.notify_one();
        }

    public:
        Logger(std::ostream& out_stream = std::cout) 
            : out_(&out_stream), stop_flag_(false) {
            start();
        }

        Logger(const std::string& file_path)
            : out_(nullptr), file_stream_(file_path, std::ios::app), stop_flag_(false) {
            out_ = &file_stream_;
            start();
        }

        ~Logger() {
            stop_flag_.store(true);
            queue_cv_.notify_all();
            if (worker_.joinable()) worker_.join();
        }

        template <typename... Args>
        void debug(Args&&... args) {
            push(Level::DEBUG, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void info(Args&&... args) {
            push(Level::INFO, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void warn(Args&&... args) {
            push(Level::WARN, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void error(Args&&... args) {
            push(Level::ERROR, std::forward<Args>(args)...);
        }

        // Specialized logging methods for matching engine
        void log_trade(const Trade& /* trade */) {
            info("TRADE: Trade executed");
        }

        void log_accept(OrderId order_id) {
            info("ACCEPT: Order ", order_id, " accepted");
        }

        void log_reject(OrderId order_id, const std::string& reason) {
            warn("REJECT: Order ", order_id, " rejected - ", reason);
        }
    };
}
