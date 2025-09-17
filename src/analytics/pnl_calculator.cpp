#include "hft/analytics/pnl_calculator.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <ctime>

using namespace hft::core;

// Type aliases to match the implementation
using TradeExecution = hft::core::TradeExecution;
using Price = hft::core::Price;
using Quantity = hft::core::Quantity;
using TradingMetrics = hft::core::TradingMetrics;

namespace hft {
namespace analytics {

PnLCalculator::PnLCalculator() {
    reset();
}

void PnLCalculator::add_trade(const TradeExecution& trade) {
    trades_.push_back(trade);
    update_position(trade);
    calculate_realized_pnl(trade);
}

void PnLCalculator::update_market_price(const std::string& symbol, Price market_price) {
    market_prices_[symbol] = market_price;
    calculate_unrealized_pnl();
}

void PnLCalculator::add_position(const std::string& symbol, Quantity quantity, Price avg_price) {
    Position& pos = positions_[symbol];
    pos.symbol = symbol;
    pos.quantity = quantity;
    pos.avg_price = avg_price;
    pos.market_value = quantity * avg_price;
    pos.unrealized_pnl = 0.0;
    pos.realized_pnl = 0.0;
}

TradingMetrics PnLCalculator::calculate_metrics() const {
    TradingMetrics metrics;
    
    // Basic metrics
    metrics.trades_count = trades_.size();
    metrics.realized_pnl = total_realized_pnl_;
    metrics.unrealized_pnl = calculate_total_unrealized_pnl();
    metrics.total_pnl = metrics.realized_pnl + metrics.unrealized_pnl;
    
    // Volume and notional
    metrics.total_volume = 0;
    metrics.total_notional = 0.0;
    for (const auto& trade : trades_) {
        metrics.total_volume += trade.quantity;
        metrics.total_notional += trade.price * trade.quantity;
    }
    
    // Slippage analysis
    calculate_slippage_metrics(metrics);
    
    // Performance metrics
    calculate_performance_metrics(metrics);
    
    return metrics;
}

SlippageAnalysis PnLCalculator::calculate_slippage(const std::string& symbol, 
                                                  const std::vector<TradeExecution>& period_trades) const {
    SlippageAnalysis analysis;
    analysis.symbol = symbol;
    analysis.trade_count = period_trades.size();
    
    if (period_trades.empty()) return analysis;
    
    std::vector<double> slippages;
    double total_slippage = 0.0;
    double total_volume = 0.0;
    
    for (const auto& trade : period_trades) {
        // Get reference price (using market price at trade time)
        auto market_it = market_prices_.find(trade.symbol);
        if (market_it == market_prices_.end()) continue;
        
        Price reference_price = market_it->second;
        
        // Calculate slippage based on trade type
        double slippage = 0.0;
        if (trade.trade_type == TradeType::AGGRESSIVE_BUY || 
            trade.trade_type == TradeType::AGGRESSIVE_SELL) {
            // For aggressive trades, slippage is difference from reference price
            slippage = std::abs(trade.price - reference_price) / reference_price;
        }
        
        slippages.push_back(slippage);
        total_slippage += slippage * trade.quantity;
        total_volume += trade.quantity;
        
        analysis.total_cost += slippage * trade.price * trade.quantity;
    }
    
    if (!slippages.empty()) {
        // Sort for percentile calculations
        std::sort(slippages.begin(), slippages.end());
        
        analysis.avg_slippage = total_slippage / total_volume;
        analysis.median_slippage = slippages[slippages.size() / 2];
        analysis.p95_slippage = slippages[static_cast<size_t>(slippages.size() * 0.95)];
        analysis.max_slippage = slippages.back();
    }
    
    return analysis;
}

RiskMetrics PnLCalculator::calculate_risk_metrics() const {
    RiskMetrics metrics;
    
    if (trades_.empty()) return metrics;
    
    // Calculate returns for each day
    std::vector<double> daily_returns;
    calculate_daily_returns(daily_returns);
    
    if (daily_returns.size() < 2) return metrics;
    
    // Calculate basic statistics
    double sum = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0);
    double mean = sum / daily_returns.size();
    
