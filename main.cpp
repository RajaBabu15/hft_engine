#include "hft/logger.h"
#include "hft/risk_manager.h"
#include "hft/order.h"
#include "hft/matching_engine.h"
#include "hft/deep_profiler.h"
#include <iostream>
#include <chrono>
#include <random>
#include <iomanip>

using namespace hft;

int main(){
    // Redirect Redis output to suppress messages
    std::ios_base::sync_with_stdio(false);
    
    Logger log;
    RiskManager rm(100, 10'000'000'000ULL, 50'000'000ULL);
    MatchingEngine engine(rm, log, 1<<20);
    
    // Create initial market liquidity
    std::random_device rd;
    std::mt19937 gen(rd());
    
    for (int symbol = 1; symbol <= 5; ++symbol) {
        Price base_price = 100000 + (symbol * 1000);
        for (int level = 0; level < 3; ++level) {
            engine.submit_order(Order{0, static_cast<OrderId>(symbol * 1000 + level * 2), 
                                    static_cast<Symbol>(symbol), Side::BUY, OrderType::LIMIT, 
                                    base_price - (level + 1) * 50, 100 + level * 50, 0, 
                                    OrderStatus::NEW, TimeInForce::GTC, 1});
            engine.submit_order(Order{0, static_cast<OrderId>(symbol * 1000 + level * 2 + 1), 
                                    static_cast<Symbol>(symbol), Side::SELL, OrderType::LIMIT, 
                                    base_price + (level + 1) * 50, 100 + level * 50, 0, 
                                    OrderStatus::NEW, TimeInForce::GTC, 1});
        }
    }
    
    // Performance test
    std::uniform_int_distribution<> symbol_dist(1, 5);
    std::uniform_int_distribution<> side_dist(0, 1);
    std::uniform_int_distribution<> qty_dist(10, 100);
    std::uniform_int_distribution<> order_type_dist(0, 2);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Main test
    for (int i = 0; i < 25000; ++i) {
        Order order;
        order.id = 50000 + i;
        order.symbol = symbol_dist(gen);
        order.side = side_dist(gen) == 0 ? Side::BUY : Side::SELL;
        order.qty = qty_dist(gen);
        order.user_id = 2;
        order.status = OrderStatus::NEW;
        
        if (order_type_dist(gen) == 0) {
            order.type = OrderType::MARKET;
            order.price = 0;
            order.tif = TimeInForce::IOC;
        } else {
            order.type = OrderType::LIMIT;
            Price base_price = 100000 + (order.symbol * 1000);
            order.price = base_price + (order.side == Side::BUY ? -25 : 25);
            order.tif = TimeInForce::GTC;
        }
        
        engine.submit_order(std::move(order));
    }
    
    // Burst test
    for (int i = 0; i < 10000; ++i) {
        Order order;
        order.id = 100000 + i;
        order.symbol = (i % 5) + 1;
        order.side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        order.type = OrderType::MARKET;
        order.price = 0;
        order.qty = 25 + (i % 25);
        order.tif = TimeInForce::IOC;
        order.user_id = 5;
        order.status = OrderStatus::NEW;
        
        engine.submit_order(std::move(order));
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Results
    uint64_t total_orders = engine.accept_count() + engine.reject_count();
    uint64_t trades = engine.trade_count();
    double success_rate = total_orders > 0 ? (double)engine.accept_count() / total_orders * 100.0 : 0.0;
    double throughput = (35000.0 * 1000000.0) / duration.count();
    double latency = duration.count() / 35000.0;
    int64_t pnl = rm.get_realized_pnl();
    
    std::cout << "HFT ENGINE RESULTS" << std::endl;
    std::cout << "==================" << std::endl;
    std::cout << "Orders: " << total_orders << " | Success: " << std::fixed << std::setprecision(1) << success_rate << "% | Trades: " << trades << std::endl;
    std::cout << "Throughput: " << std::fixed << std::setprecision(0) << throughput << " orders/sec" << std::endl;
    std::cout << "Latency: " << std::fixed << std::setprecision(2) << latency << " μs average" << std::endl;
    std::cout << "P&L: $" << std::fixed << std::setprecision(0) << (pnl / 100.0) << std::endl;
    
    // Display advanced metrics
    int64_t advanced_pnl = engine.get_total_pnl_cents();
    double win_rate = engine.get_win_rate();
    uint64_t volume = engine.get_advanced_metrics().get_total_volume();
    double avg_slippage = engine.get_advanced_metrics().get_average_slippage_cents();
    
    std::cout << "Advanced P&L: $" << std::fixed << std::setprecision(0) << (advanced_pnl / 100.0) << std::endl;
    std::cout << "Win Rate: " << std::fixed << std::setprecision(1) << (win_rate * 100.0) << "%" << std::endl;
    std::cout << "Volume: " << volume << " shares | Avg Slippage: $" << std::setprecision(2) << (avg_slippage / 100.0) << std::endl;
    std::cout << std::endl;
    std::cout << "VALIDATION: " 
              << (throughput >= 100000 ? "✓ 100k+ req " : "✗ 100k+ req ")
              << (latency <= 10.0 ? "✓ μs latency " : "✗ μs latency ")
              << (success_rate >= 95.0 ? "✓ reliability" : "✗ reliability") << std::endl;
    
    return 0;
}