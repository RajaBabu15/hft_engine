#pragma once

#include "hft/core/types.hpp"
#include "hft/core/clock.hpp"
#include "hft/core/lock_free_queue.hpp"
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <fstream>
#include <chrono>
#include <random>

namespace hft {
namespace backtesting {

// ============================================================================
// Tick Data Structures
// ============================================================================

struct HistoricalTick {
    core::TimePoint timestamp;
    core::Symbol symbol;
    core::Price bid_price;
    core::Price ask_price;
    core::Quantity bid_size;
    core::Quantity ask_size;
    core::Price last_price;
    core::Quantity last_size;
    uint64_t sequence_number;
    
    HistoricalTick() : bid_price(0.0), ask_price(0.0), bid_size(0), ask_size(0),
                      last_price(0.0), last_size(0), sequence_number(0) {}
    
    HistoricalTick(core::TimePoint ts, const core::Symbol& sym, 
                  core::Price bid, core::Price ask, core::Quantity bid_sz, core::Quantity ask_sz,
                  core::Price last, core::Quantity last_sz, uint64_t seq)
        : timestamp(ts), symbol(sym), bid_price(bid), ask_price(ask),
          bid_size(bid_sz), ask_size(ask_sz), last_price(last), last_size(last_sz),
          sequence_number(seq) {}
    
    // Convert to MarketDataTick
    core::MarketDataTick to_market_data_tick() const {
        core::MarketDataTick tick;
        tick.timestamp = timestamp;
        tick.symbol = symbol;
        tick.bid = bid_price;
        tick.ask = ask_price;
        tick.bid_size = bid_size;
        tick.ask_size = ask_size;
        tick.last_price = last_price;
        tick.last_size = last_size;
        tick.sequence_number = sequence_number;
        return tick;
    }
};

struct HistoricalTrade {
    core::TimePoint timestamp;
    core::Symbol symbol;
    core::Price price;
    core::Quantity quantity;
    core::Side aggressor_side;
    std::string trade_id;
    
    HistoricalTrade() : price(0.0), quantity(0), aggressor_side(core::Side::BUY) {}
    
    HistoricalTrade(core::TimePoint ts, const core::Symbol& sym, core::Price p, 
                   core::Quantity q, core::Side side, const std::string& id = "")
        : timestamp(ts), symbol(sym), price(p), quantity(q), 
          aggressor_side(side), trade_id(id) {}
    
    // Convert to TradeExecution
    core::TradeExecution to_trade() const {
        return core::TradeExecution(0, 0, symbol, price, quantity, timestamp, core::TradeType::AGGRESSIVE_BUY);
    }
};

// ============================================================================
// Data Source Interface
// ============================================================================

class TickDataSource {
public:
    virtual ~TickDataSource() = default;
    
    // Load data for given symbol and time range
    virtual bool load_data(const core::Symbol& symbol,
                          const core::TimePoint& start_time,
                          const core::TimePoint& end_time) = 0;
    
    // Get next tick in chronological order
    virtual bool get_next_tick(HistoricalTick& tick) = 0;
    virtual bool get_next_trade(HistoricalTrade& trade) = 0;
    
    // Check if more data is available
    virtual bool has_more_data() const = 0;
    
    // Get data statistics
    virtual size_t get_tick_count() const = 0;
    virtual size_t get_trade_count() const = 0;
    virtual core::TimePoint get_start_time() const = 0;
    virtual core::TimePoint get_end_time() const = 0;
    
    // Reset to beginning
    virtual void reset() = 0;
};

// ============================================================================
// CSV File Data Source
// ============================================================================

class CSVTickDataSource : public TickDataSource {
private:
    std::string file_path_;
    std::ifstream file_stream_;
    std::vector<HistoricalTick> ticks_;
    std::vector<HistoricalTrade> trades_;
    size_t current_tick_index_;
    size_t current_trade_index_;
    
    // Data statistics
    core::TimePoint start_time_;
    core::TimePoint end_time_;
    
public:
    explicit CSVTickDataSource(const std::string& file_path);
    ~CSVTickDataSource() override;
    
    // TickDataSource interface
    bool load_data(const core::Symbol& symbol,
                  const core::TimePoint& start_time,
                  const core::TimePoint& end_time) override;
    
    bool get_next_tick(HistoricalTick& tick) override;
    bool get_next_trade(HistoricalTrade& trade) override;
    bool has_more_data() const override;
    size_t get_tick_count() const override { return ticks_.size(); }
    size_t get_trade_count() const override { return trades_.size(); }
    core::TimePoint get_start_time() const override { return start_time_; }
    core::TimePoint get_end_time() const override { return end_time_; }
    void reset() override;
    
