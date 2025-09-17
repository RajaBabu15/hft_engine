#pragma once

#include "hft/core/types.hpp"
#include "hft/matching/matching_engine.hpp"
#include <unordered_map>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>

namespace hft {
namespace analytics {

// ============================================================================
// Trade and Position Types
// ============================================================================

struct Trade {
    core::OrderID order_id;
    core::Symbol symbol;
    core::Side side;
    core::Price executed_price;
    core::Quantity quantity;
    core::TimePoint execution_time;
    core::Price market_price_at_execution;
    std::string strategy_id;
    
    // Calculated fields
    double notional_value;
    double commission;
    double slippage_bps;
    
    Trade() = default;
    Trade(core::OrderID id, const core::Symbol& sym, core::Side s, 
          core::Price price, core::Quantity qty, core::TimePoint time)
        : order_id(id), symbol(sym), side(s), executed_price(price), 
          quantity(qty), execution_time(time), notional_value(price * qty) {}
};

struct Position {
    core::Symbol symbol;
    double quantity;                    // Net position (+ long, - short)
    double avg_entry_price;            // Volume weighted average entry price
    double unrealized_pnl;             // Mark-to-market P&L
    double realized_pnl;               // Realized P&L from closed positions
    double total_pnl;                  // Total P&L
    core::TimePoint last_update_time;
    
    // Risk metrics
    double var_1_day;                  // 1-day Value at Risk
    double max_favorable_excursion;    // Best unrealized P&L seen
    double max_adverse_excursion;      // Worst unrealized P&L seen
    
    Position() : quantity(0.0), avg_entry_price(0.0), unrealized_pnl(0.0), 
                 realized_pnl(0.0), total_pnl(0.0), var_1_day(0.0),
                 max_favorable_excursion(0.0), max_adverse_excursion(0.0) {}
};

// ============================================================================
// Slippage Analysis
// ============================================================================

struct SlippageMetrics {
    double total_slippage_bps;         // Total slippage in basis points
    double avg_slippage_bps;           // Average slippage per trade
    double slippage_std_dev;           // Standard deviation of slippage
    double positive_slippage_bps;      // Favorable slippage
    double negative_slippage_bps;      // Adverse slippage
    
    // By side
    double buy_slippage_bps;
    double sell_slippage_bps;
    
    // By size buckets
    std::unordered_map<std::string, double> slippage_by_size;
    
    // By time of day
    std::vector<double> hourly_slippage;
    
    size_t trade_count;
    
    void reset() {
        total_slippage_bps = 0.0;
        avg_slippage_bps = 0.0;
        slippage_std_dev = 0.0;
        positive_slippage_bps = 0.0;
        negative_slippage_bps = 0.0;
        buy_slippage_bps = 0.0;
        sell_slippage_bps = 0.0;
        slippage_by_size.clear();
        hourly_slippage.assign(24, 0.0);
        trade_count = 0;
    }
};

// ============================================================================
// P&L Calculator
// ============================================================================

class PnLCalculator {
private:
    // Positions and trades
    std::unordered_map<core::Symbol, Position> positions_;
    std::vector<Trade> trades_;
    mutable std::mutex positions_mutex_;
    mutable std::mutex trades_mutex_;
    
    // Market data for mark-to-market
    std::unordered_map<core::Symbol, core::Price> current_prices_;
    std::unordered_map<core::Symbol, core::TimePoint> price_timestamps_;
    mutable std::mutex prices_mutex_;
    
    // Configuration
    std::unordered_map<core::Symbol, double> commission_rates_;
    double default_commission_rate_;
    
    // Statistics
    std::atomic<double> total_realized_pnl_{0.0};
    std::atomic<double> total_unrealized_pnl_{0.0};
    std::atomic<double> total_commission_{0.0};
    std::atomic<size_t> trade_count_{0};
    
public:
    explicit PnLCalculator(double default_commission_rate = 0.001); // 0.1% default
    
    // Trade recording
    void record_trade(const Trade& trade);
    void record_trade(core::OrderID order_id, const core::Symbol& symbol, 
                     core::Side side, core::Price price, core::Quantity quantity,
                     const std::string& strategy_id = "");
    
    // Market data updates
    void update_market_price(const core::Symbol& symbol, core::Price price);
    void update_market_prices(const std::unordered_map<core::Symbol, core::Price>& prices);
    
    // Position queries
    Position get_position(const core::Symbol& symbol) const;
    std::unordered_map<core::Symbol, Position> get_all_positions() const;
    double get_net_position(const core::Symbol& symbol) const;
    double get_position_value(const core::Symbol& symbol) const;
    
