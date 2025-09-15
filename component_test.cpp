#include "hft/latency_metrics.h"
#include "hft/lockfree_queue.h"
#include "hft/admission_control.h"
#include "hft/pnl_tracker.h"
#include <iostream>
#include <chrono>
#include <thread>

using namespace hft;

int main() {
    std::cout << "Testing HFT Engine Enhanced Components..." << std::endl;
    
    // Test 1: Latency Metrics
    std::cout << "\n1. Testing Latency Metrics..." << std::endl;
    {
        LatencyTracker tracker;
        
        // Simulate some latency measurements
        for (int i = 0; i < 1000; ++i) {
            auto measurement = tracker.measure(LatencyPoint::ORDER_SUBMISSION);
            std::this_thread::sleep_for(std::chrono::microseconds(10 + (i % 100)));
        }
        
        auto stats = tracker.get_stats(LatencyPoint::ORDER_SUBMISSION);
        std::cout << "  Recorded " << stats.count << " measurements" << std::endl;
        std::cout << "  P50: " << stats.p50_ns / 1000.0 << " μs" << std::endl;
        std::cout << "  P90: " << stats.p90_ns / 1000.0 << " μs" << std::endl;
        std::cout << "  P99: " << stats.p99_ns / 1000.0 << " μs" << std::endl;
    }
    
    // Test 2: Lock-Free Queue
    std::cout << "\n2. Testing Lock-Free Queue..." << std::endl;
    {
        LockFreeQueue<int, 1024> queue;
        
        // Producer
        for (int i = 0; i < 500; ++i) {
            if (!queue.try_enqueue(i)) {
                std::cout << "  Queue full at " << i << std::endl;
                break;
            }
        }
        
        // Consumer
        int value;
        int consumed = 0;
        while (queue.try_dequeue(value)) {
            consumed++;
        }
        
        std::cout << "  Enqueued 500 items, consumed " << consumed << " items" << std::endl;
        std::cout << "  Queue size: " << queue.size() << std::endl;
    }
    
    // Test 3: Admission Control
    std::cout << "\n3. Testing Admission Control..." << std::endl;
    {
        AdaptiveAdmissionControl::Config config;
        config.order_rate_per_second = 100.0; // Low rate for testing
        AdaptiveAdmissionControl admission(config);
        
        int allowed = 0, denied = 0;
        
        for (int i = 0; i < 200; ++i) {
            if (admission.allow_order_submission()) {
                allowed++;
            } else {
                denied++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        
        std::cout << "  Orders allowed: " << allowed << ", denied: " << denied << std::endl;
        
        auto status = admission.get_status();
        std::cout << "  Available tokens: " << status.order_tokens_available 
                  << "/" << status.order_capacity << std::endl;
    }
    
    // Test 4: P&L Tracker
    std::cout << "\n4. Testing P&L Tracker..." << std::endl;
    {
        PnLManager pnl_manager;
        pnl_manager.register_strategy("test_strategy");
        
        // Simulate some trades
        pnl_manager.record_execution("test_strategy", 1, 100, Side::BUY, 10, 50000, 50001);
        pnl_manager.record_execution("test_strategy", 2, 100, Side::SELL, 5, 50010, 50005);
        pnl_manager.update_market_price(100, 50005);
        
        auto summary = pnl_manager.get_consolidated_summary();
        std::cout << "  Total P&L: " << summary.total_pnl << " ticks" << std::endl;
        std::cout << "  Total trades: " << summary.total_trades << std::endl;
        std::cout << "  Total volume: " << summary.total_volume << std::endl;
    }
    
    std::cout << "\nAll component tests completed successfully!" << std::endl;
    return 0;
}