
#pragma once

#include "hft/order.h"
#include "hft/types.h"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>
#include <iostream>
#include <iomanip>

namespace hft {

// Base strategy interface
class TradingStrategy {
public:
    virtual ~TradingStrategy() = default;
    virtual std::vector<Order> on_book_update(Price best_bid, Price best_ask) = 0;
    virtual std::vector<Order> on_trade(Price price, Quantity qty) = 0;
    virtual void on_fill(const Order& order, Price fill_price, Quantity fill_qty) = 0;
    virtual std::string get_strategy_name() const = 0;
    virtual void print_performance_metrics() const = 0;
};

// Enhanced Market Making Strategy with multiple algorithms
class AdvancedMarketMakingStrategy : public TradingStrategy {
public:
    enum class MMAlgorithm {
        SIMPLE_SYMMETRIC,    // Basic symmetric quotes
        INVENTORY_SKEWED,    // Skew based on inventory
        VOLATILITY_ADAPTIVE, // Adjust spread based on volatility
        ORDERFLOW_IMBALANCE, // React to order flow imbalance
        OPTIMAL_BID_ASK      // Avellaneda-Stoikov model
    };
    
    struct StrategyConfig {
        Symbol symbol;
        MMAlgorithm algorithm;
        double target_spread_bps;      // basis points target spread
        Quantity base_quantity;        // Base order size
        double inventory_limit;        // Max inventory position
        double risk_aversion;          // Risk aversion parameter (gamma)
        double volatility_window;      // window in nanoseconds
        uint32_t max_levels;           // Maximum price levels
        double min_edge_bps;           // Minimum edge in basis points
        bool enable_inventory_management;
        bool enable_dynamic_sizing;
        
        StrategyConfig() : 
            symbol(1),
            algorithm(MMAlgorithm::VOLATILITY_ADAPTIVE),
            target_spread_bps(5.0),
            base_quantity(100),
            inventory_limit(10000),
            risk_aversion(0.1),
            volatility_window(300000000000ULL), // 5 minutes
            max_levels(5),
            min_edge_bps(1.0),
            enable_inventory_management(true),
            enable_dynamic_sizing(true) {}
    };

private:
    StrategyConfig config_;
    std::atomic<uint64_t> order_id_counter_{0};
    
    // Market state tracking
    Price last_best_bid_ = 0;
    Price last_best_ask_ = 0;
    std::deque<std::pair<uint64_t, Price>> price_history_;  // timestamp_ns, mid_price
    
    // Inventory tracking
    int64_t current_inventory_ = 0;
    double avg_inventory_cost_ = 0.0;
    
    // Performance metrics
    std::atomic<uint64_t> total_quotes_{0};
    std::atomic<uint64_t> filled_orders_{0};
    std::atomic<int64_t> realized_pnl_cents_{0};
    std::atomic<double> inventory_turnover_{0.0};
    
    // Volatility tracking
    mutable std::mutex volatility_mutex_;
    std::deque<double> returns_;
    double current_volatility_ = 0.0;
    
    // Order flow tracking
    struct OrderFlowMetrics {
        double buy_pressure = 0.0;
        double sell_pressure = 0.0;
        double imbalance_ratio = 0.0;
        uint64_t last_update_time = 0;
    } order_flow_;

public:
    AdvancedMarketMakingStrategy() : config_(StrategyConfig{}) {
        std::cout << "🤖 Initialized Advanced Market Making Strategy" << std::endl;
        std::cout << "   Symbol: " << config_.symbol << std::endl;
        std::cout << "   Algorithm: " << get_algorithm_name(config_.algorithm) << std::endl;
        std::cout << "   Target Spread: " << config_.target_spread_bps << " bps" << std::endl;
        std::cout << "   Base Quantity: " << config_.base_quantity << std::endl;
    }
    