    // P&L queries
    double get_realized_pnl(const core::Symbol& symbol = "") const;
    double get_unrealized_pnl(const core::Symbol& symbol = "") const;
    double get_total_pnl(const core::Symbol& symbol = "") const;
    double get_total_commission() const { return total_commission_.load(); }
    
    // Trade queries
    std::vector<Trade> get_trades(const core::Symbol& symbol = "") const;
    std::vector<Trade> get_trades_in_range(core::TimePoint start, core::TimePoint end) const;
    size_t get_trade_count() const { return trade_count_.load(); }
    
    // Configuration
    void set_commission_rate(const core::Symbol& symbol, double rate);
    void set_default_commission_rate(double rate);
    
    // Reset
    void reset();
    void reset_positions();
    void reset_trades();
    
private:
    void update_position(const Trade& trade);
    void calculate_unrealized_pnl(Position& position, core::Price current_price);
    double calculate_commission(const Trade& trade);
    void update_risk_metrics(Position& position, double previous_pnl);
};

// ============================================================================
// Slippage Analyzer
// ============================================================================

class SlippageAnalyzer {
private:
    std::vector<Trade> trades_;
    SlippageMetrics metrics_;
    mutable std::mutex trades_mutex_;
    
    // Reference price sources
    enum class ReferencePrice {
        ARRIVAL_PRICE,      // Price when order was submitted
        DECISION_PRICE,     // Price when trading decision was made
        MID_PRICE,         // Mid price at execution
        VWAP,              // Volume weighted average price
        TWAP               // Time weighted average price
    };
    
    ReferencePrice reference_price_type_;
    
    // VWAP/TWAP calculation
    std::unordered_map<core::Symbol, std::vector<std::pair<core::TimePoint, core::Price>>> price_history_;
    mutable std::mutex price_history_mutex_;
    
public:
    explicit SlippageAnalyzer(ReferencePrice ref_price_type = ReferencePrice::MID_PRICE);
    
    // Trade recording
    void record_trade(const Trade& trade, core::Price reference_price);
    void record_execution(const matching::ExecutionReport& execution, 
                         core::Price arrival_price, core::Price market_mid);
    
    // Price history for VWAP/TWAP
    void update_price_history(const core::Symbol& symbol, core::Price price);
    
    // Analysis
    SlippageMetrics calculate_slippage_metrics() const;
    SlippageMetrics calculate_slippage_metrics_for_symbol(const core::Symbol& symbol) const;
    SlippageMetrics calculate_slippage_metrics_for_period(core::TimePoint start, core::TimePoint end) const;
    
    // Detailed analysis
    struct SlippageBreakdown {
        double market_impact_bps;       // Impact from order size
        double timing_cost_bps;         // Cost from execution timing
        double spread_cost_bps;         // Cost from bid-ask spread
        double opportunity_cost_bps;    // Cost from delayed execution
    };
    
    SlippageBreakdown analyze_slippage_components(const Trade& trade) const;
    
    // Configuration
    void set_reference_price_type(ReferencePrice type);
    
    // Utility
    double calculate_vwap(const core::Symbol& symbol, core::TimePoint start, core::TimePoint end) const;
    double calculate_twap(const core::Symbol& symbol, core::TimePoint start, core::TimePoint end) const;
    
    // Reset
    void reset();
    
private:
    double calculate_slippage_bps(const Trade& trade, core::Price reference_price) const;
    void update_metrics();
    std::string get_size_bucket(core::Quantity quantity) const;
    int get_hour_of_day(core::TimePoint time) const;
};

// ============================================================================
// Performance Analytics
// ============================================================================

class PerformanceAnalytics {
private:
    std::unique_ptr<PnLCalculator> pnl_calculator_;
    std::unique_ptr<SlippageAnalyzer> slippage_analyzer_;
    
    // Performance metrics
    struct PerformanceMetrics {
        // Returns
        double total_return;
        double annualized_return;
        double monthly_return;
        double daily_return;
        
        // Risk metrics
        double volatility;
        double sharpe_ratio;
        double max_drawdown;
        double var_95;
        double var_99;
        
        // Trade metrics
        size_t total_trades;
        size_t winning_trades;
        size_t losing_trades;
        double win_rate;
        double avg_win;
        double avg_loss;
        double profit_factor;
        
        // Slippage metrics
        double total_slippage_bps;
        double avg_slippage_bps;
        
        void reset() {
            total_return = 0.0;
            annualized_return = 0.0;
            monthly_return = 0.0;
            daily_return = 0.0;
            volatility = 0.0;
            sharpe_ratio = 0.0;
            max_drawdown = 0.0;
            var_95 = 0.0;
            var_99 = 0.0;
            total_trades = 0;
            winning_trades = 0;
            losing_trades = 0;
            win_rate = 0.0;
            avg_win = 0.0;
            avg_loss = 0.0;
            profit_factor = 0.0;
            total_slippage_bps = 0.0;
            avg_slippage_bps = 0.0;
        }
    };
    
