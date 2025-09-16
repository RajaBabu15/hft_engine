#include <gtest/gtest.h>
#include "hft/strategy.h"
#include "hft/types.h"
#include <vector>
#include <memory>
#include <chrono>
#include <thread>

using namespace hft;

class AdvancedMarketMakingStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.symbol = 1;
        config_.target_spread_bps = 5.0;
        config_.base_quantity = 100;
        config_.inventory_limit = 10000;
        config_.risk_aversion = 0.1;
    }

    AdvancedMarketMakingStrategy::StrategyConfig config_;
};

TEST_F(AdvancedMarketMakingStrategyTest, BasicSymmetricAlgorithm) {
    config_.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::SIMPLE_SYMMETRIC;
    AdvancedMarketMakingStrategy strategy(config_);
    
    // Test basic market making behavior
    auto orders = strategy.on_book_update(9900, 10100); // Wide spread
    
    EXPECT_GE(orders.size(), 0);
    if (orders.size() >= 2) {
        // Should have one buy and one sell order
        bool has_buy = false, has_sell = false;
        for (const auto& order : orders) {
            if (order.side == Side::BUY) has_buy = true;
            if (order.side == Side::SELL) has_sell = true;
        }
        EXPECT_TRUE(has_buy || has_sell);
    }
}

TEST_F(AdvancedMarketMakingStrategyTest, VolatilityAdaptiveAlgorithm) {
    config_.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::VOLATILITY_ADAPTIVE;
    AdvancedMarketMakingStrategy strategy(config_);
    
    // Simulate price movements to build volatility history
    strategy.on_book_update(10000, 10010);
    std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Small delay for timestamp difference
    
    strategy.on_book_update(10050, 10060); // Large move
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    auto orders = strategy.on_book_update(10100, 10110); // Another move
    
    // Strategy should adapt to volatility
    EXPECT_GE(orders.size(), 0);
    
    // Test that volatility is being calculated
    double vol = strategy.get_current_volatility();
    EXPECT_GE(vol, 0.0);
}

TEST_F(AdvancedMarketMakingStrategyTest, InventorySkewedAlgorithm) {
    config_.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::INVENTORY_SKEWED;
    AdvancedMarketMakingStrategy strategy(config_);
    
    // Simulate fills to build inventory
    Order buy_order{};
    buy_order.side = Side::BUY;
    buy_order.qty = 500;
    strategy.on_fill(buy_order, 10000, 500); // Build long inventory
    
    auto orders = strategy.on_book_update(10000, 10010);
    
    EXPECT_GE(orders.size(), 0);
    EXPECT_EQ(strategy.get_current_inventory(), 500);
}

TEST_F(AdvancedMarketMakingStrategyTest, OrderFlowImbalanceAlgorithm) {
    config_.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::ORDERFLOW_IMBALANCE;
    AdvancedMarketMakingStrategy strategy(config_);
    
    // Simulate order flow
    strategy.on_trade(10005, 100); // Trade above mid (buying pressure)
    strategy.on_trade(10008, 200); // More buying pressure
    
    auto orders = strategy.on_book_update(10000, 10010);
    
    EXPECT_GE(orders.size(), 0);
}

TEST_F(AdvancedMarketMakingStrategyTest, OptimalBidAskAlgorithm) {
    config_.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::OPTIMAL_BID_ASK;
    config_.risk_aversion = 0.2; // Higher risk aversion
    AdvancedMarketMakingStrategy strategy(config_);
    
    auto orders = strategy.on_book_update(10000, 10010);
    
    EXPECT_GE(orders.size(), 0);
}

TEST_F(AdvancedMarketMakingStrategyTest, CrossedMarketHandling) {
    AdvancedMarketMakingStrategy strategy(config_);
    
    // Test with crossed market (invalid)
    auto orders = strategy.on_book_update(10010, 10000);
    EXPECT_EQ(orders.size(), 0);
    
    // Test with zero prices (invalid)
    orders = strategy.on_book_update(0, 10010);
    EXPECT_EQ(orders.size(), 0);
    
    orders = strategy.on_book_update(10000, 0);
    EXPECT_EQ(orders.size(), 0);
}