    double variance = 0.0;
    for (double ret : daily_returns) {
        variance += std::pow(ret - mean, 2);
    }
    variance /= (daily_returns.size() - 1);
    
    metrics.volatility = std::sqrt(variance) * std::sqrt(252); // Annualized
    
    // Sharpe ratio (assuming 0% risk-free rate)
    metrics.sharpe_ratio = (mean * 252) / metrics.volatility;
    
    // Maximum drawdown
    calculate_max_drawdown(daily_returns, metrics);
    
    // VaR calculation (95% confidence)
    std::vector<double> sorted_returns = daily_returns;
    std::sort(sorted_returns.begin(), sorted_returns.end());
    size_t var_index = static_cast<size_t>(sorted_returns.size() * 0.05);
    metrics.var_95 = sorted_returns[var_index];
    
    // Win rate
    size_t winning_days = 0;
    for (double ret : daily_returns) {
        if (ret > 0) winning_days++;
    }
    metrics.win_rate = static_cast<double>(winning_days) / daily_returns.size();
    
    return metrics;
}

std::vector<Position> PnLCalculator::get_positions() const {
    std::vector<Position> result;
    for (const auto& pair : positions_) {
        result.push_back(pair.second);
    }
    return result;
}

void PnLCalculator::reset() {
    trades_.clear();
    positions_.clear();
    market_prices_.clear();
    total_realized_pnl_ = 0.0;
}

void PnLCalculator::update_position(const TradeExecution& trade) {
    Position& pos = positions_[trade.symbol];
    pos.symbol = trade.symbol;
    
    if (trade.trade_type == TradeType::AGGRESSIVE_BUY || 
        trade.trade_type == TradeType::PASSIVE_BUY) {
        // Buying - increase position
        if (pos.quantity >= 0) {
            // Same direction - average the price
            pos.avg_price = ((pos.avg_price * pos.quantity) + (trade.price * trade.quantity)) / 
                           (pos.quantity + trade.quantity);
            pos.quantity += trade.quantity;
        } else {
            // Opposite direction - reduce short position or flip
            if (trade.quantity <= static_cast<Quantity>(std::abs(pos.quantity))) {
                pos.quantity += trade.quantity;
            } else {
                // Flip to long
                Quantity excess = trade.quantity - static_cast<Quantity>(std::abs(pos.quantity));
                pos.quantity = excess;
                pos.avg_price = trade.price;
            }
        }
    } else {
        // Selling - decrease position
        if (pos.quantity <= 0) {
            // Same direction (short) - average the price
            pos.avg_price = ((pos.avg_price * std::abs(pos.quantity)) + (trade.price * trade.quantity)) / 
                           (std::abs(pos.quantity) + trade.quantity);
            pos.quantity -= trade.quantity;
        } else {
            // Opposite direction - reduce long position or flip
            if (trade.quantity <= pos.quantity) {
                pos.quantity -= trade.quantity;
            } else {
                // Flip to short
                Quantity excess = trade.quantity - pos.quantity;
                pos.quantity = -static_cast<int64_t>(excess);
                pos.avg_price = trade.price;
            }
        }
    }
}

void PnLCalculator::calculate_realized_pnl(const TradeExecution& trade) {
    // Simplified realized P&L calculation
    // In practice, this would use FIFO, LIFO, or average cost accounting
    
    Position& pos = positions_[trade.symbol];
    
    // Get reference price for the position
    auto market_it = market_prices_.find(trade.symbol);
    if (market_it != market_prices_.end()) {
        Price reference_price = market_it->second;
        
        // Calculate P&L based on position change
        if ((trade.trade_type == TradeType::AGGRESSIVE_SELL || 
             trade.trade_type == TradeType::PASSIVE_SELL) && pos.quantity > 0) {
            // Selling from long position
            double pnl = (trade.price - pos.avg_price) * trade.quantity;
            pos.realized_pnl += pnl;
            total_realized_pnl_ += pnl;
        } else if ((trade.trade_type == TradeType::AGGRESSIVE_BUY || 
                   trade.trade_type == TradeType::PASSIVE_BUY) && pos.quantity < 0) {
            // Buying to cover short position
            double pnl = (pos.avg_price - trade.price) * trade.quantity;
            pos.realized_pnl += pnl;
            total_realized_pnl_ += pnl;
        }
    }
}

void PnLCalculator::calculate_unrealized_pnl() {
    for (auto& pair : positions_) {
        Position& pos = pair.second;
        
        auto market_it = market_prices_.find(pos.symbol);
        if (market_it != market_prices_.end()) {
            Price market_price = market_it->second;
            pos.market_value = pos.quantity * market_price;
            pos.unrealized_pnl = (market_price - pos.avg_price) * pos.quantity;
        }
    }
}

double PnLCalculator::calculate_total_unrealized_pnl() const {
    double total = 0.0;
    for (const auto& pair : positions_) {
        total += pair.second.unrealized_pnl;
    }
    return total;
}

void PnLCalculator::calculate_slippage_metrics(TradingMetrics& metrics) const {
    if (trades_.empty()) return;
    
    double total_slippage = 0.0;
    size_t slippage_count = 0;
    
    for (const auto& trade : trades_) {
        auto market_it = market_prices_.find(trade.symbol);
        if (market_it == market_prices_.end()) continue;
        
        Price reference_price = market_it->second;
        double slippage = std::abs(trade.price - reference_price) / reference_price;
        total_slippage += slippage;
        slippage_count++;
    }
    
    if (slippage_count > 0) {
        metrics.avg_slippage = total_slippage / slippage_count;
        metrics.total_slippage = total_slippage;
    }
}

void PnLCalculator::calculate_performance_metrics(TradingMetrics& metrics) const {
    std::vector<double> daily_returns;
    calculate_daily_returns(daily_returns);
    
    if (daily_returns.size() < 2) return;
    
    // Calculate volatility
    double sum = std::accumulate(daily_returns.begin(), daily_returns.end(), 0.0);
    double mean = sum / daily_returns.size();
    
    double variance = 0.0;
    for (double ret : daily_returns) {
        variance += std::pow(ret - mean, 2);
    }
    variance /= (daily_returns.size() - 1);
    
    metrics.volatility = std::sqrt(variance) * std::sqrt(252); // Annualized
    
    // Sharpe ratio
    metrics.sharpe_ratio = (mean * 252) / metrics.volatility;
    
    // Win rate
    size_t winning_days = 0;
    for (double ret : daily_returns) {
        if (ret > 0) winning_days++;
    }
    metrics.win_rate = static_cast<double>(winning_days) / daily_returns.size();
    
    // Max drawdown
    RiskMetrics risk;
    calculate_max_drawdown(daily_returns, risk);
    metrics.max_drawdown = risk.max_drawdown;
}

void PnLCalculator::calculate_daily_returns(std::vector<double>& returns) const {
    // Group trades by day and calculate daily P&L
    // This is a simplified implementation
    std::map<std::string, double> daily_pnl;
    
    for (const auto& trade : trades_) {
        // Convert timestamp to date string (simplified)
        auto time_t = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(trade.timestamp));
        
        char date_str[11];
        std::strftime(date_str, sizeof(date_str), "%Y-%m-%d", std::gmtime(&time_t));
        std::string date(date_str);
        
        // Calculate trade P&L (simplified)
        Position pos = positions_.at(trade.symbol);
        double trade_pnl = (trade.price - pos.avg_price) * trade.quantity;
        daily_pnl[date] += trade_pnl;
    }
    
    for (const auto& pair : daily_pnl) {
        returns.push_back(pair.second);
    }
}

void PnLCalculator::calculate_max_drawdown(const std::vector<double>& returns, RiskMetrics& metrics) const {
    if (returns.empty()) return;
    
    double cumulative = 0.0;
    double peak = 0.0;
    double max_drawdown = 0.0;
    
    for (double ret : returns) {
        cumulative += ret;
        if (cumulative > peak) {
            peak = cumulative;
        }
        double drawdown = peak - cumulative;
        if (drawdown > max_drawdown) {
            max_drawdown = drawdown;
        }
    }
    
    metrics.max_drawdown = max_drawdown;
}

} // namespace analytics
} // namespace hft