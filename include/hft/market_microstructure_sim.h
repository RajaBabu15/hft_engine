#pragma once

#include "hft/types.h"
#include "hft/order.h"
#include <vector>
#include <random>
#include <atomic>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace hft {

// Market impact models for HFT simulation
class MarketImpactModel {
public:
    enum class ImpactType {
        LINEAR,
        SQUARE_ROOT,
        LOGARITHMIC,
        ALMGREN_CHRISS
    };

    struct ImpactParameters {
        double temporary_impact_coeff = 0.001;   // Temporary impact coefficient
        double permanent_impact_coeff = 0.0005;  // Permanent impact coefficient
        double liquidity_factor = 1000000;       // Market liquidity factor
        double volatility = 0.02;                // Annual volatility
        ImpactType model_type = ImpactType::SQUARE_ROOT;
    };

private:
    ImpactParameters params_;
    std::mt19937 rng_;

public:
    explicit MarketImpactModel(const ImpactParameters& params = {}) 
        : params_(params), rng_(std::random_device{}()) {}

    // Calculate market impact for given order
    double calculate_impact(Quantity order_size, double participation_rate) const {
        double normalized_size = static_cast<double>(order_size) / params_.liquidity_factor;
        
        switch (params_.model_type) {
            case ImpactType::LINEAR:
                return params_.temporary_impact_coeff * normalized_size;
            case ImpactType::SQUARE_ROOT:
                return params_.temporary_impact_coeff * std::sqrt(normalized_size);
            case ImpactType::LOGARITHMIC:
                return params_.temporary_impact_coeff * std::log(1.0 + normalized_size);
            case ImpactType::ALMGREN_CHRISS:
                return params_.temporary_impact_coeff * std::pow(normalized_size, 0.6) * 
                       std::sqrt(participation_rate);
        }
        return 0.0;
    }

    // Simulate execution with realistic slippage
    Price simulate_execution_price(Price intended_price, Quantity order_size, 
                                   Side side, double market_volatility) {
        double impact = calculate_impact(order_size, 0.1); // 10% participation rate
        double volatility_noise = std::normal_distribution<double>(0.0, market_volatility)(rng_);
        
        double price_impact = impact + volatility_noise;
        if (side == Side::SELL) price_impact = -price_impact;
        
        return static_cast<Price>(intended_price + price_impact * intended_price);
    }
};

// Latency distribution simulator
class LatencySimulator {
public:
    struct LatencyProfile {
        uint64_t base_latency_ns = 500;      // 0.5 μs base latency
        uint64_t network_jitter_ns = 100;    // Network jitter
        uint64_t processing_time_ns = 200;   // Processing time
        double congestion_factor = 1.0;      // Congestion multiplier
        bool enable_realistic_distribution = true;
    };

private:
    LatencyProfile profile_;
    std::mt19937 rng_;
    std::gamma_distribution<double> latency_dist_;

public:
    explicit LatencySimulator(const LatencyProfile& profile = {})
        : profile_(profile), rng_(std::random_device{}()), 
          latency_dist_(2.0, profile.base_latency_ns / 2.0) {}

    uint64_t simulate_latency() {
        if (!profile_.enable_realistic_distribution) {
            return profile_.base_latency_ns + 
                   std::uniform_int_distribution<uint64_t>(0, profile_.network_jitter_ns)(rng_);
        }

        // Realistic gamma distribution for latency
        uint64_t base = static_cast<uint64_t>(latency_dist_(rng_));
        uint64_t jitter = std::uniform_int_distribution<uint64_t>(0, profile_.network_jitter_ns)(rng_);
        uint64_t processing = profile_.processing_time_ns;
        
        return static_cast<uint64_t>((base + jitter + processing) * profile_.congestion_factor);
    }

    void set_congestion_factor(double factor) {
        profile_.congestion_factor = factor;
    }
};

// Order book dynamics simulation
class OrderBookDynamics {
public:
    struct BookState {
        std::vector<std::pair<Price, Quantity>> bids;
        std::vector<std::pair<Price, Quantity>> asks;
        Price mid_price = 0;
        double spread_bps = 0.0;
        double depth = 0.0;
        uint64_t last_update_time = 0;
    };

