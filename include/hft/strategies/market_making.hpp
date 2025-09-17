#pragma once

#include "hft/core/types.hpp"
#include "hft/core/clock.hpp"
#include "hft/matching/matching_engine.hpp"
#include "hft/backtesting/tick_replay.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>

namespace hft {
namespace strategies {

// ============================================================================
// Position and P&L Tracking
// ============================================================================

struct Position {
    core::Symbol symbol;
    double quantity;                    // Net position (+ve long, -ve short)
    double avg_cost_basis;             // Volume weighted average cost
    double realized_pnl;               // Realized profit/loss
    double unrealized_pnl;             // Mark-to-market unrealized P&L
    core::TimePoint last_update_time;
    
    // Transaction history for detailed analysis
    std::vector<core::Trade> trades;
    
    Position() : quantity(0.0), avg_cost_basis(0.0), realized_pnl(0.0), 
                unrealized_pnl(0.0) {}
    
    Position(const core::Symbol& sym) : symbol(sym), quantity(0.0), avg_cost_basis(0.0),
                                       realized_pnl(0.0), unrealized_pnl(0.0) {}
    
    double get_total_pnl() const { return realized_pnl + unrealized_pnl; }
    bool is_flat() const { return std::abs(quantity) < 1e-6; }
    bool is_long() const { return quantity > 1e-6; }
    bool is_short() const { return quantity < -1e-6; }
    
    void update_unrealized_pnl(core::Price mark_price) {
        if (is_flat()) {
            unrealized_pnl = 0.0;
        } else {
            unrealized_pnl = quantity * (mark_price - avg_cost_basis);
        }
        last_update_time = core::HighResolutionClock::now();
    }
};

struct SlippageMetrics {
    double total_slippage_bps;         // Total slippage in basis points
    double avg_slippage_per_trade_bps; // Average slippage per trade
    double market_impact_bps;          // Market impact component
    double timing_slippage_bps;        // Timing delay component
    uint64_t trade_count;              // Number of trades measured
    
    SlippageMetrics() : total_slippage_bps(0.0), avg_slippage_per_trade_bps(0.0),
                       market_impact_bps(0.0), timing_slippage_bps(0.0), trade_count(0) {}
    
    void add_trade_slippage(double slippage_bps, double market_impact_bps = 0.0) {
        total_slippage_bps += slippage_bps;
        market_impact_bps += market_impact_bps;
        trade_count++;
        avg_slippage_per_trade_bps = total_slippage_bps / trade_count;
    }
};

struct QueueingMetrics {
    std::atomic<uint64_t> orders_queued{0};      // Orders waiting in queue
    std::atomic<uint64_t> orders_filled{0};      // Orders successfully filled
    std::atomic<uint64_t> orders_cancelled{0};   // Orders cancelled while queued
    std::atomic<double> avg_queue_time_ms{0.0};  // Average time in queue
    std::atomic<double> fill_probability{0.0};   // Historical fill probability
    std::atomic<size_t> current_queue_depth{0};  // Current queue depth
    
    // Adverse selection metrics
    std::atomic<double> adverse_selection_cost_bps{0.0};
    std::atomic<uint64_t> picked_off_orders{0}; // Orders filled at bad prices
    
    double get_fill_rate() const {
        uint64_t total = orders_queued.load();
        return total > 0 ? static_cast<double>(orders_filled.load()) / total : 0.0;
    }
    
    double get_cancel_rate() const {
        uint64_t total = orders_queued.load();
        return total > 0 ? static_cast<double>(orders_cancelled.load()) / total : 0.0;
    }
};

// ============================================================================
// Market Making Strategy Base Class
// ============================================================================

class MarketMakingStrategy {
public:
    struct Parameters {
        double target_spread_bps;          // Target bid-ask spread
        core::Quantity default_quote_size; // Default quote size
        double max_position_limit;         // Maximum position size
        double inventory_penalty_factor;   // Position skewing factor
        double risk_aversion;              // Risk aversion parameter
        
        // Queue management
        double min_fill_probability;       // Minimum fill probability to quote
        double max_adverse_selection_bps;  // Max adverse selection tolerance
        
        // Latency requirements
        double max_quote_latency_us;       // Maximum quote update latency
        double max_cancel_latency_us;      // Maximum order cancel latency
        
