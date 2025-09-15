#pragma once

#include "hft/types.h"
#include "hft/lockfree_queue.h"
#include <random>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

namespace hft {

// Market regime types
enum class MarketRegime : uint8_t {
    STABLE,      // Low volatility, tight spreads
    VOLATILE,    // High volatility, wide spreads
    TRENDING,    // Persistent directional movement
    CHOPPY,      // High frequency mean reversion
    OPENING,     // Market opening dynamics
    CLOSING      // Market closing dynamics
};

// Price impact model for realistic market simulation
class PriceImpactModel {
private:
    double permanent_impact_factor_;
    double temporary_impact_factor_;
    double volatility_;
    
public:
    struct ImpactParameters {
        double permanent_factor = 0.1;    // Permanent impact per unit volume
        double temporary_factor = 0.5;    // Temporary impact per unit volume
        double volatility = 0.02;         // Price volatility
    };
    
    PriceImpactModel(const ImpactParameters& params)
        : permanent_impact_factor_(params.permanent_factor)
        , temporary_impact_factor_(params.temporary_factor)
        , volatility_(params.volatility)
    {}
    
    PriceImpactModel() : PriceImpactModel(ImpactParameters{}) {}
    
    // Calculate price impact for a trade
    struct Impact {
        double permanent_impact;
        double temporary_impact;
        double total_impact;
    };
    
    Impact calculate_impact(Price current_price, Quantity trade_size, Side side) const {
        double relative_size = static_cast<double>(trade_size) / 1000.0; // Normalize
        double direction = (side == Side::BUY) ? 1.0 : -1.0;
        
        Impact impact;
        impact.permanent_impact = direction * permanent_impact_factor_ * 
                                std::sqrt(relative_size) * current_price;
        impact.temporary_impact = direction * temporary_impact_factor_ * 
                                relative_size * current_price;
        impact.total_impact = impact.permanent_impact + impact.temporary_impact;
        
        return impact;
    }
    
    void set_volatility(double vol) { volatility_ = vol; }
    double get_volatility() const { return volatility_; }
};

// Random walk generator for realistic price movements
class RandomWalkGenerator {
private:
    std::mt19937_64 rng_;
    std::normal_distribution<double> normal_dist_;
    std::uniform_real_distribution<double> uniform_dist_;
    
    double drift_;
    double volatility_;
    double mean_reversion_speed_;
    double long_term_mean_;
    
public:
    struct Parameters {
        double drift = 0.0;                    // Trend component
        double volatility = 0.01;              // Random volatility
        double mean_reversion_speed = 0.1;     // Speed of mean reversion
        double long_term_mean = 50000.0;       // Long-term price mean
        uint64_t seed = 12345;                 // Random seed
    };
    
    RandomWalkGenerator(const Parameters& params)
        : rng_(params.seed)
        , normal_dist_(0.0, 1.0)
        , uniform_dist_(0.0, 1.0)
        , drift_(params.drift)
        , volatility_(params.volatility)
        , mean_reversion_speed_(params.mean_reversion_speed)
        , long_term_mean_(params.long_term_mean)
    {}
    
    RandomWalkGenerator() : RandomWalkGenerator(Parameters{}) {}
    
    // Generate next price step
    Price generate_next_price(Price current_price, double dt = 1.0) {
        double price_d = static_cast<double>(current_price);
        
        // Brownian motion component
        double random_shock = normal_dist_(rng_) * volatility_ * std::sqrt(dt);
        
        // Mean reversion component
        double mean_revert = mean_reversion_speed_ * (long_term_mean_ - price_d) * dt;
        
        // Trend component
        double trend = drift_ * dt;
        
        // Combined price change
        double price_change = trend + random_shock + mean_revert;
        double new_price = price_d + price_change;
        
        // Ensure price stays positive
        return static_cast<Price>(std::max(new_price, 1.0));
    }
    
    // Generate price with jump process
    Price generate_jump_price(Price current_price, double jump_intensity = 0.01) {
        double price_d = static_cast<double>(current_price);
        
        // Check for jump
        if (uniform_dist_(rng_) < jump_intensity) {
            // Jump occurs
            double jump_size = normal_dist_(rng_) * volatility_ * 5.0; // Larger jumps
            price_d += jump_size;
        }
        
        return generate_next_price(static_cast<Price>(price_d));
    }
    
    void set_regime_parameters(MarketRegime regime) {
        switch (regime) {
            case MarketRegime::STABLE:
                volatility_ = 0.005;
                drift_ = 0.0;
                mean_reversion_speed_ = 0.2;
                break;
            case MarketRegime::VOLATILE:
                volatility_ = 0.03;
                drift_ = 0.0;
                mean_reversion_speed_ = 0.05;
                break;
            case MarketRegime::TRENDING:
                volatility_ = 0.015;
                drift_ = 0.001;
                mean_reversion_speed_ = 0.01;
                break;
            case MarketRegime::CHOPPY:
                volatility_ = 0.02;
                drift_ = 0.0;
                mean_reversion_speed_ = 0.5;
                break;
            case MarketRegime::OPENING:
                volatility_ = 0.025;
                drift_ = 0.0005;
                mean_reversion_speed_ = 0.1;
                break;
            case MarketRegime::CLOSING:
                volatility_ = 0.02;
                drift_ = -0.0002;
                mean_reversion_speed_ = 0.15;
                break;
        }
    }
};

// Volume dynamics generator
class VolumeGenerator {
private:
    std::mt19937_64 rng_;
    std::exponential_distribution<double> exp_dist_;
    std::poisson_distribution<int> poisson_dist_;
    