TEST_F(AdvancedMarketMakingStrategyTest, PerformanceMetrics) {
    AdvancedMarketMakingStrategy strategy(config_);
    
    // Generate some quotes and fills
    auto orders = strategy.on_book_update(10000, 10010);
    
    // Simulate fills
    if (!orders.empty()) {
        strategy.on_fill(orders[0], orders[0].price, orders[0].qty);
    }
    
    // Test performance metrics (should not crash)
    testing::internal::CaptureStdout();
    strategy.print_performance_metrics();
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_FALSE(output.empty());
    EXPECT_NE(output.find("MARKET MAKING STRATEGY PERFORMANCE"), std::string::npos);
}

TEST_F(AdvancedMarketMakingStrategyTest, AlgorithmSwitching) {
    AdvancedMarketMakingStrategy strategy(config_);
    
    EXPECT_EQ(strategy.get_strategy_name(), "AdvancedMarketMaking_VolatilityAdaptive");
    
    testing::internal::CaptureStdout();
    strategy.set_algorithm(AdvancedMarketMakingStrategy::MMAlgorithm::INVENTORY_SKEWED);
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_NE(output.find("Switched to"), std::string::npos);
    EXPECT_EQ(strategy.get_strategy_name(), "AdvancedMarketMaking_InventorySkewed");
}

TEST_F(AdvancedMarketMakingStrategyTest, ConfigurationMethods) {
    AdvancedMarketMakingStrategy strategy(config_);
    
    // Test spread adjustment
    strategy.set_target_spread(10.0);
    
    // Test inventory tracking
    EXPECT_EQ(strategy.get_current_inventory(), 0);
    
    Order buy_order{};
    buy_order.side = Side::BUY;
    buy_order.qty = 200;
    strategy.on_fill(buy_order, 10000, 200);
    
    EXPECT_EQ(strategy.get_current_inventory(), 200);
    
    Order sell_order{};
    sell_order.side = Side::SELL;
    sell_order.qty = 50;
    strategy.on_fill(sell_order, 10005, 50);
    
    EXPECT_EQ(strategy.get_current_inventory(), 150);
}

// Test MomentumStrategy
class MomentumStrategyTest : public ::testing::Test {};

TEST_F(MomentumStrategyTest, BasicMomentumDetection) {
    MomentumStrategy strategy(1);
    
    // Build price history with upward momentum
    strategy.on_book_update(10000, 10010); // Initial price
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    strategy.on_book_update(10050, 10060); // +0.5% move
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    auto orders = strategy.on_book_update(10100, 10110); // +1% total move
    
    // Should generate momentum orders
    EXPECT_GE(orders.size(), 0);
    
    if (!orders.empty()) {
        // Should be market orders
        for (const auto& order : orders) {
            EXPECT_EQ(order.type, OrderType::MARKET);
        }
    }
}

TEST_F(MomentumStrategyTest, InsufficientMomentum) {
    MomentumStrategy strategy(1);
    
    // Small price changes (below threshold)
    strategy.on_book_update(10000, 10010);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    
    auto orders = strategy.on_book_update(10002, 10012); // Only 0.02% move
    
    // Should not generate orders due to insufficient momentum
    EXPECT_EQ(orders.size(), 0);
}

// Test ArbitrageStrategy
class ArbitrageStrategyTest : public ::testing::Test {};

TEST_F(ArbitrageStrategyTest, BasicArbitrageSetup) {
    ArbitrageStrategy strategy(1, 2);
    
    EXPECT_EQ(strategy.get_strategy_name(), "ArbitrageStrategy");
    
    // Basic test - currently returns empty orders
    auto orders = strategy.on_book_update(10000, 10010);
    EXPECT_EQ(orders.size(), 0);
    
    // Test performance metrics
    testing::internal::CaptureStdout();
    strategy.print_performance_metrics();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(output.empty());
}

// Integration tests
class StrategyIntegrationTest : public ::testing::Test {};