        Parameters() : target_spread_bps(5.0), default_quote_size(100), 
                      max_position_limit(10000.0), inventory_penalty_factor(0.1),
                      risk_aversion(1.0), min_fill_probability(0.3),
                      max_adverse_selection_bps(2.0), max_quote_latency_us(50.0),
                      max_cancel_latency_us(25.0) {}
    };
    
protected:
    Parameters params_;
    std::unordered_map<core::Symbol, Position> positions_;
    std::unordered_map<core::Symbol, SlippageMetrics> slippage_metrics_;
    std::unordered_map<core::Symbol, QueueingMetrics> queueing_metrics_;
    
    // Current market state
    std::unordered_map<core::Symbol, core::MarketDataTick> current_market_data_;
    std::unordered_map<core::Symbol, std::pair<core::OrderID, core::OrderID>> active_quotes_; // bid, ask
    
    // Strategy state
    std::atomic<bool> running_{false};
    std::atomic<core::OrderID> next_order_id_{1};
    std::mutex strategy_mutex_;
    
    // Performance tracking
    std::atomic<double> total_realized_pnl_{0.0};
    std::atomic<double> total_unrealized_pnl_{0.0};
    std::atomic<uint64_t> total_trades_{0};
    std::atomic<double> total_volume_{0.0};
    
    // Callbacks to matching engine
    std::function<bool(const order::Order&)> submit_order_callback_;
    std::function<bool(core::OrderID)> cancel_order_callback_;
    
public:
    explicit MarketMakingStrategy(const Parameters& params = Parameters{});
    virtual ~MarketMakingStrategy() = default;
    
    // Strategy lifecycle
    virtual void start() { running_.store(true); }
    virtual void stop() { running_.store(false); }
    bool is_running() const { return running_.load(); }
    
    // Set callbacks to matching engine
    void set_order_callbacks(
        std::function<bool(const order::Order&)> submit_callback,
        std::function<bool(core::OrderID)> cancel_callback
    );
    
    // Market data and execution event handlers
    virtual void on_market_data(const core::MarketDataTick& tick) = 0;
    virtual void on_trade_execution(const matching::ExecutionReport& execution);
    virtual void on_order_cancelled(core::OrderID order_id, const core::Symbol& symbol);
    
    // Strategy interface - implemented by derived classes
    virtual void update_quotes(const core::Symbol& symbol) = 0;
    virtual bool should_quote(const core::Symbol& symbol) const = 0;
    virtual std::pair<core::Price, core::Price> calculate_quote_prices(
        const core::Symbol& symbol, core::Price reference_price) = 0;
    virtual core::Quantity calculate_quote_size(
        const core::Symbol& symbol, core::Side side) = 0;
    
    // Position and P&L access
    const Position& get_position(const core::Symbol& symbol) const;
    double get_total_pnl() const;
    double get_realized_pnl() const { return total_realized_pnl_.load(); }
    double get_unrealized_pnl() const { return total_unrealized_pnl_.load(); }
    
    // Performance metrics
    const SlippageMetrics& get_slippage_metrics(const core::Symbol& symbol) const;
    const QueueingMetrics& get_queueing_metrics(const core::Symbol& symbol) const;
    
    // Strategy statistics
    struct StrategyStats {
        double total_pnl;
        double realized_pnl;
        double unrealized_pnl;
        double sharpe_ratio;
        double max_drawdown;
        uint64_t total_trades;
        double total_volume;
        double avg_holding_period_ms;
        double inventory_turnover;
        
        // Risk metrics
        double var_95;              // 95% Value at Risk
        double expected_shortfall;  // Conditional VaR
        double max_position_size;   // Largest position held
        
        // Execution quality
        double avg_effective_spread_captured_bps;
        double avg_slippage_bps;
        double fill_rate;
        double adverse_selection_cost_bps;
    };
    
    StrategyStats get_strategy_stats() const;
    void reset_statistics();
    
    // Configuration
    void set_parameters(const Parameters& params) { params_ = params; }
    const Parameters& get_parameters() const { return params_; }
    
protected:
    // Position management helpers
    void update_position(const core::Symbol& symbol, const matching::Fill& fill);
    void update_unrealized_pnl(const core::Symbol& symbol, core::Price mark_price);
    
    // Order management helpers
    bool submit_quote_order(const core::Symbol& symbol, core::Side side, 
                           core::Price price, core::Quantity quantity);
    void cancel_existing_quotes(const core::Symbol& symbol);
    core::OrderID generate_order_id();
    
