#pragma once
#include "hft/core/types.hpp"
#include "hft/core/clock.hpp"
#include "hft/matching/matching_engine.hpp"
#include "hft/analytics/pnl_calculator.hpp"
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <functional>
#include <map>
#include <chrono>
namespace hft {
namespace backtesting {
struct HistoricalTick {
    core::Symbol symbol;
    core::TimePoint timestamp;
    double bid_price;
    double ask_price;
    double last_price;
    uint64_t bid_size;
    uint64_t ask_size;
    uint64_t last_size;
    uint64_t sequence_number;
    double spread_bps;
    double mid_price;
    bool is_trade_tick;
    core::Side trade_aggressor_side;
    HistoricalTick() = default;
    HistoricalTick(const core::Symbol& sym, core::TimePoint ts,
                  double bid, double ask, double last,
                  uint64_t bid_sz, uint64_t ask_sz, uint64_t last_sz)
        : symbol(sym), timestamp(ts), bid_price(bid), ask_price(ask),
          last_price(last), bid_size(bid_sz), ask_size(ask_sz), last_size(last_sz),
          sequence_number(0) {
        mid_price = (bid_price + ask_price) / 2.0;
        spread_bps = ((ask_price - bid_price) / mid_price) * 10000.0;
        is_trade_tick = (last_size > 0);
    }
};
struct HistoricalOrderBookSnapshot {
    core::Symbol symbol;
    core::TimePoint timestamp;
    std::vector<std::pair<double, uint64_t>> bids;
    std::vector<std::pair<double, uint64_t>> asks;
    uint64_t sequence_number;
    double get_mid_price() const {
        if (bids.empty() || asks.empty()) return 0.0;
        return (bids[0].first + asks[0].first) / 2.0;
    }
    double get_spread_bps() const {
        if (bids.empty() || asks.empty()) return 0.0;
        double mid = get_mid_price();
        return ((asks[0].first - bids[0].first) / mid) * 10000.0;
    }
};
class MarketDataParser {
public:
    virtual ~MarketDataParser() = default;
    virtual bool open(const std::string& filename) = 0;
    virtual bool read_next_tick(HistoricalTick& tick) = 0;
    virtual bool read_next_snapshot(HistoricalOrderBookSnapshot& snapshot) = 0;
    virtual void close() = 0;
    virtual bool has_more_data() const = 0;
    virtual size_t get_total_records() const = 0;
};
class CSVMarketDataParser : public MarketDataParser {
private:
    std::ifstream file_;
    std::string current_line_;
    size_t line_number_;
    core::Symbol default_symbol_;
    struct CSVFormat {
        int timestamp_col = 0;
        int symbol_col = 1;
        int bid_price_col = 2;
        int ask_price_col = 3;
        int last_price_col = 4;
        int bid_size_col = 5;
        int ask_size_col = 6;
        int last_size_col = 7;
        char delimiter = ',';
        bool has_header = true;
        std::string timestamp_format = "%Y-%m-%d %H:%M:%S.%f";
    };
    CSVFormat format_;
public:
    explicit CSVMarketDataParser(const core::Symbol& default_symbol = "");
    void set_csv_format(const CSVFormat& format) { format_ = format; }
    bool open(const std::string& filename) override;
    bool read_next_tick(HistoricalTick& tick) override;
    bool read_next_snapshot(HistoricalOrderBookSnapshot& snapshot) override;
    void close() override;
    bool has_more_data() const override;
    size_t get_total_records() const override;
private:
    std::vector<std::string> parse_csv_line(const std::string& line);
    core::TimePoint parse_timestamp(const std::string& timestamp_str);
};
class BinaryMarketDataParser : public MarketDataParser {
private:
    std::ifstream file_;
    size_t file_size_;
    size_t current_position_;
    struct BinaryHeader {
        char magic[4] = {'H', 'F', 'T', 'D'};
        uint32_t version = 1;
        uint64_t record_count;
        uint64_t start_time_ns;
        uint64_t end_time_ns;
        char symbol[16];
        uint32_t record_size;
    };
    BinaryHeader header_;
public:
    bool open(const std::string& filename) override;
    bool read_next_tick(HistoricalTick& tick) override;
    bool read_next_snapshot(HistoricalOrderBookSnapshot& snapshot) override;
    void close() override;
    bool has_more_data() const override;
    size_t get_total_records() const override;
    static bool convert_csv_to_binary(const std::string& csv_file, const std::string& binary_file);
};
class BacktestingStrategy {
public:
    virtual ~BacktestingStrategy() = default;
    virtual void on_start(const std::map<core::Symbol, double>& initial_prices) {}
    virtual void on_finish() {}
    virtual void on_tick(const HistoricalTick& tick) = 0;
    virtual void on_order_book_update(const HistoricalOrderBookSnapshot& snapshot) {}
    virtual void on_execution_report(const matching::ExecutionReport& report) {}
    virtual void on_fill(const matching::Fill& fill) {}
    virtual void on_pnl_update(const core::Symbol& symbol, double pnl) {}
    virtual std::string get_strategy_name() const = 0;
    virtual std::map<std::string, std::string> get_parameters() const { return {}; }
protected:
    matching::MatchingEngine* matching_engine_ = nullptr;
    analytics::PnLCalculator* pnl_calculator_ = nullptr;
    friend class TickReplayEngine;
};
class MarketMakingStrategy : public BacktestingStrategy {
private:
    double spread_bps_;
    uint64_t default_size_;
    double max_position_;
    double inventory_skew_factor_;
    std::map<core::Symbol, double> positions_;
    std::map<core::Symbol, std::pair<core::OrderID, core::OrderID>> active_quotes_;
    std::atomic<core::OrderID> next_order_id_{1};
    double min_spread_bps_;
    double max_spread_bps_;
    bool adaptive_sizing_;
public:
    MarketMakingStrategy(double spread_bps = 5.0, uint64_t default_size = 100,
                        double max_position = 10000.0);
    void on_tick(const HistoricalTick& tick) override;
    void on_execution_report(const matching::ExecutionReport& report) override;
    void on_fill(const matching::Fill& fill) override;
    std::string get_strategy_name() const override { return "MarketMaking"; }
    std::map<std::string, std::string> get_parameters() const override;
private:
    void update_quotes(const core::Symbol& symbol, double reference_price);
    void cancel_quotes(const core::Symbol& symbol);
    std::pair<double, double> calculate_quote_prices(const core::Symbol& symbol, double ref_price);
    uint64_t calculate_quote_size(const core::Symbol& symbol, core::Side side);
    void update_position(const core::Symbol& symbol, const matching::Fill& fill);
};
class MomentumStrategy : public BacktestingStrategy {
private:
    struct SymbolState {
        std::deque<double> price_history;
        double ema_fast = 0.0;
        double ema_slow = 0.0;
        bool in_position = false;
        core::Side position_side;
        core::OrderID active_order_id = 0;
    };
    std::map<core::Symbol, SymbolState> symbol_states_;
    double fast_ema_alpha_;
    double slow_ema_alpha_;
    double momentum_threshold_;
    uint64_t position_size_;
public:
    MomentumStrategy(double fast_period = 10.0, double slow_period = 30.0,
                    double momentum_threshold = 0.001, uint64_t position_size = 1000);
    void on_tick(const HistoricalTick& tick) override;
    void on_execution_report(const matching::ExecutionReport& report) override;
    std::string get_strategy_name() const override { return "Momentum"; }
    std::map<std::string, std::string> get_parameters() const override;
private:
    void update_indicators(const core::Symbol& symbol, double price);
    bool check_entry_signal(const core::Symbol& symbol);
    bool check_exit_signal(const core::Symbol& symbol);
    void enter_position(const core::Symbol& symbol, core::Side side, double price);
    void exit_position(const core::Symbol& symbol, double price);
};
class TickReplayEngine {
public:
    struct ReplayConfig {
        core::TimePoint start_time = core::TimePoint::min();
        core::TimePoint end_time = core::TimePoint::max();
        double time_acceleration = 1.0;
        bool enable_slippage_model = true;
        double commission_rate = 0.001;
        bool print_progress = true;
        size_t progress_interval = 100000;
    };
    
private:
    std::unique_ptr<MarketDataParser> data_parser_;
    std::unique_ptr<matching::MatchingEngine> matching_engine_;
    std::unique_ptr<analytics::PnLCalculator> pnl_calculator_;
    std::vector<std::unique_ptr<BacktestingStrategy>> strategies_;
    ReplayConfig config_;
    core::TimePoint current_replay_time_;
    size_t ticks_processed_;
    std::chrono::steady_clock::time_point replay_start_time_;
    struct ReplayStats {
        size_t total_ticks = 0;
        size_t total_trades = 0;
        double total_volume = 0.0;
        double total_notional = 0.0;
        core::TimePoint first_tick_time;
        core::TimePoint last_tick_time;
        std::chrono::milliseconds replay_duration{0};
        double ticks_per_second = 0.0;
    } stats_;
public:
    explicit TickReplayEngine(std::unique_ptr<MarketDataParser> parser);
    ~TickReplayEngine();
    void set_replay_config(const ReplayConfig& config) { config_ = config; }
    void set_commission_rate(double rate) { config_.commission_rate = rate; }
    void enable_slippage_model(bool enable) { config_.enable_slippage_model = enable; }
    void add_strategy(std::unique_ptr<BacktestingStrategy> strategy);
    void clear_strategies();
    bool run_backtest();
    void stop_backtest();
    const ReplayStats& get_replay_stats() const { return stats_; }
    analytics::PnLCalculator* get_pnl_calculator() { return pnl_calculator_.get(); }
    void print_backtest_summary() const;
    void export_results(const std::string& filename) const;
    void export_trade_log(const std::string& filename) const;
private:
    bool process_tick(const HistoricalTick& tick);
    void update_strategies(const HistoricalTick& tick);
    void apply_slippage_model(matching::ExecutionReport& report);
    bool is_tick_in_replay_window(const HistoricalTick& tick) const;
    void update_replay_time(const HistoricalTick& tick);
    void handle_time_acceleration();
    void setup_matching_engine_callbacks();
    void on_execution_report(const matching::ExecutionReport& report);
    void on_fill(const matching::Fill& fill);
};
class BacktestingSuite {
private:
    struct BacktestJob {
        std::string name;
        std::string data_file;
        std::unique_ptr<BacktestingStrategy> strategy;
        TickReplayEngine::ReplayConfig config;
        bool completed = false;
        double final_pnl = 0.0;
        double max_drawdown = 0.0;
        double sharpe_ratio = 0.0;
        size_t total_trades = 0;
    };
    std::vector<BacktestJob> jobs_;
public:
    void add_backtest_job(const std::string& name, const std::string& data_file,
                         std::unique_ptr<BacktestingStrategy> strategy,
                         const TickReplayEngine::ReplayConfig& config = {});
    bool run_all_backtests();
    bool run_backtest(size_t job_index);
    void print_suite_summary() const;
    void export_comparative_results(const std::string& filename) const;
    void run_parameter_sweep(const std::string& strategy_name,
                           const std::string& data_file,
                           const std::map<std::string, std::vector<double>>& param_ranges);
private:
    void calculate_performance_metrics(BacktestJob& job, analytics::PnLCalculator* pnl_calc);
    double calculate_sharpe_ratio(const std::vector<double>& returns);
    double calculate_max_drawdown(const std::vector<double>& equity_curve);
};
}
}