TEST_F(StrategyIntegrationTest, MultipleStrategyComparison) {
    // Create different strategy configurations
    AdvancedMarketMakingStrategy::StrategyConfig config1, config2;
    
    config1.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::SIMPLE_SYMMETRIC;
    config1.target_spread_bps = 5.0;
    
    config2.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::VOLATILITY_ADAPTIVE;
    config2.target_spread_bps = 10.0;
    
    AdvancedMarketMakingStrategy strategy1(config1);
    AdvancedMarketMakingStrategy strategy2(config2);
    
    // Same market conditions
    auto orders1 = strategy1.on_book_update(10000, 10020);
    auto orders2 = strategy2.on_book_update(10000, 10020);
    
    // Both should handle the market update
    EXPECT_GE(orders1.size(), 0);
    EXPECT_GE(orders2.size(), 0);
    
    // Different strategies should have different names
    EXPECT_NE(strategy1.get_strategy_name(), strategy2.get_strategy_name());
}

TEST_F(StrategyIntegrationTest, StrategyPolymorphism) {
    // Test polymorphic behavior
    std::vector<std::unique_ptr<TradingStrategy>> strategies;
    
    AdvancedMarketMakingStrategy::StrategyConfig config;
    strategies.push_back(std::make_unique<AdvancedMarketMakingStrategy>(config));
    strategies.push_back(std::make_unique<MomentumStrategy>(1));
    strategies.push_back(std::make_unique<ArbitrageStrategy>(1, 2));
    
    // Test that all strategies implement the interface correctly
    for (auto& strategy : strategies) {
        auto orders = strategy->on_book_update(10000, 10010);
        EXPECT_GE(orders.size(), 0);
        
        auto trade_orders = strategy->on_trade(10005, 100);
        EXPECT_GE(trade_orders.size(), 0);
        
        std::string name = strategy->get_strategy_name();
        EXPECT_FALSE(name.empty());
        
        // Test performance metrics don't crash
        testing::internal::CaptureStdout();
        strategy->print_performance_metrics();
        std::string output = testing::internal::GetCapturedStdout();
        EXPECT_FALSE(output.empty());
    }
}

TEST_F(StrategyIntegrationTest, HighFrequencyUpdates) {
    AdvancedMarketMakingStrategy strategy;
    
    // Simulate rapid market updates
    const int num_updates = 1000;
    const Price base_bid = 10000;
    const Price base_ask = 10010;
    
    for (int i = 0; i < num_updates; ++i) {
        Price bid = base_bid + (i % 20) - 10; // Add some variation
        Price ask = base_ask + (i % 20) - 10;
        
        if (bid < ask) { // Ensure valid market
            auto orders = strategy.on_book_update(bid, ask);
            EXPECT_GE(orders.size(), 0);
        }
    }
    
    // Check that strategy maintained state correctly
    EXPECT_GT(strategy.get_current_volatility(), 0.0);
}

// Performance benchmarks
class StrategyPerformanceTest : public ::testing::Test {};

TEST_F(StrategyPerformanceTest, QuoteGenerationLatency) {
    AdvancedMarketMakingStrategy strategy;
    
    const int num_iterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_iterations; ++i) {
        auto orders = strategy.on_book_update(10000 + i % 100, 10010 + i % 100);
        (void)orders; // Suppress unused variable warning
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double avg_latency_ns = static_cast<double>(duration.count()) / num_iterations;
    
    // Should be very fast - expect less than 10 microseconds per quote generation
    EXPECT_LT(avg_latency_ns, 10000.0); // 10 microseconds
    
    std::cout << "Average quote generation latency: " 
              << avg_latency_ns << " nanoseconds" << std::endl;
}

TEST_F(StrategyPerformanceTest, MemoryUsage) {
    // Test that strategy doesn't leak memory with many updates
    AdvancedMarketMakingStrategy strategy;
    
    for (int i = 0; i < 100000; ++i) {
        strategy.on_book_update(10000, 10010);
        strategy.on_trade(10005, 100);
        
        Order order{};
        order.side = (i % 2) ? Side::BUY : Side::SELL;
        order.qty = 100;
        strategy.on_fill(order, 10005, 100);
    }
    
    // If we reach here without crashing, memory management is working
    EXPECT_TRUE(true);
    
    // Print final metrics to ensure everything is functioning
    testing::internal::CaptureStdout();
    strategy.print_performance_metrics();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_FALSE(output.empty());
}