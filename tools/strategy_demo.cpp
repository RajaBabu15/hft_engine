#include "hft/strategy.h"
#include "hft/types.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <random>

using namespace hft;

class StrategyDemonstrator {
private:
    std::mt19937 rng_;
    std::uniform_real_distribution<double> price_dist_;
    std::uniform_real_distribution<double> vol_dist_;
    std::uniform_int_distribution<int> qty_dist_;
    
    struct MarketData {
        Price bid, ask;
        uint64_t timestamp;
        double volatility;
    };
    
    std::vector<MarketData> generate_realistic_market_data(int num_points, Price base_price = 10000) {
        std::vector<MarketData> data;
        data.reserve(num_points);
        
        Price current_price = base_price;
        double current_vol = 0.02; // 2% volatility
        
        for (int i = 0; i < num_points; ++i) {
            // Generate realistic price movements with volatility clustering
            double return_val = std::normal_distribution<double>(0.0, current_vol)(rng_);
            current_price = static_cast<Price>(current_price * (1.0 + return_val / 100.0));
            
            // Volatility clustering - high vol tends to follow high vol
            current_vol = std::max(0.005, current_vol * 0.95 + vol_dist_(rng_) * 0.05);
            
            // Generate bid-ask spread (typically 0.01% to 0.1% of mid price)
            Price spread = static_cast<Price>(current_price * 0.0005 * (1.0 + vol_dist_(rng_)));
            spread = std::max(static_cast<Price>(1), spread);
            
            MarketData md;
            md.bid = current_price - spread / 2;
            md.ask = current_price + spread / 2;
            md.timestamp = now_ns() + i * 1000000; // 1ms intervals
            md.volatility = current_vol;
            
            data.push_back(md);
        }
        
        return data;
    }
    
public:
    StrategyDemonstrator() : rng_(std::random_device{}()), 
                           price_dist_(-0.002, 0.002), 
                           vol_dist_(0.0, 0.01),
                           qty_dist_(50, 500) {
    }
    
    void demonstrate_all_algorithms() {
        std::cout << " ADVANCED MARKET MAKING STRATEGY DEMONSTRATION" << std::endl;
        std::cout << "=================================================" << std::endl << std::endl;
        
        // Generate realistic market data
        auto market_data = generate_realistic_market_data(100, 10000);
        
        // Test each algorithm
        std::vector<std::pair<std::string, AdvancedMarketMakingStrategy::MMAlgorithm>> algorithms = {
            {"Simple Symmetric", AdvancedMarketMakingStrategy::MMAlgorithm::SIMPLE_SYMMETRIC},
            {"Inventory Skewed", AdvancedMarketMakingStrategy::MMAlgorithm::INVENTORY_SKEWED},
            {"Volatility Adaptive", AdvancedMarketMakingStrategy::MMAlgorithm::VOLATILITY_ADAPTIVE},
            {"Order Flow Imbalance", AdvancedMarketMakingStrategy::MMAlgorithm::ORDERFLOW_IMBALANCE},
            {"Optimal Bid-Ask (Avellaneda-Stoikov)", AdvancedMarketMakingStrategy::MMAlgorithm::OPTIMAL_BID_ASK}
        };
        
        for (const auto& [name, algorithm] : algorithms) {
            demonstrate_algorithm(name, algorithm, market_data);
            std::cout << "\n" << std::string(60, '-') << "\n" << std::endl;
        }
    }
    