    PerformanceMetrics metrics_;
    
    // Daily P&L history for calculations
    std::vector<std::pair<core::TimePoint, double>> daily_pnl_;
    mutable std::mutex daily_pnl_mutex_;
    
public:
    PerformanceAnalytics(double initial_capital = 1000000.0, double commission_rate = 0.001);
    
    // Data input
    void record_trade(const Trade& trade, core::Price reference_price = 0.0);
    void record_execution(const matching::ExecutionReport& execution, 
                         core::Price arrival_price, core::Price market_mid);
    void update_market_prices(const std::unordered_map<core::Symbol, core::Price>& prices);
    
    // Performance calculation
    PerformanceMetrics calculate_performance_metrics() const;
    PerformanceMetrics calculate_performance_metrics_for_period(core::TimePoint start, core::TimePoint end) const;
    
    // Specific metrics
    double calculate_sharpe_ratio(double risk_free_rate = 0.02) const;
    double calculate_max_drawdown() const;
    double calculate_var(double confidence_level = 0.95) const;
    double calculate_volatility() const;
    
    // Access to components
    PnLCalculator* get_pnl_calculator() { return pnl_calculator_.get(); }
    SlippageAnalyzer* get_slippage_analyzer() { return slippage_analyzer_.get(); }
    
    // Reporting
    void print_performance_summary() const;
    void export_performance_report(const std::string& filename) const;
    void export_trade_log(const std::string& filename) const;
    
    // Reset
    void reset();
    
private:
    void update_daily_pnl();
    std::vector<double> get_daily_returns() const;
    double calculate_return_volatility(const std::vector<double>& returns) const;
};

// ============================================================================
// Real-time Performance Monitor
// ============================================================================

class RealTimePerformanceMonitor {
private:
    std::unique_ptr<PerformanceAnalytics> analytics_;
    
    // Real-time tracking
    std::atomic<double> current_pnl_{0.0};
    std::atomic<double> current_drawdown_{0.0};
    std::atomic<double> peak_pnl_{0.0};
    std::atomic<size_t> trades_today_{0};
    
    // Alert thresholds
    double max_drawdown_alert_;
    double min_daily_pnl_alert_;
    double max_position_size_alert_;
    
    // Callbacks
    std::function<void(const std::string&)> alert_callback_;
    
    // Monitoring thread
    std::atomic<bool> monitoring_{false};
    std::thread monitor_thread_;
    
public:
    explicit RealTimePerformanceMonitor(double initial_capital = 1000000.0);
    ~RealTimePerformanceMonitor();
    
    // Control
    void start_monitoring();
    void stop_monitoring();
    
    // Data input
    void record_trade(const Trade& trade, core::Price reference_price = 0.0);
    void update_market_prices(const std::unordered_map<core::Symbol, core::Price>& prices);
    
    // Real-time metrics
    double get_current_pnl() const { return current_pnl_.load(); }
    double get_current_drawdown() const { return current_drawdown_.load(); }
    size_t get_trades_today() const { return trades_today_.load(); }
    
    // Alert configuration
    void set_alert_callback(std::function<void(const std::string&)> callback);
    void set_drawdown_alert(double max_drawdown);
    void set_daily_pnl_alert(double min_pnl);
    void set_position_size_alert(double max_size);
    
    // Access to analytics
    PerformanceAnalytics* get_analytics() { return analytics_.get(); }
    
private:
    void monitor_worker();
    void check_alerts();
    void trigger_alert(const std::string& message);
    void update_real_time_metrics();
};

// ============================================================================
// Utility Functions
// ============================================================================

// P&L calculations
double calculate_pnl(core::Side side, core::Price entry_price, core::Price exit_price, core::Quantity quantity);
double calculate_return(double initial_value, double final_value);
double calculate_annualized_return(double total_return, int days);

// Slippage calculations  
double calculate_slippage_bps(core::Price execution_price, core::Price reference_price, core::Side side);
double calculate_market_impact(core::Quantity order_size, core::Quantity average_daily_volume);

// Risk calculations
double calculate_var(const std::vector<double>& returns, double confidence_level);
double calculate_expected_shortfall(const std::vector<double>& returns, double confidence_level);
double calculate_beta(const std::vector<double>& strategy_returns, const std::vector<double>& benchmark_returns);

// Statistical utilities
double calculate_mean(const std::vector<double>& values);
double calculate_std_dev(const std::vector<double>& values);
double calculate_correlation(const std::vector<double>& x, const std::vector<double>& y);
std::pair<double, double> calculate_linear_regression(const std::vector<double>& x, const std::vector<double>& y);

} // namespace analytics
} // namespace hft