    // CSV-specific methods
    void set_csv_format(const std::string& format); // e.g., "timestamp,symbol,bid,ask,bid_size,ask_size"
    bool parse_csv_line(const std::string& line, HistoricalTick& tick);
    
private:
    core::TimePoint parse_timestamp(const std::string& timestamp_str);
    std::vector<std::string> split_csv_line(const std::string& line);
};

// ============================================================================
// Market Data Simulator
// ============================================================================

class MarketDataSimulator {
private:
    std::vector<std::string> symbols_;
    std::mt19937 rng_;
    
    // Simulation parameters
    std::atomic<double> base_volatility_{0.02};
    std::atomic<double> tick_frequency_hz_{1000.0}; // 1000 ticks per second
    std::atomic<double> spread_bps_{5.0}; // 5 bps spread
    
    // Current market state
    std::unordered_map<core::Symbol, core::Price> current_prices_;
    std::unordered_map<core::Symbol, core::Price> daily_opens_;
    std::atomic<uint64_t> next_sequence_number_{1};
    
public:
    explicit MarketDataSimulator(const std::vector<std::string>& symbols);
    
    // Generate synthetic tick data
    HistoricalTick generate_tick(const core::Symbol& symbol, core::TimePoint timestamp);
    HistoricalTrade generate_trade(const core::Symbol& symbol, core::TimePoint timestamp);
    
    // Generate realistic market session
    std::vector<HistoricalTick> generate_trading_session(
        const core::Symbol& symbol,
        core::TimePoint session_start,
        std::chrono::seconds session_duration
    );
    
    // Configuration
    void set_volatility(double volatility) { base_volatility_.store(volatility); }
    void set_tick_frequency(double hz) { tick_frequency_hz_.store(hz); }
    void set_spread_bps(double spread) { spread_bps_.store(spread); }
    void set_initial_price(const core::Symbol& symbol, core::Price price);
    
    // Market microstructure simulation
    void simulate_market_impact(const core::Symbol& symbol, core::Quantity quantity, core::Side side);
    void simulate_volatility_clustering();
    void simulate_intraday_patterns(core::TimePoint current_time);
    
private:
    core::Price generate_price_movement(const core::Symbol& symbol);
    core::Quantity generate_realistic_size();
    double get_intraday_volatility_factor(core::TimePoint timestamp);
    double get_intraday_volume_factor(core::TimePoint timestamp);
};

// ============================================================================
// Tick Replay Engine
// ============================================================================

class TickReplayEngine {
public:
    using TickCallback = std::function<void(const HistoricalTick&)>;
    using TradeCallback = std::function<void(const HistoricalTrade&)>;
    using CompletionCallback = std::function<void()>;
    
    enum class ReplayMode {
        HISTORICAL_TIME,    // Replay with historical timestamps
        ACCELERATED,        // Accelerated replay (faster than real-time)
        STEP_BY_STEP,      // Manual step-through
        REAL_TIME          // Real-time replay speed
    };
    
private:
    std::unique_ptr<TickDataSource> data_source_;
    std::vector<std::thread> replay_threads_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    
    // Replay configuration
    ReplayMode mode_;
    double acceleration_factor_;
    std::atomic<size_t> current_position_{0};
    
    // Callbacks
    TickCallback tick_callback_;
    TradeCallback trade_callback_;
    CompletionCallback completion_callback_;
    
    // Timing control
    core::TimePoint replay_start_time_;
    core::TimePoint data_start_time_;
    core::TimePoint data_end_time_;
    
    // Performance monitoring
    std::atomic<uint64_t> ticks_replayed_{0};
    std::atomic<uint64_t> trades_replayed_{0};
    std::atomic<double> replay_rate_hz_{0.0};
    
    // Queue for tick distribution
    std::unique_ptr<core::LockFreeQueue<HistoricalTick, 8192>> tick_queue_;
    std::unique_ptr<core::LockFreeQueue<HistoricalTrade, 4096>> trade_queue_;
    
public:
    explicit TickReplayEngine(std::unique_ptr<TickDataSource> data_source);
    ~TickReplayEngine();
    
    // Configuration
    void set_replay_mode(ReplayMode mode, double acceleration_factor = 1.0);
    void set_tick_callback(TickCallback callback);
    void set_trade_callback(TradeCallback callback);
    void set_completion_callback(CompletionCallback callback);
    
    // Control
    void start_replay();
    void pause_replay();
    void resume_replay();
    void stop_replay();
    void step_forward(); // For step-by-step mode
    
    // Navigation
    void seek_to_time(const core::TimePoint& timestamp);
    void seek_to_position(double percentage); // 0.0 to 1.0
    void reset_to_beginning();
    
    // Status
    bool is_running() const { return running_.load(); }
    bool is_paused() const { return paused_.load(); }
    double get_progress() const; // 0.0 to 1.0
    
    // Statistics
    uint64_t get_ticks_replayed() const { return ticks_replayed_.load(); }
    uint64_t get_trades_replayed() const { return trades_replayed_.load(); }
    double get_replay_rate() const { return replay_rate_hz_.load(); }
    