    double base_arrival_rate_;
    double volume_intensity_;
    
public:
    VolumeGenerator(double arrival_rate = 1000.0, double intensity = 100.0, uint64_t seed = 54321)
        : rng_(seed)
        , exp_dist_(arrival_rate)
        , poisson_dist_(static_cast<int>(intensity))
        , base_arrival_rate_(arrival_rate)
        , volume_intensity_(intensity)
    {}
    
    // Generate next order arrival time (nanoseconds)
    uint64_t next_arrival_time_ns() {
        double dt_seconds = exp_dist_(rng_);
        return static_cast<uint64_t>(dt_seconds * 1e9);
    }
    
    // Generate order size
    Quantity generate_order_size() {
        int size = poisson_dist_(rng_);
        return std::max(1, size * 10); // Scale to reasonable order sizes
    }
    
    // Generate side bias (probability of buy order)
    Side generate_side(double buy_probability = 0.5) {
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        return (uniform(rng_) < buy_probability) ? Side::BUY : Side::SELL;
    }
    
    void set_regime_parameters(MarketRegime regime) {
        switch (regime) {
            case MarketRegime::STABLE:
                base_arrival_rate_ = 800.0;
                volume_intensity_ = 80.0;
                break;
            case MarketRegime::VOLATILE:
                base_arrival_rate_ = 2000.0;
                volume_intensity_ = 150.0;
                break;
            case MarketRegime::TRENDING:
                base_arrival_rate_ = 1200.0;
                volume_intensity_ = 120.0;
                break;
            case MarketRegime::CHOPPY:
                base_arrival_rate_ = 1500.0;
                volume_intensity_ = 90.0;
                break;
            case MarketRegime::OPENING:
                base_arrival_rate_ = 3000.0;
                volume_intensity_ = 200.0;
                break;
            case MarketRegime::CLOSING:
                base_arrival_rate_ = 2500.0;
                volume_intensity_ = 180.0;
                break;
        }
        
        exp_dist_ = std::exponential_distribution<double>(base_arrival_rate_);
        poisson_dist_ = std::poisson_distribution<int>(static_cast<int>(volume_intensity_));
    }
};

// Market microstructure simulator
class MarketSimulator {
private:
    Symbol symbol_id_;
    Price current_bid_;
    Price current_ask_;
    MarketRegime current_regime_;
    
    std::unique_ptr<PriceImpactModel> impact_model_;
    std::unique_ptr<RandomWalkGenerator> price_generator_;
    std::unique_ptr<VolumeGenerator> volume_generator_;
    
    // Simulation thread
    std::thread simulation_thread_;
    std::atomic<bool> running_{false};
    
    // Output queues
    MarketDataQueue market_data_queue_;
    
    // Simulation parameters
    uint64_t tick_interval_ns_;
    std::atomic<uint64_t> simulation_time_{0};
    
    void simulation_loop() {
        auto next_tick_time = std::chrono::high_resolution_clock::now();
        
        while (running_.load(std::memory_order_relaxed)) {
            uint64_t current_sim_time = simulation_time_.load(std::memory_order_relaxed);
            
            // Generate market data update
            generate_market_tick(current_sim_time);
            
            // Sleep until next tick
            next_tick_time += std::chrono::nanoseconds(tick_interval_ns_);
            std::this_thread::sleep_until(next_tick_time);
            
            simulation_time_.fetch_add(tick_interval_ns_, std::memory_order_relaxed);
        }
    }
    
    void generate_market_tick(uint64_t timestamp) {
        // Update prices based on random walk
        Price mid_price = (current_bid_ + current_ask_) / 2;
        Price new_mid = price_generator_->generate_next_price(mid_price);
        
        // Calculate spread based on regime
        Price spread = calculate_spread();
        current_bid_ = new_mid - spread / 2;
        current_ask_ = new_mid + spread / 2;
        
        // Ensure minimum tick size and positive prices
        current_bid_ = std::max(current_bid_, static_cast<Price>(1));
        current_ask_ = std::max(current_ask_, current_bid_ + 1);
        
        // Generate market data updates
        generate_level_updates(timestamp);
        
        // Occasionally generate trades
        if (volume_generator_->next_arrival_time_ns() < tick_interval_ns_) {
            generate_trade_update(timestamp);
        }
    }
    
    Price calculate_spread() const {
        Price base_spread = 2; // Minimum 2 tick spread
        
        switch (current_regime_) {
            case MarketRegime::STABLE:
                return base_spread;
            case MarketRegime::VOLATILE:
                return base_spread * 3;
            case MarketRegime::TRENDING:
                return base_spread * 2;
            case MarketRegime::CHOPPY:
                return base_spread * 4;
            case MarketRegime::OPENING:
                return base_spread * 5;
            case MarketRegime::CLOSING:
                return base_spread * 3;
        }
        return base_spread;
    }
    