    struct DynamicsConfig {
        double mean_reversion_speed = 0.1;
        double volatility_clustering = 0.8;
        double liquidity_regeneration_rate = 0.05;
        uint32_t max_levels = 10;
        double tick_size = 0.01;
    };

private:
    DynamicsConfig config_;
    BookState current_state_;
    std::mt19937 rng_;
    double current_volatility_ = 0.02;

public:
    explicit OrderBookDynamics(const DynamicsConfig& config = {})
        : config_(config), rng_(std::random_device{}()) {
        initialize_book();
    }

    void simulate_market_update(uint64_t timestamp_ns) {
        // Simulate price movements with mean reversion
        double price_change = generate_price_change();
        update_mid_price(price_change);
        
        // Update book levels
        regenerate_liquidity();
        
        current_state_.last_update_time = timestamp_ns;
    }

    BookState get_current_state() const { return current_state_; }

    // Simulate realistic order arrivals
    std::vector<Order> generate_market_orders(uint64_t timestamp_ns, double intensity = 0.1) {
        std::vector<Order> orders;
        std::poisson_distribution<int> arrival_dist(intensity);
        
        int num_orders = arrival_dist(rng_);
        for (int i = 0; i < num_orders; ++i) {
            Order order = create_realistic_order(timestamp_ns);
            orders.push_back(order);
        }
        
        return orders;
    }

private:
    void initialize_book() {
        Price base_price = 100000; // $100.00
        current_state_.mid_price = base_price;
        
        // Initialize bid/ask levels
        for (uint32_t i = 0; i < config_.max_levels; ++i) {
            Price bid_price = base_price - (i + 1) * static_cast<Price>(config_.tick_size * 10000);
            Price ask_price = base_price + (i + 1) * static_cast<Price>(config_.tick_size * 10000);
            
            Quantity bid_qty = 100 + i * 50;
            Quantity ask_qty = 100 + i * 50;
            
            current_state_.bids.emplace_back(bid_price, bid_qty);
            current_state_.asks.emplace_back(ask_price, ask_qty);
        }
    }

    double generate_price_change() {
        // Implement mean-reverting price process with volatility clustering
        double drift = -config_.mean_reversion_speed * 
                       (current_state_.mid_price - 100000.0) / 100000.0;
        
        // Update volatility with clustering
        current_volatility_ = current_volatility_ * config_.volatility_clustering + 
                              0.02 * (1.0 - config_.volatility_clustering);
        
        std::normal_distribution<double> noise(0.0, current_volatility_);
        return drift + noise(rng_);
    }

    void update_mid_price(double change) {
        Price new_price = static_cast<Price>(current_state_.mid_price * (1.0 + change));
        current_state_.mid_price = std::max(static_cast<Price>(1000), new_price);
    }

    void regenerate_liquidity() {
        // Simulate liquidity regeneration after trades
        for (auto& [price, qty] : current_state_.bids) {
            if (qty < 100) {
                qty = static_cast<Quantity>(qty + config_.liquidity_regeneration_rate * 100);
            }
        }
        
        for (auto& [price, qty] : current_state_.asks) {
            if (qty < 100) {
                qty = static_cast<Quantity>(qty + config_.liquidity_regeneration_rate * 100);
            }
        }
    }

    Order create_realistic_order(uint64_t timestamp_ns) {
        Order order{};
        order.id = std::uniform_int_distribution<OrderId>(1000000, 9999999)(rng_);
        order.symbol = 1;
        order.side = std::uniform_int_distribution<int>(0, 1)(rng_) ? Side::BUY : Side::SELL;
        order.type = std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < 0.8 ? 
                     OrderType::LIMIT : OrderType::MARKET;
        order.qty = std::uniform_int_distribution<Quantity>(10, 500)(rng_);
        order.user_id = 100;
        order.status = OrderStatus::NEW;
        order.tif = TimeInForce::GTC;
        
        if (order.type == OrderType::LIMIT) {
            // Price near current market
            double offset = std::normal_distribution<double>(0.0, 0.001)(rng_);
            order.price = static_cast<Price>(current_state_.mid_price * (1.0 + offset));
        }
        
        return order;
    }
};

// Comprehensive market microstructure simulator
class MarketMicrostructureSimulator {
public:
    struct SimulationConfig {
        uint64_t simulation_duration_ns = 60000000000ULL; // 1 minute
        double order_arrival_rate = 100.0; // orders per second
        uint32_t num_symbols = 5;
        bool enable_market_impact = true;
        bool enable_latency_simulation = true;
        bool enable_cross_asset_effects = true;
        std::string output_file = "microstructure_simulation.csv";
    };