    void demonstrate_algorithm(const std::string& name, 
                             AdvancedMarketMakingStrategy::MMAlgorithm algorithm,
                             const std::vector<MarketData>& market_data) {
        std::cout << " Testing " << name << " Algorithm" << std::endl;
        std::cout << std::string(40, '=') << std::endl;
        
        // Configure strategy
        AdvancedMarketMakingStrategy::StrategyConfig config;
        config.algorithm = algorithm;
        config.target_spread_bps = 5.0;
        config.base_quantity = 100;
        config.inventory_limit = 5000;
        config.risk_aversion = 0.1;
        
        AdvancedMarketMakingStrategy strategy(config);
        
        int orders_generated = 0;
        int fills_simulated = 0;
        std::vector<Order> all_orders;
        
        // Process market data
        for (size_t i = 0; i < market_data.size(); ++i) {
            const auto& md = market_data[i];
            
            // Generate orders based on market update
            auto orders = strategy.on_book_update(md.bid, md.ask);
            orders_generated += orders.size();
            all_orders.insert(all_orders.end(), orders.begin(), orders.end());
            
            // Simulate some trades for order flow tracking
            if (i % 5 == 0 && i > 0) {
                Price trade_price = (md.bid + md.ask) / 2 + price_dist_(rng_) * 10;
                Quantity trade_qty = qty_dist_(rng_);
                strategy.on_trade(trade_price, trade_qty);
            }
            
            // Simulate fills (10% chance per order)
            for (const auto& order : orders) {
                if (std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < 0.1) {
                    strategy.on_fill(order, order.price, order.qty);
                    fills_simulated++;
                }
            }
            
            // Print sample orders periodically
            if (i == 10 || i == 30 || i == 60) {
                print_sample_orders(orders, md);
            }
        }
        
        // Print final statistics
        std::cout << "\n ALGORITHM PERFORMANCE:" << std::endl;
        std::cout << "Total Orders Generated: " << orders_generated << std::endl;
        std::cout << "Simulated Fills: " << fills_simulated << std::endl;
        std::cout << "Current Inventory: " << strategy.get_current_inventory() << " shares" << std::endl;
        std::cout << "Current Volatility: " << std::fixed << std::setprecision(4) 
                  << strategy.get_current_volatility() * 100.0 << "%" << std::endl;
        
        strategy.print_performance_metrics();
    }
    
    void print_sample_orders(const std::vector<Order>& orders, const MarketData& md) {
        if (orders.empty()) return;
        
        std::cout << "\n📋 Sample Orders (Market: " << md.bid << " x " << md.ask << "):" << std::endl;
        for (const auto& order : orders) {
            std::cout << "  " << (order.side == Side::BUY ? "BUY" : "SELL") 
                      << " " << order.qty << " @ " << order.price
                      << " (" << (order.type == OrderType::LIMIT ? "LIMIT" : "MARKET") << ")" << std::endl;
        }
    }
    
    void compare_strategies_side_by_side() {
        std::cout << "\n⚖️  STRATEGY COMPARISON" << std::endl;
        std::cout << "======================" << std::endl;
        
        auto market_data = generate_realistic_market_data(50, 10000);
        
        // Create multiple strategies with different algorithms
        std::vector<std::unique_ptr<AdvancedMarketMakingStrategy>> strategies;
        std::vector<std::string> names;
        
        AdvancedMarketMakingStrategy::StrategyConfig config;
        
        config.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::SIMPLE_SYMMETRIC;
        strategies.push_back(std::make_unique<AdvancedMarketMakingStrategy>(config));
        names.push_back("SimpleSymmetric");
        
        config.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::VOLATILITY_ADAPTIVE;
        strategies.push_back(std::make_unique<AdvancedMarketMakingStrategy>(config));
        names.push_back("VolatilityAdaptive");
        
        config.algorithm = AdvancedMarketMakingStrategy::MMAlgorithm::INVENTORY_SKEWED;
        strategies.push_back(std::make_unique<AdvancedMarketMakingStrategy>(config));
        names.push_back("InventorySkewed");
        
        // Process same market data through all strategies
        std::vector<int> order_counts(strategies.size(), 0);
        std::vector<int> fill_counts(strategies.size(), 0);
        
        for (const auto& md : market_data) {
            for (size_t i = 0; i < strategies.size(); ++i) {
                auto orders = strategies[i]->on_book_update(md.bid, md.ask);
                order_counts[i] += orders.size();
                
                // Simulate fills
                for (const auto& order : orders) {
                    if (std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < 0.15) {
                        strategies[i]->on_fill(order, order.price, order.qty);
                        fill_counts[i]++;
                    }
                }
            }
        }
        
        // Print comparison table
        std::cout << std::left << std::setw(20) << "Strategy" 
                  << std::setw(12) << "Orders" 
                  << std::setw(12) << "Fills" 
                  << std::setw(15) << "Inventory" 
                  << std::setw(15) << "Volatility" << std::endl;
        std::cout << std::string(74, '-') << std::endl;
        
        for (size_t i = 0; i < strategies.size(); ++i) {
            std::cout << std::left << std::setw(20) << names[i]
                      << std::setw(12) << order_counts[i]
                      << std::setw(12) << fill_counts[i]
                      << std::setw(15) << strategies[i]->get_current_inventory()
                      << std::fixed << std::setprecision(3) << std::setw(15) 
                      << strategies[i]->get_current_volatility() * 100.0 << "%" << std::endl;
        }
    }
    
