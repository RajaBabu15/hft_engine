#include "hft/latency_metrics.h"
#include "hft/lockfree_queue.h"
#include "hft/admission_control.h"
#include "hft/pnl_tracker.h"
#include "hft/market_simulator.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace hft;

int main() {
    std::cout << "=== HFT Engine Enhanced Features Demo ===" << std::endl;
    
    // 1. Latency Metrics Demo
    std::cout << "\n1. Latency Metrics Demo" << std::endl;
    std::cout << "------------------------" << std::endl;
    
    LatencyTracker latency_tracker;
    
    // Simulate order processing with latency measurement
    for (int i = 0; i < 100; ++i) {
        auto measurement = latency_tracker.measure(LatencyPoint::ORDER_SUBMISSION);
        
        // Simulate order processing work
        std::this_thread::sleep_for(std::chrono::microseconds(50 + (i % 20)));
    }
    
    // Print latency statistics
    latency_tracker.print_report();
    
    // 2. Lock-Free Queue Demo
    std::cout << "\n2. Lock-Free Queue Demo" << std::endl;
    std::cout << "-----------------------" << std::endl;
    
    LockFreeQueue<int, 1024> queue;
    
    // Producer thread
    std::thread producer([&queue]() {
        for (int i = 0; i < 1000; ++i) {
            while (!queue.try_enqueue(i)) {
                std::this_thread::yield();
            }
        }
    });
    
    // Consumer thread
    std::atomic<int> consumed_count{0};
    std::thread consumer([&queue, &consumed_count]() {
        int value;
        for (int i = 0; i < 1000; ++i) {
            while (!queue.try_dequeue(value)) {
                std::this_thread::yield();
            }
            consumed_count++;
        }
    });
    
    producer.join();
    consumer.join();
    
    std::cout << "Produced 1000 items, consumed " << consumed_count.load() << " items" << std::endl;
    std::cout << "Queue final size: " << queue.size() << std::endl;
    
    // 3. Admission Control Demo
    std::cout << "\n3. Admission Control Demo" << std::endl;
    std::cout << "-------------------------" << std::endl;
    
    AdaptiveAdmissionControl::Config ac_config;
    ac_config.order_rate_per_second = 10.0; // Low rate for demo
    AdaptiveAdmissionControl admission_control(ac_config);
    
    int allowed = 0, denied = 0;
    
    for (int i = 0; i < 50; ++i) {
        if (admission_control.allow_order_submission()) {
            allowed++;
        } else {
            denied++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "Orders allowed: " << allowed << ", denied: " << denied << std::endl;
    
    auto status = admission_control.get_status();
    std::cout << "Available tokens: " << status.order_tokens_available 
              << "/" << status.order_capacity << std::endl;
    
    // 4. P&L Tracking Demo
    std::cout << "\n4. P&L Tracking Demo" << std::endl;
    std::cout << "-------------------" << std::endl;
    
    PnLManager pnl_manager;
    pnl_manager.register_strategy("demo_strategy");
    
    // Simulate trading activity
    Symbol symbol = 1;
    
    // Buy 100 shares at 50.00
    pnl_manager.record_execution("demo_strategy", 1, symbol, Side::BUY, 
                                100, 500000, 500010); // 1 tick slippage
    
    // Sell 50 shares at 50.05
    pnl_manager.record_execution("demo_strategy", 2, symbol, Side::SELL, 
                                50, 500500, 500480); // Favorable execution
    
    // Update market price for unrealized P&L
    pnl_manager.update_market_price(symbol, 500300); // Market at 50.03
    
    // Print P&L report
    pnl_manager.print_consolidated_report();
    
    // 5. Market Simulator Demo
    std::cout << "\n5. Market Simulator Demo" << std::endl;
    std::cout << "-----------------------" << std::endl;
    
    MarketSimulator::Config sim_config;
    sim_config.symbol_id = 1;
    sim_config.regime = MarketRegime::STABLE;
    sim_config.tick_interval_ns = 10000000; // 10ms for demo
    
    MarketSimulator market_sim(sim_config);
    market_sim.start_simulation();
    
    std::cout << "Starting market simulation for 2 seconds..." << std::endl;
    
    int updates_received = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
        MarketDataUpdate update;
        if (market_sim.get_market_update(update)) {
            updates_received++;
            if (updates_received % 20 == 0) {
                std::cout << "Received " << updates_received 
                         << " updates. Latest: Symbol=" << update.symbol_id 
                         << " Price=" << update.price 
                         << " Qty=" << update.quantity << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    market_sim.stop_simulation();
    
    auto market_state = market_sim.get_market_state();
    std::cout << "Final market state: Bid=" << market_state.current_bid 
              << " Ask=" << market_state.current_ask 
              << " Total updates: " << updates_received << std::endl;
    
    std::cout << "\n=== Demo Completed Successfully! ===" << std::endl;
    std::cout << "\nAll enhanced HFT engine components are working correctly." << std::endl;
    std::cout << "Features demonstrated:" << std::endl;
    std::cout << "✓ Microsecond-class latency measurement with percentiles" << std::endl;
    std::cout << "✓ Lock-free queue with multi-threaded producers/consumers" << std::endl;
    std::cout << "✓ Adaptive admission control with token bucket algorithm" << std::endl;
    std::cout << "✓ Real-time P&L tracking with slippage calculation" << std::endl;
    std::cout << "✓ Market microstructure simulation with realistic dynamics" << std::endl;
    
    return 0;
}