    struct SimulationResults {
        uint64_t total_orders_generated = 0;
        uint64_t total_trades_executed = 0;
        double average_spread_bps = 0.0;
        double average_market_impact_bps = 0.0;
        double average_latency_ns = 0.0;
        std::vector<double> price_series;
        std::vector<double> volatility_series;
        std::vector<uint64_t> latency_distribution;
    };

private:
    SimulationConfig config_;
    std::vector<std::unique_ptr<OrderBookDynamics>> books_;
    std::unique_ptr<MarketImpactModel> impact_model_;
    std::unique_ptr<LatencySimulator> latency_sim_;
    std::atomic<bool> running_{false};

public:
    explicit MarketMicrostructureSimulator(const SimulationConfig& config = {})
        : config_(config) {
        
        // Initialize order books for each symbol
        for (uint32_t i = 0; i < config_.num_symbols; ++i) {
            books_.push_back(std::make_unique<OrderBookDynamics>());
        }
        
        impact_model_ = std::make_unique<MarketImpactModel>();
        latency_sim_ = std::make_unique<LatencySimulator>();
    }

    SimulationResults run_simulation() {
        std::cout << "Starting Market Microstructure Simulation" << std::endl;
        std::cout << "Duration: " << config_.simulation_duration_ns / 1000000000.0 << " seconds" << std::endl;
        std::cout << "Order Rate: " << config_.order_arrival_rate << " orders/sec" << std::endl;
        
        SimulationResults results;
        running_.store(true, std::memory_order_release);
        
        uint64_t start_time = now_ns();
        uint64_t current_time = start_time;
        
        while (running_.load(std::memory_order_acquire) && 
               (current_time - start_time) < config_.simulation_duration_ns) {
            
            // Update market dynamics for all symbols
            for (auto& book : books_) {
                book->simulate_market_update(current_time);
                
                // Generate market orders
                auto orders = book->generate_market_orders(current_time, 
                    config_.order_arrival_rate / 1000000000.0);
                results.total_orders_generated += orders.size();
                
                // Simulate execution with market impact
                for (const auto& order : orders) {
                    if (config_.enable_market_impact) {
                        simulate_order_execution(order, results);
                    }
                }
            }
            
            // Record metrics
            record_simulation_metrics(current_time - start_time, results);
            
            current_time = now_ns();
        }
        
        finalize_results(results);
        return results;
    }

    void stop_simulation() {
        running_.store(false, std::memory_order_release);
    }

private:
    void simulate_order_execution(const Order& order, SimulationResults& results) {
        if (config_.enable_latency_simulation) {
            uint64_t latency = latency_sim_->simulate_latency();
            results.latency_distribution.push_back(latency);
        }
        
        if (config_.enable_market_impact) {
            double impact = impact_model_->calculate_impact(order.qty, 0.1);
            results.average_market_impact_bps += impact * 10000; // Convert to bps
        }
        
        results.total_trades_executed++;
    }

    void record_simulation_metrics(uint64_t elapsed_ns, SimulationResults& results) {
        if (elapsed_ns % 1000000000ULL == 0) { // Every second
            // Record price and volatility
            if (!books_.empty()) {
                auto state = books_[0]->get_current_state();
                results.price_series.push_back(static_cast<double>(state.mid_price));
                results.volatility_series.push_back(0.02); // Simplified
            }
        }
    }

    void finalize_results(SimulationResults& results) {
        if (results.total_trades_executed > 0) {
            results.average_market_impact_bps /= results.total_trades_executed;
        }
        
        if (!results.latency_distribution.empty()) {
            double sum = std::accumulate(results.latency_distribution.begin(), 
                                       results.latency_distribution.end(), 0.0);
            results.average_latency_ns = sum / results.latency_distribution.size();
        }
        
        std::cout << "Simulation Complete:" << std::endl;
        std::cout << "  Orders Generated: " << results.total_orders_generated << std::endl;
        std::cout << "  Trades Executed: " << results.total_trades_executed << std::endl;
        std::cout << "  Avg Market Impact: " << std::fixed << std::setprecision(2) 
                  << results.average_market_impact_bps << " bps" << std::endl;
        std::cout << "  Avg Latency: " << results.average_latency_ns / 1000.0 << " μs" << std::endl;
    }
};

} // namespace hft