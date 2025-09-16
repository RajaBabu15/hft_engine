#include "hft/core/types.hpp"
#include "hft/core/clock.hpp"
#include "hft/core/lock_free_queue.hpp"
#include "hft/core/object_pool.hpp"
#include "hft/order/order.hpp"
#include "hft/order/price_level.hpp"
#include "hft/order/order_book.hpp"

#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <random>

using namespace hft;

// Simple demonstration of the refactored components
class SimpleHftEngine {
private:
    std::atomic<bool> running_{false};
    std::vector<core::Symbol> symbols_;
    
public:
    SimpleHftEngine(const std::vector<core::Symbol>& symbols) : symbols_(symbols) {}
    
    void run() {
        running_ = true;
        std::cout << "HFT Trading Engine - Refactored Implementation" << std::endl;
        std::cout << "=============================================" << std::endl << std::endl;
        
        // Demonstrate core components
        demonstrate_clock();
        demonstrate_lock_free_queue();
        demonstrate_order_book();
        
        std::cout << "Engine demonstration complete!" << std::endl;
        running_ = false;
    }
    
private:
    void demonstrate_clock() {
        std::cout << "=== High Resolution Clock Test ===" << std::endl;
        auto start = core::HighResolutionClock::now();
        
        // Simulate some work
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        auto end = core::HighResolutionClock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        
        std::cout << "Clock precision test: " << duration.count() << " nanoseconds" << std::endl;
        std::cout << "RDTSC: " << core::HighResolutionClock::rdtsc() << std::endl << std::endl;
    }
    
    void demonstrate_lock_free_queue() {
        std::cout << "=== Lock-Free Queue Test ===" << std::endl;
        
        core::LockFreeQueue<int, 1024> queue;
        const int test_items = 1000;
        
        // Producer
        for (int i = 0; i < test_items; ++i) {
            if (!queue.push(i)) {
                std::cout << "Queue full at item " << i << std::endl;
                break;
            }
        }
        
        // Consumer
        int consumed = 0;
        int value;
        while (queue.pop(value)) {
            consumed++;
        }
        
        std::cout << "Produced and consumed " << consumed << " items" << std::endl << std::endl;
    }
    
    void demonstrate_order_book() {
        std::cout << "=== Order Book Test ===" << std::endl;
        
        order::OrderBook book("AAPL");
        
        // Add some orders
        order::Order buy_order(1, "AAPL", core::Side::BUY, core::OrderType::LIMIT, 150.00, 100);
        order::Order sell_order(2, "AAPL", core::Side::SELL, core::OrderType::LIMIT, 151.00, 100);
        
        book.add_order(buy_order);
        book.add_order(sell_order);
        
        std::cout << "Best Bid: " << book.get_best_bid() << std::endl;
        std::cout << "Best Ask: " << book.get_best_ask() << std::endl;
        std::cout << "Mid Price: " << book.get_mid_price() << std::endl;
        
        // Test market depth
        auto bids = book.get_bids(5);
        auto asks = book.get_asks(5);
        
        std::cout << "Market Depth:" << std::endl;
        std::cout << "Bids: ";
        for (const auto& bid : bids) {
            std::cout << "[" << bid.first << ":" << bid.second << "] ";
        }
        std::cout << std::endl;
        
        std::cout << "Asks: ";
        for (const auto& ask : asks) {
            std::cout << "[" << ask.first << ":" << ask.second << "] ";
        }
        std::cout << std::endl << std::endl;
    }
};

int main() {
    try {
        std::vector<core::Symbol> symbols = {"AAPL", "MSFT", "GOOGL"};
        SimpleHftEngine engine(symbols);
        engine.run();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}