    explicit AdvancedMarketMakingStrategy(const StrategyConfig& config)
        : config_(config) {
        std::cout << "🤖 Initialized Advanced Market Making Strategy" << std::endl;
        std::cout << "   Symbol: " << config_.symbol << std::endl;
        std::cout << "   Algorithm: " << get_algorithm_name(config_.algorithm) << std::endl;
        std::cout << "   Target Spread: " << config_.target_spread_bps << " bps" << std::endl;
        std::cout << "   Base Quantity: " << config_.base_quantity << std::endl;
    }
    
    std::vector<Order> on_book_update(Price best_bid, Price best_ask) override {
        std::vector<Order> orders;
        
        if (best_bid == 0 || best_ask == 0) return orders;
        if (best_bid >= best_ask) return orders; // Crossed market
        
        // Update market state
        update_market_state(best_bid, best_ask);
        
        // Generate quotes based on selected algorithm
        switch (config_.algorithm) {
            case MMAlgorithm::SIMPLE_SYMMETRIC:
                orders = generate_symmetric_quotes(best_bid, best_ask);
                break;
            case MMAlgorithm::INVENTORY_SKEWED:
                orders = generate_inventory_skewed_quotes(best_bid, best_ask);
                break;
            case MMAlgorithm::VOLATILITY_ADAPTIVE:
                orders = generate_volatility_adaptive_quotes(best_bid, best_ask);
                break;
            case MMAlgorithm::ORDERFLOW_IMBALANCE:
                orders = generate_imbalance_based_quotes(best_bid, best_ask);
                break;
            case MMAlgorithm::OPTIMAL_BID_ASK:
                orders = generate_optimal_quotes(best_bid, best_ask);
                break;
        }
        
        last_best_bid_ = best_bid;
        last_best_ask_ = best_ask;
        total_quotes_.fetch_add(orders.size(), std::memory_order_relaxed);
        
        return orders;
    }
    
    std::vector<Order> on_trade(Price price, Quantity qty) override {
        // Update order flow metrics
        update_order_flow_metrics(price, qty);
        return {}; // No immediate reaction to trades in this implementation
    }
    
    void on_fill(const Order& order, Price fill_price, Quantity fill_qty) override {
        filled_orders_.fetch_add(1, std::memory_order_relaxed);
        
        // Update inventory
        int64_t inventory_change = (order.side == Side::BUY) ? 
            static_cast<int64_t>(fill_qty) : -static_cast<int64_t>(fill_qty);
        
        current_inventory_ += inventory_change;
        
        // Calculate realized P&L (simplified)
        Price mid_price = (last_best_bid_ + last_best_ask_) / 2;
        int64_t pnl_cents = 0;
        
        if (order.side == Side::BUY) {
            pnl_cents = static_cast<int64_t>(fill_qty) * (mid_price - fill_price);
        } else {
            pnl_cents = static_cast<int64_t>(fill_qty) * (fill_price - mid_price);
        }
        
        realized_pnl_cents_.fetch_add(pnl_cents, std::memory_order_relaxed);
    }
    
    std::string get_strategy_name() const override {
        return "AdvancedMarketMaking_" + get_algorithm_name(config_.algorithm);
    }
    
    void print_performance_metrics() const override {
        std::cout << "\n MARKET MAKING STRATEGY PERFORMANCE" << std::endl;
        std::cout << "=====================================" << std::endl;
        std::cout << "Strategy: " << get_strategy_name() << std::endl;
        std::cout << "Total Quotes: " << total_quotes_.load(std::memory_order_relaxed) << std::endl;
        std::cout << "Filled Orders: " << filled_orders_.load(std::memory_order_relaxed) << std::endl;
        
        uint64_t total = total_quotes_.load(std::memory_order_relaxed);
        uint64_t filled = filled_orders_.load(std::memory_order_relaxed);
        double fill_rate = total > 0 ? static_cast<double>(filled) / total * 100.0 : 0.0;
        std::cout << "Fill Rate: " << std::fixed << std::setprecision(1) << fill_rate << "%" << std::endl;
        
        int64_t pnl = realized_pnl_cents_.load(std::memory_order_relaxed);
        std::cout << "Realized P&L: $" << std::setprecision(2) << pnl / 100.0 << std::endl;
        std::cout << "Current Inventory: " << current_inventory_ << " shares" << std::endl;
        std::cout << "Current Volatility: " << std::setprecision(4) << current_volatility_ * 100.0 << "%" << std::endl;
        std::cout << "Order Flow Imbalance: " << std::setprecision(3) << order_flow_.imbalance_ratio << std::endl;
    }
    
