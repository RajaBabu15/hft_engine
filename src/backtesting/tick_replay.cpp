#include "hft/backtesting/tick_replay.hpp"
#include "hft/order/order.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <random>
#include <deque>
#include <numeric>
namespace hft {
namespace backtesting {
CSVMarketDataParser::CSVMarketDataParser(const core::Symbol& default_symbol)
    : line_number_(0), default_symbol_(default_symbol) {}
bool CSVMarketDataParser::open(const std::string& filename) {
    file_.open(filename);
    if (!file_.is_open()) {
        std::cerr << "Failed to open CSV file: " << filename << std::endl;
        return false;
    }
    line_number_ = 0;
    if (format_.has_header && file_.good()) {
        std::getline(file_, current_line_);
        line_number_++;
    }
    return true;
}
bool CSVMarketDataParser::read_next_tick(HistoricalTick& tick) {
    if (!file_.good()) return false;
    if (!std::getline(file_, current_line_)) {
        return false;
    }
    line_number_++;
    try {
        auto fields = parse_csv_line(current_line_);
        if (fields.size() < 8) {
            std::cerr << "Invalid CSV format at line " << line_number_ << std::endl;
            return false;
        }
        tick.timestamp = parse_timestamp(fields[format_.timestamp_col]);
        tick.symbol = fields.size() > format_.symbol_col ? fields[format_.symbol_col] : default_symbol_;
        tick.bid_price = std::stod(fields[format_.bid_price_col]);
        tick.ask_price = std::stod(fields[format_.ask_price_col]);
        tick.last_price = std::stod(fields[format_.last_price_col]);
        tick.bid_size = std::stoull(fields[format_.bid_size_col]);
        tick.ask_size = std::stoull(fields[format_.ask_size_col]);
        tick.last_size = std::stoull(fields[format_.last_size_col]);
        tick.sequence_number = line_number_;
        tick.mid_price = (tick.bid_price + tick.ask_price) / 2.0;
        tick.spread_bps = ((tick.ask_price - tick.bid_price) / tick.mid_price) * 10000.0;
        tick.is_trade_tick = (tick.last_size > 0);
        tick.trade_aggressor_side = tick.last_price >= tick.mid_price ?
            core::Side::BUY : core::Side::SELL;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Parse error at line " << line_number_ << ": " << e.what() << std::endl;
        return false;
    }
}
bool CSVMarketDataParser::read_next_snapshot(HistoricalOrderBookSnapshot& snapshot) {
    HistoricalTick tick;
    if (!read_next_tick(tick)) return false;
    snapshot.symbol = tick.symbol;
    snapshot.timestamp = tick.timestamp;
    snapshot.sequence_number = tick.sequence_number;
    snapshot.bids.clear();
    snapshot.asks.clear();
    snapshot.bids.emplace_back(tick.bid_price, tick.bid_size);
    snapshot.asks.emplace_back(tick.ask_price, tick.ask_size);
    return true;
}
void CSVMarketDataParser::close() {
    if (file_.is_open()) {
        file_.close();
    }
    line_number_ = 0;
}
bool CSVMarketDataParser::has_more_data() const {
    return file_.good() && !file_.eof();
}
size_t CSVMarketDataParser::get_total_records() const {
    return line_number_;
}
std::vector<std::string> CSVMarketDataParser::parse_csv_line(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, format_.delimiter)) {
        field.erase(field.find_last_not_of(" \t\r\n") + 1);
        field.erase(0, field.find_first_not_of(" \t\r\n"));
        result.push_back(field);
    }
    return result;
}
core::TimePoint CSVMarketDataParser::parse_timestamp(const std::string& timestamp_str) {
    try {
        uint64_t ns = std::stoull(timestamp_str);
        return core::TimePoint(std::chrono::nanoseconds(ns));
    } catch (...) {
        return std::chrono::steady_clock::now();
    }
}
bool BinaryMarketDataParser::open(const std::string& filename) {
    file_.open(filename, std::ios::binary);
    if (!file_.is_open()) {
        std::cerr << "Failed to open binary file: " << filename << std::endl;
        return false;
    }
    file_.read(reinterpret_cast<char*>(&header_), sizeof(BinaryHeader));
    if (!file_.good() || memcmp(header_.magic, "HFTD", 4) != 0) {
        std::cerr << "Invalid binary file format" << std::endl;
        file_.close();
        return false;
    }
    file_.seekg(0, std::ios::end);
    file_size_ = file_.tellg();
    file_.seekg(sizeof(BinaryHeader), std::ios::beg);
    current_position_ = sizeof(BinaryHeader);
    return true;
}
bool BinaryMarketDataParser::read_next_tick(HistoricalTick& tick) {
    if (!file_.good() || current_position_ >= file_size_) return false;
    struct BinaryTick {
        uint64_t timestamp_ns;
        double bid_price, ask_price, last_price;
        uint64_t bid_size, ask_size, last_size;
        uint64_t sequence_number;
    } binary_tick;
    file_.read(reinterpret_cast<char*>(&binary_tick), sizeof(BinaryTick));
    if (!file_.good()) return false;
    tick.symbol = std::string(header_.symbol);
    tick.timestamp = core::TimePoint(std::chrono::nanoseconds(binary_tick.timestamp_ns));
    tick.bid_price = binary_tick.bid_price;
    tick.ask_price = binary_tick.ask_price;
    tick.last_price = binary_tick.last_price;
    tick.bid_size = binary_tick.bid_size;
    tick.ask_size = binary_tick.ask_size;
    tick.last_size = binary_tick.last_size;
    tick.sequence_number = binary_tick.sequence_number;
    tick.mid_price = (tick.bid_price + tick.ask_price) / 2.0;
    tick.spread_bps = ((tick.ask_price - tick.bid_price) / tick.mid_price) * 10000.0;
    tick.is_trade_tick = (tick.last_size > 0);
    tick.trade_aggressor_side = tick.last_price >= tick.mid_price ?
        core::Side::BUY : core::Side::SELL;
    current_position_ += sizeof(BinaryTick);
    return true;
}
bool BinaryMarketDataParser::read_next_snapshot(HistoricalOrderBookSnapshot& snapshot) {
    HistoricalTick tick;
    if (!read_next_tick(tick)) return false;
    snapshot.symbol = tick.symbol;
    snapshot.timestamp = tick.timestamp;
    snapshot.sequence_number = tick.sequence_number;
    snapshot.bids.clear();
    snapshot.asks.clear();
    snapshot.bids.emplace_back(tick.bid_price, tick.bid_size);
    snapshot.asks.emplace_back(tick.ask_price, tick.ask_size);
    return true;
}
void BinaryMarketDataParser::close() {
    if (file_.is_open()) {
        file_.close();
    }
    current_position_ = 0;
}
bool BinaryMarketDataParser::has_more_data() const {
    return file_.good() && current_position_ < file_size_;
}
size_t BinaryMarketDataParser::get_total_records() const {
    return header_.record_count;
}
bool BinaryMarketDataParser::convert_csv_to_binary(const std::string& csv_file, const std::string& binary_file) {
    CSVMarketDataParser csv_parser;
    if (!csv_parser.open(csv_file)) return false;
    std::ofstream bin_file(binary_file, std::ios::binary);
    if (!bin_file.is_open()) return false;
    struct BinaryTick {
        uint64_t timestamp_ns;
        double bid_price, ask_price, last_price;
        uint64_t bid_size, ask_size, last_size;
        uint64_t sequence_number;
    };
    BinaryHeader header;
    header.record_count = 0;
    header.start_time_ns = 0;
    header.end_time_ns = 0;
    strcpy(header.symbol, "UNKNOWN");
    header.record_size = sizeof(BinaryTick);
    bin_file.write(reinterpret_cast<const char*>(&header), sizeof(BinaryHeader));
    HistoricalTick tick;
    while (csv_parser.read_next_tick(tick)) {
        BinaryTick binary_tick;
        binary_tick.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            tick.timestamp.time_since_epoch()).count();
        binary_tick.bid_price = tick.bid_price;
        binary_tick.ask_price = tick.ask_price;
        binary_tick.last_price = tick.last_price;
        binary_tick.bid_size = tick.bid_size;
        binary_tick.ask_size = tick.ask_size;
        binary_tick.last_size = tick.last_size;
        binary_tick.sequence_number = tick.sequence_number;
        bin_file.write(reinterpret_cast<const char*>(&binary_tick), sizeof(binary_tick));
        header.record_count++;
        if (header.record_count == 1) {
            header.start_time_ns = binary_tick.timestamp_ns;
            strcpy(header.symbol, tick.symbol.c_str());
        }
        header.end_time_ns = binary_tick.timestamp_ns;
    }
    bin_file.seekp(0);
    bin_file.write(reinterpret_cast<const char*>(&header), sizeof(BinaryHeader));
    csv_parser.close();
    bin_file.close();
    return true;
}
MarketMakingStrategy::MarketMakingStrategy(double spread_bps, uint64_t default_size, double max_position)
    : spread_bps_(spread_bps), default_size_(default_size), max_position_(max_position),
      inventory_skew_factor_(0.1), min_spread_bps_(2.0), max_spread_bps_(20.0),
      adaptive_sizing_(true) {}