    void demonstrate_dynamic_algorithm_switching() {
        std::cout << "\n DYNAMIC ALGORITHM SWITCHING DEMO" << std::endl;
        std::cout << "===================================" << std::endl;
        
        AdvancedMarketMakingStrategy::StrategyConfig config;
        AdvancedMarketMakingStrategy strategy(config);
        
        auto market_data = generate_realistic_market_data(30, 10000);
        
        // Switch algorithms based on market conditions
        for (size_t i = 0; i < market_data.size(); ++i) {
            const auto& md = market_data[i];
            
            // Switch algorithm based on simulated market conditions
            if (i == 10) {
                std::cout << "\n High volatility detected - switching to Volatility Adaptive" << std::endl;
                strategy.set_algorithm(AdvancedMarketMakingStrategy::MMAlgorithm::VOLATILITY_ADAPTIVE);
            } else if (i == 20) {
                std::cout << "\n Large inventory detected - switching to Inventory Skewed" << std::endl;
                strategy.set_algorithm(AdvancedMarketMakingStrategy::MMAlgorithm::INVENTORY_SKEWED);
                
                // Simulate large inventory
                Order big_order{};
                big_order.side = Side::BUY;
                big_order.qty = 1000;
                strategy.on_fill(big_order, md.bid + 5, 1000);
            }
            
            auto orders = strategy.on_book_update(md.bid, md.ask);
            
            if (i % 10 == 0) {
                std::cout << "Market " << i << ": " << md.bid << " x " << md.ask 
                          << ", Generated " << orders.size() << " orders" << std::endl;
            }
        }
        
        strategy.print_performance_metrics();
    }
    
    void performance_stress_test() {
        std::cout << "\n PERFORMANCE STRESS TEST" << std::endl;
        std::cout << "=========================" << std::endl;
        
        AdvancedMarketMakingStrategy strategy;
        
        const int num_updates = 100000;
        const Price base_price = 10000;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < num_updates; ++i) {
            Price bid = base_price + (i % 100) - 50;
            Price ask = bid + 5 + (i % 10);
            
            auto orders = strategy.on_book_update(bid, ask);
            
            // Simulate some trades and fills
            if (i % 100 == 0) {
                strategy.on_trade(bid + 2, 100);
            }
            
            if (!orders.empty() && i % 1000 == 0) {
                strategy.on_fill(orders[0], orders[0].price, orders[0].qty);
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        
        double avg_latency_ns = static_cast<double>(duration.count()) / num_updates;
        double throughput_ops_per_sec = num_updates / (duration.count() / 1e9);
        
        std::cout << " PERFORMANCE RESULTS:" << std::endl;
        std::cout << "Total Updates: " << num_updates << std::endl;
        std::cout << "Total Time: " << duration.count() / 1e6 << " ms" << std::endl;
        std::cout << "Average Latency: " << std::fixed << std::setprecision(2) 
                  << avg_latency_ns << " ns per update" << std::endl;
        std::cout << "Throughput: " << std::fixed << std::setprecision(0) 
                  << throughput_ops_per_sec << " ops/second" << std::endl;
        
        std::cout << "\n Strategy State:" << std::endl;
        strategy.print_performance_metrics();
    }
};

int main() {
    try {
        StrategyDemonstrator demo;
        
        std::cout << " HFT ENGINE - ADVANCED MARKET MAKING STRATEGIES" << std::endl;
        std::cout << "================================================" << std::endl;
        std::cout << "This demo showcases sophisticated market-making algorithms" << std::endl;
        std::cout << "including volatility adaptation, inventory management, and" << std::endl;
        std::cout << "optimal bid-ask spread calculations.\n" << std::endl;
        
        // Main demonstration
        demo.demonstrate_all_algorithms();
        
        // Side-by-side comparison
        demo.compare_strategies_side_by_side();
        
        // Dynamic switching demo
        demo.demonstrate_dynamic_algorithm_switching();
        
        // Performance test
        demo.performance_stress_test();
        
        std::cout << "\n DEMONSTRATION COMPLETED SUCCESSFULLY" << std::endl;
        std::cout << "\nKey Features Demonstrated:" << std::endl;
        std::cout << "• Multiple sophisticated MM algorithms" << std::endl;
        std::cout << "• Real-time volatility adaptation" << std::endl;
        std::cout << "• Inventory-aware quote skewing" << std::endl;
        std::cout << "• Order flow imbalance detection" << std::endl;
        std::cout << "• Avellaneda-Stoikov optimal MM model" << std::endl;
        std::cout << "• Dynamic algorithm switching" << std::endl;
        std::cout << "• High-frequency performance optimization" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << " Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