    // Configuration methods
    void set_algorithm(MMAlgorithm algorithm) {
        config_.algorithm = algorithm;
        std::cout << " Switched to " << get_algorithm_name(algorithm) << " algorithm" << std::endl;
    }
    
    void set_target_spread(double bps) {
        config_.target_spread_bps = bps;
    }
    
    double get_current_volatility() const {
        return current_volatility_;
    }
    
    int64_t get_current_inventory() const {
        return current_inventory_;
    }

private:
    void update_market_state(Price best_bid, Price best_ask) {
        Price mid_price = (best_bid + best_ask) / 2;
        uint64_t timestamp = now_ns();
        
        // Update price history
        price_history_.emplace_back(timestamp, mid_price);
        
        // Keep only recent history for volatility calculation
        while (!price_history_.empty() && 
               timestamp - price_history_.front().first > config_.volatility_window) {
            price_history_.pop_front();
        }
        
        // Update volatility
        update_volatility();
    }
    
    void update_volatility() {
        std::lock_guard<std::mutex> lock(volatility_mutex_);
        
        if (price_history_.size() < 2) {
            current_volatility_ = 0.01; // Default volatility
            return;
        }
        
        // Calculate returns
        returns_.clear();
        for (size_t i = 1; i < price_history_.size(); ++i) {
            Price price1 = price_history_[i-1].second;
            Price price2 = price_history_[i].second;
            
            if (price1 > 0) {
                double return_val = std::log(static_cast<double>(price2) / static_cast<double>(price1));
                returns_.push_back(return_val);
            }
        }
        
        // Calculate volatility (standard deviation of returns)
        if (returns_.size() > 1) {
            double mean = 0.0;
            for (double ret : returns_) {
                mean += ret;
            }
            mean /= returns_.size();
            
            double variance = 0.0;
            for (double ret : returns_) {
                variance += (ret - mean) * (ret - mean);
            }
            variance /= (returns_.size() - 1);
            
            current_volatility_ = std::sqrt(variance * 252 * 24 * 60 * 60); // Annualized
        }
    }
    
    void update_order_flow_metrics(Price price, Quantity qty) {
        uint64_t current_time = now_ns();
        Price mid_price = (last_best_bid_ + last_best_ask_) / 2;
        
        // Determine if trade was buyer or seller initiated
        bool buyer_initiated = price >= mid_price;
        
        double decay_factor = 0.95; // Exponential decay
        
        if (buyer_initiated) {
            order_flow_.buy_pressure = order_flow_.buy_pressure * decay_factor + 
                static_cast<double>(qty) * (1.0 - decay_factor);
        } else {
            order_flow_.sell_pressure = order_flow_.sell_pressure * decay_factor + 
                static_cast<double>(qty) * (1.0 - decay_factor);
        }
        
        // Calculate imbalance ratio
        double total_pressure = order_flow_.buy_pressure + order_flow_.sell_pressure;
        if (total_pressure > 0) {
            order_flow_.imbalance_ratio = (order_flow_.buy_pressure - order_flow_.sell_pressure) / total_pressure;
        }
        
        order_flow_.last_update_time = current_time;
    }
    
    std::vector<Order> generate_symmetric_quotes(Price best_bid, Price best_ask) {
        std::vector<Order> orders;
        Price spread = best_ask - best_bid;
        Price target_spread = std::max(static_cast<Price>(1), 
            static_cast<Price>(((best_bid + best_ask) / 2) * config_.target_spread_bps / 10000.0));
        
        if (spread <= target_spread) {
            return orders; // Spread too tight, don't compete
        }
        
        Price our_bid = best_bid + 1;
        Price our_ask = best_ask - 1;
        
        if (our_bid >= our_ask) return orders;
        
        // Buy order
        Order buy_order{};
        buy_order.id = ++order_id_counter_;
        buy_order.symbol = config_.symbol;
        buy_order.side = Side::BUY;
        buy_order.type = OrderType::LIMIT;
        buy_order.price = our_bid;
        buy_order.qty = config_.base_quantity;
        orders.push_back(buy_order);
        
        // Sell order
        Order sell_order{};
        sell_order.id = ++order_id_counter_;
        sell_order.symbol = config_.symbol;
        sell_order.side = Side::SELL;
        sell_order.type = OrderType::LIMIT;
        sell_order.price = our_ask;
        sell_order.qty = config_.base_quantity;
        orders.push_back(sell_order);
        
        return orders;
    }
    
