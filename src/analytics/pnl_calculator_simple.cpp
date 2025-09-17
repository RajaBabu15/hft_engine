#include "hft/analytics/pnl_calculator.hpp"
#include <algorithm>
#include <cmath>
using namespace hft::core;
namespace hft {
namespace analytics {
PnLCalculator::PnLCalculator(double default_commission_rate)
    : default_commission_rate_(default_commission_rate) {
    reset();
}
void PnLCalculator::record_trade(const Trade& trade) {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    trades_.push_back(trade);
    update_position(trade);
    trade_count_.fetch_add(1);
}
void PnLCalculator::record_trade(OrderID order_id, const Symbol& symbol,
                                Side side, Price price, Quantity quantity,
                                const std::string& strategy_id) {
    Trade trade(order_id, symbol, side, price, quantity, std::chrono::high_resolution_clock::now());
    trade.strategy_id = strategy_id;
    record_trade(trade);
}
void PnLCalculator::update_market_price(const Symbol& symbol, Price price) {
    std::lock_guard<std::mutex> lock(prices_mutex_);
    current_prices_[symbol] = price;
    price_timestamps_[symbol] = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::mutex> pos_lock(positions_mutex_);
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        calculate_unrealized_pnl(it->second, price);
    }
}
void PnLCalculator::update_market_prices(const std::unordered_map<Symbol, Price>& prices) {
    for (const auto& [symbol, price] : prices) {
        update_market_price(symbol, price);
    }
}
Position PnLCalculator::get_position(const Symbol& symbol) const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    auto it = positions_.find(symbol);
    return (it != positions_.end()) ? it->second : Position();
}
std::unordered_map<Symbol, Position> PnLCalculator::get_all_positions() const {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    return positions_;
}
double PnLCalculator::get_net_position(const Symbol& symbol) const {
    return get_position(symbol).quantity;
}
double PnLCalculator::get_position_value(const Symbol& symbol) const {
    Position pos = get_position(symbol);
    return pos.quantity * pos.avg_entry_price;
}
double PnLCalculator::get_realized_pnl(const Symbol& symbol) const {
    if (symbol.empty()) {
        return total_realized_pnl_.load();
    }
    Position pos = get_position(symbol);
    return pos.realized_pnl;
}
double PnLCalculator::get_unrealized_pnl(const Symbol& symbol) const {
    if (symbol.empty()) {
        return total_unrealized_pnl_.load();
    }
    Position pos = get_position(symbol);
    return pos.unrealized_pnl;
}
double PnLCalculator::get_total_pnl(const Symbol& symbol) const {
    return get_realized_pnl(symbol) + get_unrealized_pnl(symbol);
}
std::vector<Trade> PnLCalculator::get_trades(const Symbol& symbol) const {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    if (symbol.empty()) {
        return trades_;
    }
    std::vector<Trade> filtered_trades;
    for (const auto& trade : trades_) {
        if (trade.symbol == symbol) {
            filtered_trades.push_back(trade);
        }
    }
    return filtered_trades;
}
std::vector<Trade> PnLCalculator::get_trades_in_range(TimePoint start, TimePoint end) const {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    std::vector<Trade> filtered_trades;
    for (const auto& trade : trades_) {
        if (trade.execution_time >= start && trade.execution_time <= end) {
            filtered_trades.push_back(trade);
        }
    }
    return filtered_trades;
}
void PnLCalculator::set_commission_rate(const Symbol& symbol, double rate) {
    commission_rates_[symbol] = rate;
}
void PnLCalculator::set_default_commission_rate(double rate) {
    default_commission_rate_ = rate;
}
void PnLCalculator::reset() {
    std::lock_guard<std::mutex> trades_lock(trades_mutex_);
    std::lock_guard<std::mutex> positions_lock(positions_mutex_);
    std::lock_guard<std::mutex> prices_lock(prices_mutex_);
    trades_.clear();
    positions_.clear();
    current_prices_.clear();
    price_timestamps_.clear();
    commission_rates_.clear();
    total_realized_pnl_.store(0.0);
    total_unrealized_pnl_.store(0.0);
    total_commission_.store(0.0);
    trade_count_.store(0);
}
void PnLCalculator::reset_positions() {
    std::lock_guard<std::mutex> lock(positions_mutex_);
    positions_.clear();
    total_unrealized_pnl_.store(0.0);
}
void PnLCalculator::reset_trades() {
    std::lock_guard<std::mutex> lock(trades_mutex_);
    trades_.clear();
    trade_count_.store(0);
    total_realized_pnl_.store(0.0);
    total_commission_.store(0.0);
}
void PnLCalculator::update_position(const Trade& trade) {
    Position& position = positions_[trade.symbol];
    position.symbol = trade.symbol;
    double trade_quantity = static_cast<double>(trade.quantity);
    if (trade.side == Side::SELL) {
        trade_quantity = -trade_quantity;
    }
    if ((position.quantity > 0 && trade_quantity > 0) ||
        (position.quantity < 0 && trade_quantity < 0)) {
        double total_cost = position.quantity * position.avg_entry_price +
                           trade_quantity * trade.executed_price;
        position.quantity += trade_quantity;
        position.avg_entry_price = (position.quantity != 0) ?
                                  total_cost / position.quantity : 0.0;
    } else if ((position.quantity > 0 && trade_quantity < 0) ||
               (position.quantity < 0 && trade_quantity > 0)) {
        double closing_quantity = std::min(std::abs(position.quantity), std::abs(trade_quantity));
        double realized_pnl = 0.0;
        if (position.quantity > 0) {
            realized_pnl = closing_quantity * (trade.executed_price - position.avg_entry_price);
        } else {
            realized_pnl = closing_quantity * (position.avg_entry_price - trade.executed_price);
        }
        position.realized_pnl += realized_pnl;
        double expected = total_realized_pnl_.load();
        while (!total_realized_pnl_.compare_exchange_weak(expected, expected + realized_pnl));
        position.quantity += trade_quantity;
        if (std::signbit(position.quantity) != std::signbit(position.quantity - trade_quantity)) {
            position.avg_entry_price = trade.executed_price;
        }
    } else {
        position.quantity = trade_quantity;
        position.avg_entry_price = trade.executed_price;
    }
    position.last_update_time = trade.execution_time;
    double commission = calculate_commission(trade);
    position.realized_pnl -= commission;
    double expected_comm = total_commission_.load();
    while (!total_commission_.compare_exchange_weak(expected_comm, expected_comm + commission));
}
void PnLCalculator::calculate_unrealized_pnl(Position& position, Price current_price) {
    if (std::abs(position.quantity) < 1e-6) {
        position.unrealized_pnl = 0.0;
        return;
    }
    double pnl = 0.0;
    if (position.quantity > 0) {
        pnl = position.quantity * (current_price - position.avg_entry_price);
    } else {
        pnl = std::abs(position.quantity) * (position.avg_entry_price - current_price);
    }
    double old_pnl = position.unrealized_pnl;
    position.unrealized_pnl = pnl;
    double expected_unreal = total_unrealized_pnl_.load();
    while (!total_unrealized_pnl_.compare_exchange_weak(expected_unreal, expected_unreal + (pnl - old_pnl)));
    update_risk_metrics(position, old_pnl);
}
double PnLCalculator::calculate_commission(const Trade& trade) {
    double rate = default_commission_rate_;
    auto it = commission_rates_.find(trade.symbol);
    if (it != commission_rates_.end()) {
        rate = it->second;
    }
    return trade.notional_value * rate;
}
void PnLCalculator::update_risk_metrics(Position& position, double previous_pnl) {
    double current_pnl = position.unrealized_pnl;
    if (current_pnl > position.max_favorable_excursion) {
        position.max_favorable_excursion = current_pnl;
    }
    if (current_pnl < position.max_adverse_excursion) {
        position.max_adverse_excursion = current_pnl;
    }
}
double calculate_pnl(Side side, Price entry_price, Price exit_price, Quantity quantity) {
    double qty = static_cast<double>(quantity);
    if (side == Side::BUY) {
        return qty * (exit_price - entry_price);
    } else {
        return qty * (entry_price - exit_price);
    }
}
double calculate_return(double initial_value, double final_value) {
    if (std::abs(initial_value) < 1e-6) return 0.0;
    return (final_value - initial_value) / std::abs(initial_value);
}
double calculate_annualized_return(double total_return, int days) {
    if (days <= 0) return 0.0;
    double daily_return = total_return / days;
    return daily_return * 252;
}
double calculate_slippage_bps(Price execution_price, Price reference_price, Side side) {
    if (std::abs(reference_price) < 1e-6) return 0.0;
    double slippage = 0.0;
    if (side == Side::BUY) {
        slippage = execution_price - reference_price;
    } else {
        slippage = reference_price - execution_price;
    }
    return (slippage / reference_price) * 10000.0;
}
double calculate_market_impact(Quantity order_size, Quantity average_daily_volume) {
    if (average_daily_volume == 0) return 0.0;
    double participation_rate = static_cast<double>(order_size) / static_cast<double>(average_daily_volume);
    return std::sqrt(participation_rate) * 100.0;
}
}
}