    // Risk management
    virtual bool passes_risk_checks(const core::Symbol& symbol, 
                                  core::Side side, core::Quantity quantity) const;
    double calculate_position_risk(const core::Symbol& symbol) const;
    
    // Metrics calculation
    void update_slippage_metrics(const core::Symbol& symbol, 
                                core::Price expected_price, core::Price actual_price,
                                core::Quantity quantity);
    void update_queueing_metrics(const core::Symbol& symbol, const matching::ExecutionReport& report);
    
    // Utility functions
    core::Price get_mid_price(const core::Symbol& symbol) const;
    core::Price get_theoretical_value(const core::Symbol& symbol) const;
    double calculate_inventory_skew(const core::Symbol& symbol) const;
};

// ============================================================================
// Simple Market Making Strategy
// ============================================================================

class SimpleMarketMaker : public MarketMakingStrategy {
private:
    // Simple parameters
    double half_spread_bps_;
    bool symmetric_quotes_;
    
public:
    explicit SimpleMarketMaker(double half_spread_bps = 2.5, bool symmetric = true);
    
    // MarketMakingStrategy interface
    void on_market_data(const core::MarketDataTick& tick) override;
    void update_quotes(const core::Symbol& symbol) override;
    bool should_quote(const core::Symbol& symbol) const override;
    std::pair<core::Price, core::Price> calculate_quote_prices(
        const core::Symbol& symbol, core::Price reference_price) override;
    core::Quantity calculate_quote_size(
        const core::Symbol& symbol, core::Side side) override;
    
private:
    bool is_market_stable(const core::Symbol& symbol) const;
};

// ============================================================================
// Advanced Market Making with Inventory Management
// ============================================================================

class AdvancedMarketMaker : public MarketMakingStrategy {
private:
    // Advanced parameters
    double volatility_multiplier_;
    double momentum_threshold_;
    double mean_reversion_strength_;
    
    // Market microstructure models
    std::unordered_map<core::Symbol, double> estimated_volatility_;
    std::unordered_map<core::Symbol, double> order_flow_imbalance_;
    std::unordered_map<core::Symbol, std::vector<core::Price>> price_history_;
    
    // Queue position modeling
    std::unordered_map<core::Symbol, double> estimated_queue_position_;
    std::unordered_map<core::Symbol, double> fill_probability_model_;
    
public:
    explicit AdvancedMarketMaker(const Parameters& params = Parameters{});
    
    // MarketMakingStrategy interface
    void on_market_data(const core::MarketDataTick& tick) override;
    void update_quotes(const core::Symbol& symbol) override;
    bool should_quote(const core::Symbol& symbol) const override;
    std::pair<core::Price, core::Price> calculate_quote_prices(
        const core::Symbol& symbol, core::Price reference_price) override;
    core::Quantity calculate_quote_size(
        const core::Symbol& symbol, core::Side side) override;
    
    // Advanced features
    void update_volatility_estimate(const core::Symbol& symbol);
    void update_order_flow_model(const core::Symbol& symbol);
    void update_queue_position_model(const core::Symbol& symbol);
    
private:
    double calculate_optimal_spread(const core::Symbol& symbol) const;
    double calculate_adverse_selection_cost(const core::Symbol& symbol) const;
    double estimate_fill_probability(const core::Symbol& symbol, core::Side side, 
                                   core::Price price) const;
    core::Price calculate_fair_value(const core::Symbol& symbol) const;
    
    // Market microstructure models
    double calculate_volatility(const std::vector<core::Price>& prices) const;
    double calculate_order_flow_imbalance(const core::Symbol& symbol) const;
    double calculate_momentum(const core::Symbol& symbol) const;
    double calculate_mean_reversion_signal(const core::Symbol& symbol) const;
};

// ============================================================================
// Statistical Arbitrage Market Maker
// ============================================================================

class StatArbitrageMaker : public MarketMakingStrategy {
private:
    // Statistical models
    std::vector<core::Symbol> symbol_universe_;
    std::unordered_map<std::string, double> correlation_matrix_;
    std::unordered_map<core::Symbol, double> beta_coefficients_;
    
    // Cointegration and pairs trading
    double cointegration_threshold_;
    double half_life_mean_reversion_;
    
public:
    explicit StatArbitrageMaker(const std::vector<core::Symbol>& universe,
                               const Parameters& params = Parameters{});
    
