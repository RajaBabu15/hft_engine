#include "hft/order/order.hpp"
#include "hft/order/order_book.hpp"
#include "hft/matching/matching_engine.hpp"
#include "hft/core/clock.hpp"
#include <iostream>
#include <memory>
void run_complete_hft_demonstration() {
    std::cout << "📈 Starting HFT Engine Demo...\n";
    hft::core::calibrate_clock();
    auto order_book = std::make_unique<hft::order::OrderBook>("AAPL");
    hft::order::Order buy_order(1, "AAPL", hft::core::Side::BUY, hft::core::OrderType::LIMIT, 150.0, 100);
    hft::order::Order sell_order(2, "AAPL", hft::core::Side::SELL, hft::core::OrderType::LIMIT, 151.0, 50);
    std::cout << "📋 Adding orders to order book...\n";
    order_book->add_order(buy_order);
    order_book->add_order(sell_order);
    std::cout << "💰 Best Bid: $" << order_book->get_best_bid() << "\n";
    std::cout << "💰 Best Ask: $" << order_book->get_best_ask() << "\n";
    std::cout << "💰 Mid Price: $" << order_book->get_mid_price() << "\n";
    std::cout << "\n✅ HFT Engine Demo Complete!\n";
}
int main() {
    try {
        std::cout << "🚀 HFT Trading Engine v2.0.0\n";
        std::cout << "High-Frequency Trading Demo\n";
        std::cout << "==========================\n\n";
        run_complete_hft_demonstration();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
}