    void generate_level_updates(uint64_t timestamp) {
        // Generate bid update
        MarketDataUpdate bid_update;
        bid_update.symbol_id = symbol_id_;
        bid_update.timestamp = timestamp;
        bid_update.price = current_bid_;
        bid_update.quantity = volume_generator_->generate_order_size();
        bid_update.side = 0; // Bid
        bid_update.update_type = 1; // Modify
        
        market_data_queue_.try_enqueue(bid_update);
        
        // Generate ask update
        MarketDataUpdate ask_update;
        ask_update.symbol_id = symbol_id_;
        ask_update.timestamp = timestamp;
        ask_update.price = current_ask_;
        ask_update.quantity = volume_generator_->generate_order_size();
        ask_update.side = 1; // Ask
        ask_update.update_type = 1; // Modify
        
        market_data_queue_.try_enqueue(ask_update);
    }
    
    void generate_trade_update(uint64_t timestamp) {
        Side trade_side = volume_generator_->generate_side();
        Price trade_price = (trade_side == Side::BUY) ? current_ask_ : current_bid_;
        Quantity trade_size = volume_generator_->generate_order_size();
        
        // Apply price impact
        auto impact = impact_model_->calculate_impact(trade_price, trade_size, trade_side);
        Price new_mid = (current_bid_ + current_ask_) / 2 + static_cast<Price>(impact.permanent_impact);
        
        // Update prices with impact
        Price spread = current_ask_ - current_bid_;
        current_bid_ = new_mid - spread / 2;
        current_ask_ = new_mid + spread / 2;
        
        // Generate trade report
        MarketDataUpdate trade_update;
        trade_update.symbol_id = symbol_id_;
        trade_update.timestamp = timestamp;
        trade_update.price = trade_price;
        trade_update.quantity = trade_size;
        trade_update.side = static_cast<uint8_t>(trade_side);
        trade_update.update_type = 3; // Trade
        
        market_data_queue_.try_enqueue(trade_update);
    }

public:
    struct Config {
        Symbol symbol_id = 1;
        Price initial_bid = 49999;
        Price initial_ask = 50001;
        MarketRegime regime = MarketRegime::STABLE;
        uint64_t tick_interval_ns = 1000000; // 1ms
    };
    
    MarketSimulator(const Config& config)
        : symbol_id_(config.symbol_id)
        , current_bid_(config.initial_bid)
        , current_ask_(config.initial_ask)
        , current_regime_(config.regime)
        , tick_interval_ns_(config.tick_interval_ns)
    {
        impact_model_ = std::make_unique<PriceImpactModel>();
        price_generator_ = std::make_unique<RandomWalkGenerator>();
        volume_generator_ = std::make_unique<VolumeGenerator>();
        
        set_market_regime(config.regime);
    }
    
    MarketSimulator() : MarketSimulator(Config{}) {}
    
    ~MarketSimulator() {
        stop_simulation();
    }
    
    void start_simulation() {
        if (running_.load(std::memory_order_relaxed)) return;
        
        running_.store(true, std::memory_order_relaxed);
        simulation_time_.store(0, std::memory_order_relaxed);
        simulation_thread_ = std::thread(&MarketSimulator::simulation_loop, this);
    }
    
    void stop_simulation() {
        running_.store(false, std::memory_order_relaxed);
        if (simulation_thread_.joinable()) {
            simulation_thread_.join();
        }
    }
    
    bool get_market_update(MarketDataUpdate& update) {
        return market_data_queue_.try_dequeue(update);
    }
    
    void set_market_regime(MarketRegime regime) {
        current_regime_ = regime;
        price_generator_->set_regime_parameters(regime);
        volume_generator_->set_regime_parameters(regime);
    }
    
    // Get current market state
    struct MarketState {
        Price current_bid;
        Price current_ask;
        MarketRegime regime;
        uint64_t simulation_time_ns;
        size_t pending_updates;
    };
    
    MarketState get_market_state() const {
        MarketState state;
        state.current_bid = current_bid_;
        state.current_ask = current_ask_;
        state.regime = current_regime_;
        state.simulation_time_ns = simulation_time_.load(std::memory_order_relaxed);
        state.pending_updates = market_data_queue_.size();
        return state;
    }
    
    // Manual market shock for testing
    void apply_market_shock(double price_change_percent, double volatility_multiplier = 2.0) {
        Price mid_price = (current_bid_ + current_ask_) / 2;
        Price price_change = static_cast<Price>(mid_price * price_change_percent / 100.0);
        
        current_bid_ += price_change;
        current_ask_ += price_change;
        
        // Temporarily increase volatility
        PriceImpactModel::ImpactParameters params;
        params.volatility = impact_model_->get_volatility() * volatility_multiplier;
        impact_model_ = std::make_unique<PriceImpactModel>(params);
    }
};

} // namespace hft