    // MarketMakingStrategy interface
    void on_market_data(const core::MarketDataTick& tick) override;
    void update_quotes(const core::Symbol& symbol) override;
    bool should_quote(const core::Symbol& symbol) const override;
    std::pair<core::Price, core::Price> calculate_quote_prices(
        const core::Symbol& symbol, core::Price reference_price) override;
    core::Quantity calculate_quote_size(
        const core::Symbol& symbol, core::Side side) override;
    
    // Statistical arbitrage specific
    void update_correlation_matrix();
    void update_beta_coefficients(const core::Symbol& symbol);
    double calculate_residual_alpha(const core::Symbol& symbol) const;
    
private:
    core::Price calculate_statistical_fair_value(const core::Symbol& symbol) const;
    double get_correlation(const core::Symbol& sym1, const core::Symbol& sym2) const;
    std::vector<core::Symbol> find_cointegrated_pairs(const core::Symbol& symbol) const;
};

// ============================================================================
// Strategy Factory
// ============================================================================

class StrategyFactory {
public:
    enum class StrategyType {
        SIMPLE_MARKET_MAKER,
        ADVANCED_MARKET_MAKER,
        STATISTICAL_ARBITRAGE,
        MOMENTUM_MARKET_MAKER,
        MEAN_REVERSION_MARKET_MAKER
    };
    
    static std::unique_ptr<MarketMakingStrategy> create_strategy(
        StrategyType type, 
        const MarketMakingStrategy::Parameters& params = MarketMakingStrategy::Parameters{}
    );
    
    static std::unique_ptr<MarketMakingStrategy> create_optimized_strategy(
        const std::vector<core::Symbol>& symbols,
        const backtesting::BacktestMetrics& historical_performance
    );
};

// ============================================================================
// Strategy Backtester
// ============================================================================

class StrategyBacktester {
private:
    std::unique_ptr<MarketMakingStrategy> strategy_;
    std::unique_ptr<backtesting::TickReplayEngine> replay_engine_;
    
    // Backtest results
    backtesting::BacktestMetrics metrics_;
    std::vector<double> pnl_curve_;
    std::vector<core::TimePoint> timestamps_;
    
    // Execution simulation
    std::unique_ptr<matching::MatchingEngine> matching_engine_;
    
public:
    explicit StrategyBacktester(
        std::unique_ptr<MarketMakingStrategy> strategy,
        std::unique_ptr<backtesting::TickReplayEngine> replay_engine
    );
    
    // Run backtest
    backtesting::BacktestMetrics run_backtest(
        const core::TimePoint& start_time,
        const core::TimePoint& end_time
    );
    
    // Advanced backtesting
    backtesting::BacktestMetrics run_walk_forward_analysis(
        const core::TimePoint& start_time,
        const core::TimePoint& end_time,
        std::chrono::seconds window_size,
        std::chrono::seconds step_size
    );
    
    // Performance analysis
    void generate_performance_report(const std::string& filename) const;
    void plot_pnl_curve(const std::string& filename) const;
    void analyze_trade_distribution() const;
    
    // Optimization
    MarketMakingStrategy::Parameters optimize_parameters(
        const std::vector<MarketMakingStrategy::Parameters>& parameter_space,
        const core::TimePoint& start_time,
        const core::TimePoint& end_time
    );
    
private:
    void setup_matching_engine();
    void connect_strategy_to_engine();
    double calculate_fitness_score(const backtesting::BacktestMetrics& metrics) const;
};

// ============================================================================
// Utility Functions
// ============================================================================

// Position utilities
double calculate_portfolio_var(const std::unordered_map<core::Symbol, Position>& positions,
                              double confidence_level = 0.95);
double calculate_sharpe_ratio(const std::vector<double>& returns, double risk_free_rate = 0.0);
double calculate_maximum_drawdown(const std::vector<double>& pnl_curve);

// Market microstructure utilities  
double calculate_effective_spread(core::Price bid, core::Price ask, core::Price mid);
double calculate_realized_spread(core::Price transaction_price, core::Price mid_price_after,
                                core::Side side);
double calculate_price_impact(core::Price price_before, core::Price price_after,
                             core::Quantity traded_quantity);

// Strategy analysis utilities
void analyze_strategy_performance(const MarketMakingStrategy::StrategyStats& stats);
void compare_strategy_performance(const std::vector<MarketMakingStrategy::StrategyStats>& stats_vector);
void export_trade_analysis(const std::vector<core::Trade>& trades, const std::string& filename);

} // namespace strategies  
} // namespace hft