    // Data source access
    TickDataSource* get_data_source() { return data_source_.get(); }
    const TickDataSource* get_data_source() const { return data_source_.get(); }
    
private:
    // Worker threads
    void tick_replay_worker();
    void trade_replay_worker();
    void tick_distribution_worker();
    void trade_distribution_worker();
    
    // Timing calculations
    std::chrono::nanoseconds calculate_delay(const core::TimePoint& tick_time);
    void update_replay_rate();
    
    // Queue management
    bool should_process_tick(const HistoricalTick& tick) const;
    bool should_process_trade(const HistoricalTrade& trade) const;
};

// ============================================================================
// Backtesting Framework Integration
// ============================================================================

struct BacktestMetrics {
    // Performance metrics
    double total_pnl;
    double sharpe_ratio;
    double max_drawdown;
    double win_rate;
    
    // Execution metrics
    uint64_t total_orders;
    uint64_t filled_orders;
    uint64_t cancelled_orders;
    double avg_fill_rate;
    
    // Latency metrics
    double avg_order_latency_us;
    double p99_order_latency_us;
    double avg_market_data_latency_us;
    
    // Slippage metrics
    double total_slippage_bps;
    double avg_slippage_per_trade_bps;
    double market_impact_bps;
    
    // Risk metrics
    double max_position_size;
    double value_at_risk_95;
    double expected_shortfall;
    
    BacktestMetrics() : total_pnl(0.0), sharpe_ratio(0.0), max_drawdown(0.0), win_rate(0.0),
                       total_orders(0), filled_orders(0), cancelled_orders(0), avg_fill_rate(0.0),
                       avg_order_latency_us(0.0), p99_order_latency_us(0.0), avg_market_data_latency_us(0.0),
                       total_slippage_bps(0.0), avg_slippage_per_trade_bps(0.0), market_impact_bps(0.0),
                       max_position_size(0.0), value_at_risk_95(0.0), expected_shortfall(0.0) {}
};

class BacktestRunner {
private:
    std::unique_ptr<TickReplayEngine> replay_engine_;
    BacktestMetrics metrics_;
    
    // Strategy integration
    std::function<void(const HistoricalTick&)> strategy_on_tick_;
    std::function<void(const HistoricalTrade&)> strategy_on_trade_;
    
    // P&L tracking
    std::vector<double> pnl_history_;
    std::vector<core::TimePoint> pnl_timestamps_;
    
    // Order tracking
    std::atomic<uint64_t> order_count_{0};
    std::vector<double> order_latencies_;
    std::vector<double> slippage_values_;
    
public:
    explicit BacktestRunner(std::unique_ptr<TickReplayEngine> engine);
    
    // Strategy integration
    void set_strategy_callbacks(
        std::function<void(const HistoricalTick&)> on_tick,
        std::function<void(const HistoricalTrade&)> on_trade
    );
    
    // Run backtest
    void run_backtest(const core::TimePoint& start_time, const core::TimePoint& end_time);
    
    // Results
    const BacktestMetrics& get_metrics() const { return metrics_; }
    void export_results(const std::string& filename) const;
    void print_summary() const;
    
    // Real-time monitoring during backtest
    void on_order_submitted(core::OrderID order_id, const core::TimePoint& submission_time);
    void on_order_filled(core::OrderID order_id, const core::TimePoint& fill_time, 
                        core::Price fill_price, core::Price expected_price);
    void on_pnl_update(double pnl, const core::TimePoint& timestamp);
    
private:
    void calculate_performance_metrics();
    double calculate_sharpe_ratio() const;
    double calculate_max_drawdown() const;
    double calculate_win_rate() const;
    void update_latency_statistics();
};

// ============================================================================
// Utility Functions
// ============================================================================

// Time utilities for backtesting
core::TimePoint parse_iso_timestamp(const std::string& timestamp);
std::string format_timestamp_for_csv(const core::TimePoint& timestamp);
core::TimePoint market_open_time(const core::TimePoint& date);
core::TimePoint market_close_time(const core::TimePoint& date);

// Data generation utilities
std::vector<HistoricalTick> generate_sample_tick_data(
    const core::Symbol& symbol,
    const core::TimePoint& start_time,
    std::chrono::seconds duration,
    double tick_frequency_hz = 1000.0
);

void export_tick_data_to_csv(
    const std::vector<HistoricalTick>& ticks,
    const std::string& filename
);

std::unique_ptr<TickDataSource> create_csv_data_source(const std::string& filename);
std::unique_ptr<TickDataSource> create_simulated_data_source(
    const std::vector<std::string>& symbols,
    const core::TimePoint& start_time,
    std::chrono::seconds duration
);

// Performance analysis utilities
void analyze_backtest_results(const BacktestMetrics& metrics);
void compare_backtest_results(const std::vector<BacktestMetrics>& results);
void plot_pnl_curve(const std::vector<double>& pnl_history, const std::string& filename);

} // namespace backtesting
} // namespace hft