    std::vector<Order> generate_inventory_skewed_quotes(Price best_bid, Price best_ask) {
        std::vector<Order> orders;
        
        // Calculate inventory skew
        double inventory_ratio = current_inventory_ / config_.inventory_limit;
        double skew_factor = std::tanh(inventory_ratio * 2.0); // Sigmoid-like skewing
        
        Price mid_price = (best_bid + best_ask) / 2;
        Price base_spread = std::max(static_cast<Price>(2), 
            static_cast<Price>(mid_price * config_.target_spread_bps / 10000.0));
        
        // Adjust spread based on inventory
        Price bid_adjustment = static_cast<Price>(base_spread * (1.0 - skew_factor) / 2);
        Price ask_adjustment = static_cast<Price>(base_spread * (1.0 + skew_factor) / 2);
        
        Price our_bid = mid_price - bid_adjustment;
        Price our_ask = mid_price + ask_adjustment;
        
        // Ensure we don't cross the market
        our_bid = std::min(our_bid, best_bid);
        our_ask = std::max(our_ask, best_ask);
        
        if (our_bid > 0 && our_bid < best_ask) {
            Order buy_order{};
            buy_order.id = ++order_id_counter_;
            buy_order.symbol = config_.symbol;
            buy_order.side = Side::BUY;
            buy_order.type = OrderType::LIMIT;
            buy_order.price = our_bid;
            buy_order.qty = config_.base_quantity;
            orders.push_back(buy_order);
        }
        
        if (our_ask > 0 && our_ask > best_bid) {
            Order sell_order{};
            sell_order.id = ++order_id_counter_;
            sell_order.symbol = config_.symbol;
            sell_order.side = Side::SELL;
            sell_order.type = OrderType::LIMIT;
            sell_order.price = our_ask;
            sell_order.qty = config_.base_quantity;
            orders.push_back(sell_order);
        }
        
        return orders;
    }
    
    std::vector<Order> generate_volatility_adaptive_quotes(Price best_bid, Price best_ask) {
        std::vector<Order> orders;
        
        Price mid_price = (best_bid + best_ask) / 2;
        
        // Adjust spread based on volatility
        double vol_factor = std::max(0.5, std::min(3.0, current_volatility_ / 0.2)); // Scale volatility
        Price adaptive_spread = static_cast<Price>(mid_price * config_.target_spread_bps * vol_factor / 10000.0);
        adaptive_spread = std::max(adaptive_spread, static_cast<Price>(1));
        
        Price our_bid = mid_price - adaptive_spread / 2;
        Price our_ask = mid_price + adaptive_spread / 2;
        
        // Don't improve the market too aggressively
        our_bid = std::min(our_bid, best_bid);
        our_ask = std::max(our_ask, best_ask);
        
        if (our_bid > 0 && our_bid < best_ask) {
            Order buy_order{};
            buy_order.id = ++order_id_counter_;
            buy_order.symbol = config_.symbol;
            buy_order.side = Side::BUY;
            buy_order.type = OrderType::LIMIT;
            buy_order.price = our_bid;
            buy_order.qty = static_cast<Quantity>(config_.base_quantity / vol_factor); // Smaller size in high vol
            orders.push_back(buy_order);
        }
        
        if (our_ask > 0 && our_ask > best_bid) {
            Order sell_order{};
            sell_order.id = ++order_id_counter_;
            sell_order.symbol = config_.symbol;
            sell_order.side = Side::SELL;
            sell_order.type = OrderType::LIMIT;
            sell_order.price = our_ask;
            sell_order.qty = static_cast<Quantity>(config_.base_quantity / vol_factor);
            orders.push_back(sell_order);
        }
        
        return orders;
    }
    