void MarketMakingStrategy::on_tick(const HistoricalTick& tick) {
    if (!matching_engine_) return;
    cancel_quotes(tick.symbol);
    update_quotes(tick.symbol, tick.mid_price);
}
void MarketMakingStrategy::update_quotes(const core::Symbol& symbol, double reference_price) {
    auto [bid_price, ask_price] = calculate_quote_prices(symbol, reference_price);
    uint64_t bid_size = calculate_quote_size(symbol, core::Side::BUY);
    uint64_t ask_size = calculate_quote_size(symbol, core::Side::SELL);
    core::OrderID bid_order_id = next_order_id_.fetch_add(1);
    core::OrderID ask_order_id = next_order_id_.fetch_add(1);
    order::Order bid_order(bid_order_id, symbol, core::Side::BUY,
                          core::OrderType::LIMIT, bid_price, bid_size);
    order::Order ask_order(ask_order_id, symbol, core::Side::SELL,
                          core::OrderType::LIMIT, ask_price, ask_size);
    matching_engine_->submit_order(bid_order);
    matching_engine_->submit_order(ask_order);
    active_quotes_[symbol] = {bid_order_id, ask_order_id};
}
void MarketMakingStrategy::cancel_quotes(const core::Symbol& symbol) {
    auto it = active_quotes_.find(symbol);
    if (it != active_quotes_.end()) {
        matching_engine_->cancel_order(it->second.first);
        matching_engine_->cancel_order(it->second.second);
        active_quotes_.erase(it);
    }
}
std::pair<double, double> MarketMakingStrategy::calculate_quote_prices(
    const core::Symbol& symbol, double ref_price) {
    double current_position = positions_[symbol];
    double inventory_skew = current_position * inventory_skew_factor_;
    double effective_spread_bps = spread_bps_;
    if (std::abs(current_position) > max_position_ * 0.8) {
        effective_spread_bps = std::min(max_spread_bps_, spread_bps_ * 1.5);
    }
    double half_spread = ref_price * effective_spread_bps / 20000.0;
    double bid_price = ref_price - half_spread - inventory_skew;
    double ask_price = ref_price + half_spread - inventory_skew;
    return {bid_price, ask_price};
}
uint64_t MarketMakingStrategy::calculate_quote_size(const core::Symbol& symbol, core::Side side) {
    if (!adaptive_sizing_) return default_size_;
    double current_position = positions_[symbol];
    double size_adjustment = 1.0;
    if ((side == core::Side::BUY && current_position > max_position_ * 0.7) ||
        (side == core::Side::SELL && current_position < -max_position_ * 0.7)) {
        size_adjustment = 0.5;
    }
    return static_cast<uint64_t>(default_size_ * size_adjustment);
}
void MarketMakingStrategy::update_position(const core::Symbol& symbol, const matching::Fill& fill) {
    double position_change = static_cast<double>(fill.quantity);
    positions_[symbol] += position_change;
}
void MarketMakingStrategy::on_execution_report(const matching::ExecutionReport& report) {
}
void MarketMakingStrategy::on_fill(const matching::Fill& fill) {
    update_position(fill.symbol, fill);
}
std::map<std::string, std::string> MarketMakingStrategy::get_parameters() const {
    return {
        {"spread_bps", std::to_string(spread_bps_)},
        {"default_size", std::to_string(default_size_)},
        {"max_position", std::to_string(max_position_)},
        {"inventory_skew_factor", std::to_string(inventory_skew_factor_)}
    };
}
MomentumStrategy::MomentumStrategy(double fast_period, double slow_period,
                                 double momentum_threshold, uint64_t position_size)
    : momentum_threshold_(momentum_threshold), position_size_(position_size) {
    fast_ema_alpha_ = 2.0 / (fast_period + 1.0);
    slow_ema_alpha_ = 2.0 / (slow_period + 1.0);
}
void MomentumStrategy::on_tick(const HistoricalTick& tick) {
    if (!matching_engine_) return;
    update_indicators(tick.symbol, tick.mid_price);
    auto& state = symbol_states_[tick.symbol];
    if (!state.in_position && check_entry_signal(tick.symbol)) {
        core::Side entry_side = state.ema_fast > state.ema_slow ? core::Side::BUY : core::Side::SELL;
        enter_position(tick.symbol, entry_side, tick.mid_price);
    } else if (state.in_position && check_exit_signal(tick.symbol)) {
        exit_position(tick.symbol, tick.mid_price);
    }
}
void MomentumStrategy::update_indicators(const core::Symbol& symbol, double price) {
    auto& state = symbol_states_[symbol];
    state.price_history.push_back(price);
    if (state.price_history.size() > 100) {
        state.price_history.pop_front();
    }
    if (state.ema_fast == 0.0) {
        state.ema_fast = price;
        state.ema_slow = price;
    } else {
        state.ema_fast = fast_ema_alpha_ * price + (1.0 - fast_ema_alpha_) * state.ema_fast;
        state.ema_slow = slow_ema_alpha_ * price + (1.0 - slow_ema_alpha_) * state.ema_slow;
    }
}
bool MomentumStrategy::check_entry_signal(const core::Symbol& symbol) {
    auto& state = symbol_states_[symbol];
    if (state.price_history.size() < 10) return false;
    double momentum = (state.ema_fast - state.ema_slow) / state.ema_slow;
    return std::abs(momentum) > momentum_threshold_;
}
bool MomentumStrategy::check_exit_signal(const core::Symbol& symbol) {
    auto& state = symbol_states_[symbol];
    double momentum = (state.ema_fast - state.ema_slow) / state.ema_slow;
    bool bullish_position = (state.position_side == core::Side::BUY);
    return (bullish_position && momentum < -momentum_threshold_ * 0.5) ||
           (!bullish_position && momentum > momentum_threshold_ * 0.5);
}
void MomentumStrategy::enter_position(const core::Symbol& symbol, core::Side side, double price) {
    auto& state = symbol_states_[symbol];
    core::OrderID order_id = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    order::Order order(order_id, symbol, side, core::OrderType::MARKET, price, position_size_);
    matching_engine_->submit_order(order);
    state.in_position = true;
    state.position_side = side;
    state.active_order_id = order_id;
}
void MomentumStrategy::exit_position(const core::Symbol& symbol, double price) {
    auto& state = symbol_states_[symbol];
    if (!state.in_position) return;
    core::Side exit_side = (state.position_side == core::Side::BUY) ?
        core::Side::SELL : core::Side::BUY;
    core::OrderID order_id = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    order::Order order(order_id, symbol, exit_side, core::OrderType::MARKET, price, position_size_);
    matching_engine_->submit_order(order);
    state.in_position = false;
    state.active_order_id = 0;
}
void MomentumStrategy::on_execution_report(const matching::ExecutionReport& report) {
}
std::map<std::string, std::string> MomentumStrategy::get_parameters() const {
    return {
        {"fast_ema_alpha", std::to_string(fast_ema_alpha_)},
        {"slow_ema_alpha", std::to_string(slow_ema_alpha_)},
        {"momentum_threshold", std::to_string(momentum_threshold_)},
        {"position_size", std::to_string(position_size_)}
    };
}
TickReplayEngine::TickReplayEngine(std::unique_ptr<MarketDataParser> parser)
    : data_parser_(std::move(parser)), ticks_processed_(0) {
    matching_engine_ = std::make_unique<matching::MatchingEngine>(
        matching::MatchingAlgorithm::PRICE_TIME_PRIORITY, "logs/backtest.log", false);
    pnl_calculator_ = std::make_unique<analytics::PnLCalculator>();
    setup_matching_engine_callbacks();
}
TickReplayEngine::~TickReplayEngine() = default;
void TickReplayEngine::add_strategy(std::unique_ptr<BacktestingStrategy> strategy) {
    strategy->matching_engine_ = matching_engine_.get();
    strategy->pnl_calculator_ = pnl_calculator_.get();
    strategies_.push_back(std::move(strategy));
}
void TickReplayEngine::clear_strategies() {
    strategies_.clear();
}
bool TickReplayEngine::run_backtest() {
    if (!data_parser_) {
        std::cerr << "No data parser configured" << std::endl;
        return false;
    }
    replay_start_time_ = std::chrono::steady_clock::now();
    stats_ = ReplayStats{};
    ticks_processed_ = 0;
    matching_engine_->start();
    std::map<core::Symbol, double> initial_prices;
    HistoricalTick first_tick;
    if (data_parser_->read_next_tick(first_tick)) {
        initial_prices[first_tick.symbol] = first_tick.mid_price;
        stats_.first_tick_time = first_tick.timestamp;
        for (auto& strategy : strategies_) {
            strategy->on_start(initial_prices);
        }
        if (!process_tick(first_tick)) {
            return false;
        }
    }
    HistoricalTick tick;
    while (data_parser_->read_next_tick(tick)) {
        if (!is_tick_in_replay_window(tick)) continue;
        if (!process_tick(tick)) break;
        if (config_.print_progress && ticks_processed_ % config_.progress_interval == 0) {
            std::cout << "Processed " << ticks_processed_ << " ticks..." << std::endl;
        }
    }
    stats_.last_tick_time = current_replay_time_;
    stats_.total_ticks = ticks_processed_;
    auto replay_end = std::chrono::steady_clock::now();
    stats_.replay_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        replay_end - replay_start_time_);
    stats_.ticks_per_second = stats_.total_ticks /
        (stats_.replay_duration.count() / 1000.0);
    for (auto& strategy : strategies_) {
        strategy->on_finish();
    }
    matching_engine_->stop();
    return true;
}
void TickReplayEngine::stop_backtest() {
    if (matching_engine_) {
        matching_engine_->stop();
    }
}
bool TickReplayEngine::process_tick(const HistoricalTick& tick) {
    update_replay_time(tick);
    handle_time_acceleration();
    update_strategies(tick);
    stats_.total_volume += tick.last_size;
    stats_.total_notional += tick.last_price * tick.last_size;
    if (tick.is_trade_tick) {
        stats_.total_trades++;
    }
    ticks_processed_++;
    return true;
}
void TickReplayEngine::update_strategies(const HistoricalTick& tick) {
    for (auto& strategy : strategies_) {
        strategy->on_tick(tick);
    }
}
void TickReplayEngine::apply_slippage_model(matching::ExecutionReport& report) {
    if (!config_.enable_slippage_model) return;
    double slippage_factor = 0.0001;
    if (report.side == core::Side::BUY) {
        report.price *= (1.0 + slippage_factor);
    } else {
        report.price *= (1.0 - slippage_factor);
    }
}
bool TickReplayEngine::is_tick_in_replay_window(const HistoricalTick& tick) const {
    return tick.timestamp >= config_.start_time && tick.timestamp <= config_.end_time;
}
void TickReplayEngine::update_replay_time(const HistoricalTick& tick) {
    current_replay_time_ = tick.timestamp;
}
void TickReplayEngine::handle_time_acceleration() {
    if (config_.time_acceleration > 1.0) {
    }
}
void TickReplayEngine::setup_matching_engine_callbacks() {
    matching_engine_->set_execution_callback(
        [this](const matching::ExecutionReport& report) {
            this->on_execution_report(report);
        });
    matching_engine_->set_fill_callback(
        [this](const matching::Fill& fill) {
            this->on_fill(fill);
        });
}
void TickReplayEngine::on_execution_report(const matching::ExecutionReport& report) {
    auto modified_report = report;
    apply_slippage_model(modified_report);
    for (auto& strategy : strategies_) {
        strategy->on_execution_report(modified_report);
    }
}
void TickReplayEngine::on_fill(const matching::Fill& fill) {
    double commission = fill.quantity * fill.price * config_.commission_rate;
    for (auto& strategy : strategies_) {
        strategy->on_fill(fill);
    }
}
void TickReplayEngine::print_backtest_summary() const {
    std::cout << "\n=== BACKTEST SUMMARY ===" << std::endl;
    std::cout << "Total ticks processed: " << stats_.total_ticks << std::endl;
    std::cout << "Total trades: " << stats_.total_trades << std::endl;
    std::cout << "Total volume: " << stats_.total_volume << std::endl;
    std::cout << "Total notional: $" << std::fixed << std::setprecision(2)
              << stats_.total_notional << std::endl;
    std::cout << "Replay duration: " << stats_.replay_duration.count() << " ms" << std::endl;
    std::cout << "Processing speed: " << std::fixed << std::setprecision(0)
              << stats_.ticks_per_second << " ticks/sec" << std::endl;
    for (const auto& strategy : strategies_) {
        std::cout << "\nStrategy: " << strategy->get_strategy_name() << std::endl;
        auto params = strategy->get_parameters();
        for (const auto& [key, value] : params) {
            std::cout << "  " << key << ": " << value << std::endl;
        }
    }
    std::cout << "\nPnL Summary:" << std::endl;
    std::cout << "Total trades: " << pnl_calculator_->get_trade_count() << std::endl;
    std::cout << "Total PnL: $" << std::fixed << std::setprecision(2)
              << pnl_calculator_->get_total_pnl() << std::endl;
}
void TickReplayEngine::export_results(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return;
    file << "backtest_summary,value\n";
    file << "total_ticks," << stats_.total_ticks << "\n";
    file << "total_trades," << stats_.total_trades << "\n";
    file << "total_volume," << stats_.total_volume << "\n";
    file << "total_notional," << stats_.total_notional << "\n";
    file << "replay_duration_ms," << stats_.replay_duration.count() << "\n";
    file << "ticks_per_second," << stats_.ticks_per_second << "\n";
    file << "total_pnl," << pnl_calculator_->get_total_pnl() << "\n";
}
void TickReplayEngine::export_trade_log(const std::string& filename) const {
    std::ofstream file(filename);
    file << "timestamp,symbol,side,price,quantity,pnl\n";
    file << "# Trade log export - implement based on PnL calculator data\n";
}
void BacktestingSuite::add_backtest_job(const std::string& name, const std::string& data_file,
                                       std::unique_ptr<BacktestingStrategy> strategy,
                                       const TickReplayEngine::ReplayConfig& config) {
    BacktestJob job;
    job.name = name;
    job.data_file = data_file;
    job.strategy = std::move(strategy);
    job.config = config;
    jobs_.push_back(std::move(job));
}
bool BacktestingSuite::run_all_backtests() {
    bool all_successful = true;
    for (size_t i = 0; i < jobs_.size(); ++i) {
        std::cout << "Running backtest " << (i + 1) << "/" << jobs_.size()
                  << ": " << jobs_[i].name << std::endl;
        if (!run_backtest(i)) {
            all_successful = false;
            std::cerr << "Backtest failed: " << jobs_[i].name << std::endl;
        }
    }
    return all_successful;
}
bool BacktestingSuite::run_backtest(size_t job_index) {
    if (job_index >= jobs_.size()) return false;
    auto& job = jobs_[job_index];
    std::unique_ptr<MarketDataParser> parser;
    if (job.data_file.ends_with(".csv")) {
        parser = std::make_unique<CSVMarketDataParser>();
    } else if (job.data_file.ends_with(".bin")) {
        parser = std::make_unique<BinaryMarketDataParser>();
    } else {
        std::cerr << "Unsupported data file format: " << job.data_file << std::endl;
        return false;
    }
    if (!parser->open(job.data_file)) {
        std::cerr << "Failed to open data file: " << job.data_file << std::endl;
        return false;
    }
    TickReplayEngine engine(std::move(parser));
    engine.set_replay_config(job.config);
    engine.add_strategy(std::move(job.strategy));
    bool success = engine.run_backtest();
    if (success) {
        job.completed = true;
        calculate_performance_metrics(job, engine.get_pnl_calculator());
    }
    return success;
}
void BacktestingSuite::calculate_performance_metrics(BacktestJob& job, analytics::PnLCalculator* pnl_calc) {
    job.final_pnl = pnl_calc->get_total_pnl();
    job.total_trades = pnl_calc->get_trade_count();
    job.max_drawdown = 0.0;
    job.sharpe_ratio = 0.0;
}
double BacktestingSuite::calculate_sharpe_ratio(const std::vector<double>& returns) {
    if (returns.size() < 2) return 0.0;
    double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double variance = 0.0;
    for (double ret : returns) {
        variance += (ret - mean_return) * (ret - mean_return);
    }
    variance /= (returns.size() - 1);
    double std_dev = std::sqrt(variance);
    return std_dev > 0 ? mean_return / std_dev : 0.0;
}
double BacktestingSuite::calculate_max_drawdown(const std::vector<double>& equity_curve) {
    if (equity_curve.empty()) return 0.0;
    double max_dd = 0.0;
    double peak = equity_curve[0];
    for (double value : equity_curve) {
        if (value > peak) {
            peak = value;
        } else {
            double dd = (peak - value) / peak;
            max_dd = std::max(max_dd, dd);
        }
    }
    return max_dd;
}
void BacktestingSuite::print_suite_summary() const {
    std::cout << "\n=== BACKTESTING SUITE SUMMARY ===" << std::endl;
    std::cout << "Total jobs: " << jobs_.size() << std::endl;
    size_t completed = 0;
    for (const auto& job : jobs_) {
        if (job.completed) completed++;
    }
    std::cout << "Completed: " << completed << std::endl;
    for (const auto& job : jobs_) {
        std::cout << "\n" << job.name << ":" << std::endl;
        std::cout << "  Status: " << (job.completed ? "COMPLETED" : "FAILED") << std::endl;
        if (job.completed) {
            std::cout << "  Final PnL: $" << std::fixed << std::setprecision(2) << job.final_pnl << std::endl;
            std::cout << "  Total Trades: " << job.total_trades << std::endl;
            std::cout << "  Max Drawdown: " << std::fixed << std::setprecision(2) << job.max_drawdown * 100 << "%" << std::endl;
            std::cout << "  Sharpe Ratio: " << std::fixed << std::setprecision(2) << job.sharpe_ratio << std::endl;
        }
    }
}
void BacktestingSuite::export_comparative_results(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) return;
    file << "strategy_name,data_file,completed,final_pnl,total_trades,max_drawdown,sharpe_ratio\n";
    for (const auto& job : jobs_) {
        file << job.name << ","
             << job.data_file << ","
             << (job.completed ? "true" : "false") << ","
             << job.final_pnl << ","
             << job.total_trades << ","
             << job.max_drawdown << ","
             << job.sharpe_ratio << "\n";
    }
}
void BacktestingSuite::run_parameter_sweep(const std::string& strategy_name,
                                         const std::string& data_file,
                                         const std::map<std::string, std::vector<double>>& param_ranges) {
    std::cout << "Parameter sweep for " << strategy_name << " not yet implemented" << std::endl;
}
}
}
