#include "hft/matching/matching_engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    hft::matching::MatchingEngine engine;
    
    engine.set_execution_callback([](const hft::matching::ExecutionReport& report) {
        if (report.executed_quantity > 0) {
            std::cout << "Execution: " << report.executed_quantity 
                      << "@" << report.avg_executed_price << std::endl;
        }
    });
    
    engine.start();
    
    hft::order::Order buy_order(1, "AAPL", hft::core::Side::BUY, 
                                hft::core::OrderType::LIMIT, 150.00, 100);
    engine.submit_order(buy_order);
    
    hft::order::Order sell_order(2, "AAPL", hft::core::Side::SELL,
                                 hft::core::OrderType::LIMIT, 149.00, 50);
    engine.submit_order(sell_order);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    engine.stop();
    
    const auto& stats = engine.get_stats();
    std::cout << "Orders: " << stats.orders_processed << std::endl;
    std::cout << "Matched: " << stats.orders_matched << std::endl;
    
    return 0;
}