    std::vector<Order> generate_imbalance_based_quotes(Price best_bid, Price best_ask) {
        std::vector<Order> orders;
        
        Price mid_price = (best_bid + best_ask) / 2;
        double imbalance = order_flow_.imbalance_ratio;
        
        // Adjust quotes based on order flow imbalance
        Price spread = static_cast<Price>(mid_price * config_.target_spread_bps / 10000.0);
        Price imbalance_adjustment = static_cast<Price>(spread * imbalance * 0.5);
        
        Price our_bid = mid_price - spread / 2 + imbalance_adjustment;
        Price our_ask = mid_price + spread / 2 + imbalance_adjustment;
        
        our_bid = std::min(our_bid, best_bid);
        our_ask = std::max(our_ask, best_ask);
        
        // Adjust quantities based on imbalance (more aggressive on the favorable side)
        Quantity buy_qty = static_cast<Quantity>(config_.base_quantity * (1.0 - imbalance * 0.5));
        Quantity sell_qty = static_cast<Quantity>(config_.base_quantity * (1.0 + imbalance * 0.5));
        
        if (our_bid > 0 && our_bid < best_ask && buy_qty > 0) {
            Order buy_order{};
            buy_order.id = ++order_id_counter_;
            buy_order.symbol = config_.symbol;
            buy_order.side = Side::BUY;
            buy_order.type = OrderType::LIMIT;
            buy_order.price = our_bid;
            buy_order.qty = buy_qty;
            orders.push_back(buy_order);
        }
        
        if (our_ask > 0 && our_ask > best_bid && sell_qty > 0) {
            Order sell_order{};
            sell_order.id = ++order_id_counter_;
            sell_order.symbol = config_.symbol;
            sell_order.side = Side::SELL;
            sell_order.type = OrderType::LIMIT;
            sell_order.price = our_ask;
            sell_order.qty = sell_qty;
            orders.push_back(sell_order);
        }
        
        return orders;
    }
    
    std::vector<Order> generate_optimal_quotes(Price best_bid, Price best_ask) {
        // Avellaneda-Stoikov optimal market making model
        std::vector<Order> orders;
        
        Price mid_price = (best_bid + best_ask) / 2;
        double T = 1.0; // Time to end of trading session (normalized)
        double gamma = config_.risk_aversion; // Risk aversion parameter
        double sigma = current_volatility_;
        double q = current_inventory_ / config_.inventory_limit; // Normalized inventory
        
        // Optimal spread calculation
        double optimal_spread = gamma * sigma * sigma * T / 2.0 + 
                               std::log(1.0 + gamma / 0.1) / gamma; // lambda = 0.1 (arrival rate)
        
        // Optimal skew based on inventory
        double skew = gamma * sigma * sigma * T * q / 2.0;
        
        Price spread_price = static_cast<Price>(mid_price * optimal_spread);
        Price skew_price = static_cast<Price>(mid_price * skew);
        
        Price our_bid = mid_price - spread_price / 2 + skew_price;
        Price our_ask = mid_price + spread_price / 2 + skew_price;
        
        our_bid = std::min(our_bid, best_bid);
        our_ask = std::max(our_ask, best_ask);
        
        if (our_bid > 0 && our_bid < best_ask) {
            Order buy_order{};
            buy_order.id = ++order_id_counter_;
            buy_order.symbol = config_.symbol;
            buy_order.side = Side::BUY;
            buy_order.type = OrderType::LIMIT;
            buy_order.price = our_bid;
            buy_order.qty = config_.base_quantity;
            orders.push_back(buy_order);
        }
        
        if (our_ask > 0 && our_ask > best_bid) {
            Order sell_order{};
            sell_order.id = ++order_id_counter_;
            sell_order.symbol = config_.symbol;
            sell_order.side = Side::SELL;
            sell_order.type = OrderType::LIMIT;
            sell_order.price = our_ask;
            sell_order.qty = config_.base_quantity;
            orders.push_back(sell_order);
        }
        
        return orders;
    }
    
