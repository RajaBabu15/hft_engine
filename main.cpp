#include "hft/logger.h"
#include "hft/risk_manager.h"
#include "hft/order.h"
#include "hft/matching_engine.h"
#include "hft/deep_profiler.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>

using namespace hft;

int main(){
    Logger log;
    RiskManager rm(50, 1'000'000'000, 1'000'000);

    // Simple MatchingEngine benchmark
    MatchingEngine engine(rm, log, 1<<20);

    const uint32_t N = 100000; // 100k orders
    const Symbol sym = 1;

    // Generate deterministic orders around mid-price
    std::mt19937_64 rng(123456789);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> px_off(-500, 500);
    std::uniform_int_distribution<int> qty_dist(10, 1000);

    auto t0 = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < N; ++i) {
        Order o{};
        o.id = i + 1;
        o.symbol = sym;
        o.side = side_dist(rng) ? Side::BUY : Side::SELL;
        o.type = OrderType::LIMIT;
        o.price = 50'000 + px_off(rng);
        o.qty = qty_dist(rng);
        engine.submit_order(std::move(o));
    }

    // Orders are processed synchronously in submit_order()
    size_t processed = N;

    auto t1 = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    double per_order_ns = static_cast<double>(ns) / N;

    std::cout << "Processed: " << processed << " orders\n";
    std::cout << "Total time: " << ns / 1e6 << " ms\n";
    std::cout << "Avg per order: " << per_order_ns << " ns\n";
    std::cout << "Trades: " << engine.trade_count() << ", Accepts: " << engine.accept_count() << ", Rejects: " << engine.reject_count() << "\n";

    // Emit deep profiling reports (summary and detailed)
    std::cout << hft::DeepProfiler::instance().generate_report();
    std::cout << hft::DeepProfiler::instance().generate_detailed_report();
    

    return 0;
}