    std::string get_algorithm_name(MMAlgorithm algo) const {
        switch (algo) {
            case MMAlgorithm::SIMPLE_SYMMETRIC: return "SimpleSymmetric";
            case MMAlgorithm::INVENTORY_SKEWED: return "InventorySkewed";
            case MMAlgorithm::VOLATILITY_ADAPTIVE: return "VolatilityAdaptive";
            case MMAlgorithm::ORDERFLOW_IMBALANCE: return "OrderFlowImbalance";
            case MMAlgorithm::OPTIMAL_BID_ASK: return "OptimalBidAsk";
            default: return "Unknown";
        }
    }
};

// Legacy interface for backward compatibility
using MarketMakingStrategy = AdvancedMarketMakingStrategy;

// Momentum Strategy
class MomentumStrategy : public TradingStrategy {
private:
    Symbol symbol_;
    std::atomic<uint64_t> order_id_counter_{0};
    std::deque<std::pair<uint64_t, Price>> price_history_;
    double momentum_threshold_ = 0.005; // 0.5% momentum threshold
    Quantity base_quantity_ = 50;
    
public:
    explicit MomentumStrategy(Symbol symbol) : symbol_(symbol) {}
    
    std::vector<Order> on_book_update(Price best_bid, Price best_ask) override {
        std::vector<Order> orders;
        Price mid_price = (best_bid + best_ask) / 2;
        uint64_t timestamp = now_ns();
        
        price_history_.emplace_back(timestamp, mid_price);
        
        // Keep only recent history (last 10 seconds)
        while (!price_history_.empty() && 
               timestamp - price_history_.front().first > 10000000000ULL) {
            price_history_.pop_front();
        }
        
        if (price_history_.size() < 2) return orders;
        
        // Calculate momentum
        Price old_price = price_history_.front().second;
        double momentum = (static_cast<double>(mid_price) - static_cast<double>(old_price)) / old_price;
        
        // Generate momentum-based orders
        if (std::abs(momentum) > momentum_threshold_) {
            Order order{};
            order.id = ++order_id_counter_;
            order.symbol = symbol_;
            order.type = OrderType::MARKET;
            order.qty = base_quantity_;
            
            if (momentum > 0) {
                // Positive momentum - buy
                order.side = Side::BUY;
                order.price = best_ask;
            } else {
                // Negative momentum - sell
                order.side = Side::SELL;
                order.price = best_bid;
            }
            
            orders.push_back(order);
        }
        
        return orders;
    }
    
    std::vector<Order> on_trade(Price price, Quantity qty) override { return {}; }
    void on_fill(const Order& order, Price fill_price, Quantity fill_qty) override {}
    std::string get_strategy_name() const override { return "MomentumStrategy"; }
    void print_performance_metrics() const override {
        std::cout << "Momentum Strategy - Basic implementation" << std::endl;
    }
};

// Arbitrage Strategy
class ArbitrageStrategy : public TradingStrategy {
private:
    Symbol symbol1_, symbol2_;
    std::atomic<uint64_t> order_id_counter_{0};
    Price last_price1_ = 0, last_price2_ = 0;
    double spread_threshold_ = 0.002; // 0.2% spread threshold
    
public:
    ArbitrageStrategy(Symbol sym1, Symbol sym2) : symbol1_(sym1), symbol2_(sym2) {}
    
    std::vector<Order> on_book_update(Price best_bid, Price best_ask) override {
        // This would need prices from both symbols - simplified for demo
        return {};
    }
    
    std::vector<Order> on_trade(Price price, Quantity qty) override { return {}; }
    void on_fill(const Order& order, Price fill_price, Quantity fill_qty) override {}
    std::string get_strategy_name() const override { return "ArbitrageStrategy"; }
    void print_performance_metrics() const override {
        std::cout << "Arbitrage Strategy - Basic implementation" << std::endl;
    }
};

}
