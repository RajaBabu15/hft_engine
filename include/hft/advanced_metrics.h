#pragma once

#include "hft/types.h"
#include "hft/order.h"
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <numeric>

namespace hft {

// Trade record for detailed analysis
struct TradeRecord {
    uint64_t trade_id;
    uint64_t timestamp_ns;
    Symbol symbol;
    Side side;
    Price price;
    Quantity qty;
    Price intended_price;  // For slippage calculation
    int64_t pnl_cents;     // P&L in cents
    uint32_t user_id;
    std::string strategy_name;
    
    // Execution metrics
    uint64_t order_to_fill_latency_ns;
    uint64_t market_impact_bps;  // Market impact in basis points
    
    TradeRecord() = default;
    TradeRecord(uint64_t id, uint64_t ts, Symbol sym, Side s, Price p, Quantity q, 
                Price intended, int64_t pnl, uint32_t uid, const std::string& strategy)
        : trade_id(id), timestamp_ns(ts), symbol(sym), side(s), price(p), qty(q),
          intended_price(intended), pnl_cents(pnl), user_id(uid), strategy_name(strategy),
          order_to_fill_latency_ns(0), market_impact_bps(0) {}
};

// Position tracking for P&L calculation
struct Position {
    Symbol symbol;
    int64_t quantity = 0;         // Signed quantity (+ for long, - for short)
    int64_t realized_pnl_cents = 0;
    int64_t unrealized_pnl_cents = 0;
    Price average_cost_price = 0;  // Volume-weighted average price
    uint64_t last_update_time_ns = 0;
    Price last_market_price = 0;
    
    void update_position(Side side, Price price, Quantity qty, uint64_t timestamp_ns) {
        int64_t trade_qty = (side == Side::BUY) ? static_cast<int64_t>(qty) : -static_cast<int64_t>(qty);
        
        if (quantity == 0) {
            // New position
            quantity = trade_qty;
            average_cost_price = price;
        } else if ((quantity > 0 && trade_qty > 0) || (quantity < 0 && trade_qty < 0)) {
            // Adding to existing position - update average cost
            int64_t old_value = quantity * average_cost_price;
            int64_t new_value = trade_qty * price;
            quantity += trade_qty;
            if (quantity != 0) {
                average_cost_price = static_cast<Price>((old_value + new_value) / quantity);
            }
        } else {
            // Reducing or closing position - realize P&L
            int64_t qty_to_close = std::min(std::abs(quantity), std::abs(trade_qty));
            int64_t realized_pnl = 0;
            
            if (quantity > 0) { // Long position, selling
                realized_pnl = qty_to_close * (price - average_cost_price);
            } else { // Short position, buying
                realized_pnl = qty_to_close * (average_cost_price - price);
            }
            
            realized_pnl_cents += realized_pnl;
            quantity += trade_qty;
            
            if (quantity == 0) {
                average_cost_price = 0;
            }
        }
        
        last_update_time_ns = timestamp_ns;
        last_market_price = price;
    }
    
    void mark_to_market(Price market_price, uint64_t timestamp_ns) {
        if (quantity != 0) {
            if (quantity > 0) {
                unrealized_pnl_cents = quantity * (market_price - average_cost_price);
            } else {
                unrealized_pnl_cents = std::abs(quantity) * (average_cost_price - market_price);
            }
        } else {
            unrealized_pnl_cents = 0;
        }
        
        last_market_price = market_price;
        last_update_time_ns = timestamp_ns;
    }
    
    int64_t get_total_pnl_cents() const {
        return realized_pnl_cents + unrealized_pnl_cents;
    }
};

// Drawdown tracking
struct DrawdownPeriod {
    uint64_t start_time_ns;
    uint64_t end_time_ns;
    int64_t peak_value_cents;
    int64_t trough_value_cents;
    int64_t drawdown_cents;
    double drawdown_percentage;
    uint64_t duration_ms;
    
    DrawdownPeriod(uint64_t start, int64_t peak) 
        : start_time_ns(start), end_time_ns(start), peak_value_cents(peak), 
          trough_value_cents(peak), drawdown_cents(0), drawdown_percentage(0.0), duration_ms(0) {}
};

// Performance metrics calculator
class AdvancedMetrics {
private:
    mutable std::mutex metrics_mutex_;
    
    // Trade tracking
    std::vector<TradeRecord> trades_;
    std::atomic<uint64_t> next_trade_id_{1};
    
    // Position tracking
    std::unordered_map<Symbol, Position> positions_;
    std::atomic<int64_t> total_realized_pnl_cents_{0};
    std::atomic<int64_t> total_unrealized_pnl_cents_{0};
    
    // Performance time series (for Sharpe ratio, etc.)
    std::vector<std::pair<uint64_t, int64_t>> pnl_time_series_;  // timestamp_ns, cumulative_pnl_cents
    std::vector<DrawdownPeriod> drawdown_periods_;
    
    // Slippage tracking
    std::atomic<int64_t> total_slippage_cents_{0};
    std::atomic<uint64_t> slippage_trade_count_{0};
    
    // Volume and frequency metrics
    std::atomic<uint64_t> total_volume_{0};
    std::atomic<uint64_t> total_trade_count_{0};
    std::atomic<uint64_t> profitable_trades_{0};
    std::atomic<uint64_t> losing_trades_{0};
    
    // Latency tracking
    std::vector<uint64_t> execution_latencies_ns_;
    
    // Market impact tracking
    std::vector<uint64_t> market_impacts_bps_;
    
    // Configuration
    uint64_t metrics_start_time_ns_;
    double risk_free_rate_ = 0.02;  // 2% annual risk-free rate
    
public:
    AdvancedMetrics() : metrics_start_time_ns_(now_ns()) {}
    
    // Record a trade for analysis
    void record_trade(Symbol symbol, Side side, Price executed_price, Quantity qty, 
                     Price intended_price, uint32_t user_id = 1, 
                     const std::string& strategy_name = "default",
                     uint64_t order_to_fill_latency_ns = 0) {
        
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        
        uint64_t trade_id = next_trade_id_.fetch_add(1, std::memory_order_relaxed);
        uint64_t timestamp_ns = now_ns();
        
        // Calculate immediate P&L impact
        int64_t trade_pnl = 0;  // Will be calculated by position update
        
        // Update position and calculate P&L
        Position& position = positions_[symbol];
        int64_t old_realized_pnl = position.realized_pnl_cents;
        position.update_position(side, executed_price, qty, timestamp_ns);
        trade_pnl = position.realized_pnl_cents - old_realized_pnl;
        
        // Create trade record
        TradeRecord trade(trade_id, timestamp_ns, symbol, side, executed_price, qty,
                         intended_price, trade_pnl, user_id, strategy_name);
        
        // Calculate slippage
        int64_t slippage_cents = 0;
        if (side == Side::BUY) {
            slippage_cents = static_cast<int64_t>(qty) * (executed_price - intended_price);
        } else {
            slippage_cents = static_cast<int64_t>(qty) * (intended_price - executed_price);
        }\n        \n        if (slippage_cents != 0) {\n            total_slippage_cents_.fetch_add(slippage_cents, std::memory_order_relaxed);\n            slippage_trade_count_.fetch_add(1, std::memory_order_relaxed);\n        }\n        \n        // Calculate market impact\n        if (intended_price > 0) {\n            uint64_t impact_bps = static_cast<uint64_t>(\n                std::abs(executed_price - intended_price) * 10000.0 / intended_price);\n            trade.market_impact_bps = impact_bps;\n            market_impacts_bps_.push_back(impact_bps);\n        }\n        \n        trade.order_to_fill_latency_ns = order_to_fill_latency_ns;\n        if (order_to_fill_latency_ns > 0) {\n            execution_latencies_ns_.push_back(order_to_fill_latency_ns);\n        }\n        \n        trades_.push_back(trade);\n        \n        // Update aggregate metrics\n        total_volume_.fetch_add(qty, std::memory_order_relaxed);\n        total_trade_count_.fetch_add(1, std::memory_order_relaxed);\n        \n        if (trade_pnl > 0) {\n            profitable_trades_.fetch_add(1, std::memory_order_relaxed);\n        } else if (trade_pnl < 0) {\n            losing_trades_.fetch_add(1, std::memory_order_relaxed);\n        }\n        \n        // Update P&L time series\n        int64_t total_pnl = get_total_pnl_cents();\n        pnl_time_series_.emplace_back(timestamp_ns, total_pnl);\n        \n        // Update drawdown tracking\n        update_drawdown_tracking(total_pnl, timestamp_ns);\n    }\n    \n    // Mark positions to market for unrealized P&L\n    void mark_to_market(const std::unordered_map<Symbol, Price>& market_prices) {\n        std::lock_guard<std::mutex> lock(metrics_mutex_);\n        \n        uint64_t timestamp_ns = now_ns();\n        int64_t total_unrealized = 0;\n        \n        for (auto& [symbol, position] : positions_) {\n            auto price_it = market_prices.find(symbol);\n            if (price_it != market_prices.end()) {\n                position.mark_to_market(price_it->second, timestamp_ns);\n            }\n            total_unrealized += position.unrealized_pnl_cents;\n        }\n        \n        total_unrealized_pnl_cents_.store(total_unrealized, std::memory_order_relaxed);\n    }\n    \n    // Get comprehensive performance report\n    struct PerformanceReport {\n        // Basic metrics\n        uint64_t total_trades = 0;\n        uint64_t total_volume = 0;\n        int64_t total_pnl_cents = 0;\n        int64_t realized_pnl_cents = 0;\n        int64_t unrealized_pnl_cents = 0;\n        \n        // P&L breakdown\n        uint64_t profitable_trades = 0;\n        uint64_t losing_trades = 0;\n        double win_rate = 0.0;\n        int64_t largest_win_cents = 0;\n        int64_t largest_loss_cents = 0;\n        double profit_factor = 0.0;  // Gross profit / Gross loss\n        \n        // Risk metrics\n        double sharpe_ratio = 0.0;\n        double sortino_ratio = 0.0;\n        double calmar_ratio = 0.0;\n        int64_t max_drawdown_cents = 0;\n        double max_drawdown_percentage = 0.0;\n        uint64_t max_drawdown_duration_ms = 0;\n        double volatility_annualized = 0.0;\n        \n        // Execution metrics\n        int64_t total_slippage_cents = 0;\n        double average_slippage_cents = 0.0;\n        uint64_t average_execution_latency_ns = 0;\n        uint64_t p95_execution_latency_ns = 0;\n        uint64_t p99_execution_latency_ns = 0;\n        \n        // Market impact\n        uint64_t average_market_impact_bps = 0;\n        uint64_t p95_market_impact_bps = 0;\n        \n        // Frequency metrics\n        double trades_per_second = 0.0;\n        double volume_per_second = 0.0;\n        \n        // Time period\n        uint64_t start_time_ns = 0;\n        uint64_t end_time_ns = 0;\n        uint64_t duration_seconds = 0;\n        \n        std::string timestamp;\n    };\n    \n    PerformanceReport generate_report() const {\n        std::lock_guard<std::mutex> lock(metrics_mutex_);\n        \n        PerformanceReport report;\n        uint64_t now = now_ns();\n        \n        // Basic metrics\n        report.total_trades = total_trade_count_.load(std::memory_order_relaxed);\n        report.total_volume = total_volume_.load(std::memory_order_relaxed);\n        report.realized_pnl_cents = get_realized_pnl_cents();\n        report.unrealized_pnl_cents = total_unrealized_pnl_cents_.load(std::memory_order_relaxed);\n        report.total_pnl_cents = report.realized_pnl_cents + report.unrealized_pnl_cents;\n        \n        report.profitable_trades = profitable_trades_.load(std::memory_order_relaxed);\n        report.losing_trades = losing_trades_.load(std::memory_order_relaxed);\n        \n        if (report.total_trades > 0) {\n            report.win_rate = static_cast<double>(report.profitable_trades) / report.total_trades;\n        }\n        \n        // Calculate profit factor\n        int64_t gross_profit = 0, gross_loss = 0;\n        for (const auto& trade : trades_) {\n            if (trade.pnl_cents > 0) {\n                gross_profit += trade.pnl_cents;\n            } else if (trade.pnl_cents < 0) {\n                gross_loss += std::abs(trade.pnl_cents);\n            }\n        }\n        \n        if (gross_loss > 0) {\n            report.profit_factor = static_cast<double>(gross_profit) / gross_loss;\n        }\n        \n        // Find largest win/loss\n        for (const auto& trade : trades_) {\n            if (trade.pnl_cents > report.largest_win_cents) {\n                report.largest_win_cents = trade.pnl_cents;\n            }\n            if (trade.pnl_cents < report.largest_loss_cents) {\n                report.largest_loss_cents = trade.pnl_cents;\n            }\n        }\n        \n        // Risk metrics\n        report.sharpe_ratio = calculate_sharpe_ratio();\n        report.sortino_ratio = calculate_sortino_ratio();\n        report.volatility_annualized = calculate_annualized_volatility();\n        \n        // Drawdown metrics\n        if (!drawdown_periods_.empty()) {\n            auto max_dd_it = std::max_element(drawdown_periods_.begin(), drawdown_periods_.end(),\n                [](const DrawdownPeriod& a, const DrawdownPeriod& b) {\n                    return a.drawdown_cents < b.drawdown_cents;\n                });\n            \n            report.max_drawdown_cents = max_dd_it->drawdown_cents;\n            report.max_drawdown_percentage = max_dd_it->drawdown_percentage;\n            report.max_drawdown_duration_ms = max_dd_it->duration_ms;\n        }\n        \n        if (report.max_drawdown_cents > 0 && report.realized_pnl_cents > 0) {\n            report.calmar_ratio = static_cast<double>(report.realized_pnl_cents) / report.max_drawdown_cents;\n        }\n        \n        // Slippage metrics\n        report.total_slippage_cents = total_slippage_cents_.load(std::memory_order_relaxed);\n        uint64_t slippage_count = slippage_trade_count_.load(std::memory_order_relaxed);\n        if (slippage_count > 0) {\n            report.average_slippage_cents = static_cast<double>(report.total_slippage_cents) / slippage_count;\n        }\n        \n        // Execution latency metrics\n        if (!execution_latencies_ns_.empty()) {\n            std::vector<uint64_t> latencies = execution_latencies_ns_;\n            std::sort(latencies.begin(), latencies.end());\n            \n            uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0ULL);\n            report.average_execution_latency_ns = sum / latencies.size();\n            \n            report.p95_execution_latency_ns = latencies[static_cast<size_t>(latencies.size() * 0.95)];\n            report.p99_execution_latency_ns = latencies[static_cast<size_t>(latencies.size() * 0.99)];\n        }\n        \n        // Market impact metrics\n        if (!market_impacts_bps_.empty()) {\n            std::vector<uint64_t> impacts = market_impacts_bps_;\n            std::sort(impacts.begin(), impacts.end());\n            \n            uint64_t sum = std::accumulate(impacts.begin(), impacts.end(), 0ULL);\n            report.average_market_impact_bps = sum / impacts.size();\n            \n            report.p95_market_impact_bps = impacts[static_cast<size_t>(impacts.size() * 0.95)];\n        }\n        \n        // Time and frequency metrics\n        report.start_time_ns = metrics_start_time_ns_;\n        report.end_time_ns = now;\n        report.duration_seconds = (now - metrics_start_time_ns_) / 1000000000ULL;\n        \n        if (report.duration_seconds > 0) {\n            report.trades_per_second = static_cast<double>(report.total_trades) / report.duration_seconds;\n            report.volume_per_second = static_cast<double>(report.total_volume) / report.duration_seconds;\n        }\n        \n        // Timestamp\n        auto time_t = std::chrono::system_clock::to_time_t(\n            std::chrono::system_clock::now());\n        std::stringstream ss;\n        ss << std::put_time(std::localtime(&time_t), \"%Y-%m-%d %H:%M:%S\");\n        report.timestamp = ss.str();\n        \n        return report;\n    }\n    \n    // Export detailed trade data to CSV\n    void export_trades_to_csv(const std::string& filename) const {\n        std::lock_guard<std::mutex> lock(metrics_mutex_);\n        \n        std::ofstream file(filename);\n        if (!file.is_open()) {\n            throw std::runtime_error(\"Failed to open file: \" + filename);\n        }\n        \n        // CSV header\n        file << \"TradeID,Timestamp,Symbol,Side,Price,Quantity,IntendedPrice,PnL_Cents,\"\n             << \"UserID,Strategy,Slippage_Cents,ExecutionLatency_ns,MarketImpact_bps\\n\";\n        \n        for (const auto& trade : trades_) {\n            int64_t slippage_cents = 0;\n            if (trade.side == Side::BUY) {\n                slippage_cents = static_cast<int64_t>(trade.qty) * (trade.price - trade.intended_price);\n            } else {\n                slippage_cents = static_cast<int64_t>(trade.qty) * (trade.intended_price - trade.price);\n            }\n            \n            file << trade.trade_id << \",\"\n                 << trade.timestamp_ns << \",\"\n                 << trade.symbol << \",\"\n                 << (trade.side == Side::BUY ? \"BUY\" : \"SELL\") << \",\"\n                 << std::fixed << std::setprecision(4) << (trade.price / 10000.0) << \",\"\n                 << trade.qty << \",\"\n                 << std::fixed << std::setprecision(4) << (trade.intended_price / 10000.0) << \",\"\n                 << trade.pnl_cents << \",\"\n                 << trade.user_id << \",\"\n                 << trade.strategy_name << \",\"\n                 << slippage_cents << \",\"\n                 << trade.order_to_fill_latency_ns << \",\"\n                 << trade.market_impact_bps << \"\\n\";\n        }\n        \n        file.close();\n    }\n    \n    // Real-time metrics access\n    int64_t get_total_pnl_cents() const {\n        return get_realized_pnl_cents() + total_unrealized_pnl_cents_.load(std::memory_order_relaxed);\n    }\n    \n    int64_t get_realized_pnl_cents() const {\n        std::lock_guard<std::mutex> lock(metrics_mutex_);\n        int64_t total = 0;\n        for (const auto& [symbol, position] : positions_) {\n            total += position.realized_pnl_cents;\n        }\n        return total;\n    }\n    \n    double get_current_sharpe_ratio() const {\n        return calculate_sharpe_ratio();\n    }\n    \n    uint64_t get_trade_count() const {\n        return total_trade_count_.load(std::memory_order_relaxed);\n    }\n    \n    double get_win_rate() const {\n        uint64_t total = total_trade_count_.load(std::memory_order_relaxed);\n        uint64_t wins = profitable_trades_.load(std::memory_order_relaxed);\n        return total > 0 ? static_cast<double>(wins) / total : 0.0;\n    }\n    \n    // Print performance summary to console\n    void print_performance_summary() const {\n        auto report = generate_report();\n        \n        std::cout << \"\\n💰 PERFORMANCE SUMMARY (\" << report.timestamp << \")\" << std::endl;\n        std::cout << \"=================================================\" << std::endl;\n        \n        // P&L Summary\n        std::cout << \"📈 P&L Summary:\" << std::endl;\n        std::cout << \"   Total P&L: $\" << std::fixed << std::setprecision(2) \n                  << (report.total_pnl_cents / 100.0) << std::endl;\n        std::cout << \"   Realized P&L: $\" << (report.realized_pnl_cents / 100.0) << std::endl;\n        std::cout << \"   Unrealized P&L: $\" << (report.unrealized_pnl_cents / 100.0) << std::endl;\n        \n        // Trade Statistics\n        std::cout << \"\\n📊 Trade Statistics:\" << std::endl;\n        std::cout << \"   Total Trades: \" << report.total_trades << std::endl;\n        std::cout << \"   Win Rate: \" << std::setprecision(1) << (report.win_rate * 100.0) << \"%\" << std::endl;\n        std::cout << \"   Profitable Trades: \" << report.profitable_trades << std::endl;\n        std::cout << \"   Losing Trades: \" << report.losing_trades << std::endl;\n        std::cout << \"   Profit Factor: \" << std::setprecision(2) << report.profit_factor << std::endl;\n        std::cout << \"   Largest Win: $\" << (report.largest_win_cents / 100.0) << std::endl;\n        std::cout << \"   Largest Loss: $\" << (report.largest_loss_cents / 100.0) << std::endl;\n        \n        // Risk Metrics\n        std::cout << \"\\n⚠️  Risk Metrics:\" << std::endl;\n        std::cout << \"   Sharpe Ratio: \" << std::setprecision(2) << report.sharpe_ratio << std::endl;\n        std::cout << \"   Sortino Ratio: \" << report.sortino_ratio << std::endl;\n        std::cout << \"   Calmar Ratio: \" << report.calmar_ratio << std::endl;\n        std::cout << \"   Max Drawdown: $\" << (report.max_drawdown_cents / 100.0) \n                  << \" (\" << std::setprecision(1) << report.max_drawdown_percentage << \"%)\" << std::endl;\n        std::cout << \"   Volatility (annualized): \" << std::setprecision(1) \n                  << (report.volatility_annualized * 100.0) << \"%\" << std::endl;\n        \n        // Execution Metrics\n        std::cout << \"\\n⚡ Execution Metrics:\" << std::endl;\n        std::cout << \"   Total Slippage: $\" << std::setprecision(2) \n                  << (report.total_slippage_cents / 100.0) << std::endl;\n        std::cout << \"   Average Slippage: $\" << (report.average_slippage_cents / 100.0) << std::endl;\n        std::cout << \"   Avg Execution Latency: \" << std::setprecision(0) \n                  << (report.average_execution_latency_ns / 1000.0) << \" μs\" << std::endl;\n        std::cout << \"   P99 Execution Latency: \" \n                  << (report.p99_execution_latency_ns / 1000.0) << \" μs\" << std::endl;\n        std::cout << \"   Avg Market Impact: \" << report.average_market_impact_bps << \" bps\" << std::endl;\n        \n        // Frequency Metrics\n        std::cout << \"\\n🔄 Activity Metrics:\" << std::endl;\n        std::cout << \"   Trading Duration: \" << report.duration_seconds << \" seconds\" << std::endl;\n        std::cout << \"   Trades per Second: \" << std::setprecision(1) << report.trades_per_second << std::endl;\n        std::cout << \"   Volume per Second: \" << std::setprecision(0) << report.volume_per_second << std::endl;\n        std::cout << \"   Total Volume: \" << report.total_volume << \" shares\" << std::endl;\n    }\n    \n    void reset_metrics() {\n        std::lock_guard<std::mutex> lock(metrics_mutex_);\n        \n        trades_.clear();\n        positions_.clear();\n        pnl_time_series_.clear();\n        drawdown_periods_.clear();\n        execution_latencies_ns_.clear();\n        market_impacts_bps_.clear();\n        \n        next_trade_id_.store(1, std::memory_order_relaxed);\n        total_realized_pnl_cents_.store(0, std::memory_order_relaxed);\n        total_unrealized_pnl_cents_.store(0, std::memory_order_relaxed);\n        total_slippage_cents_.store(0, std::memory_order_relaxed);\n        slippage_trade_count_.store(0, std::memory_order_relaxed);\n        total_volume_.store(0, std::memory_order_relaxed);\n        total_trade_count_.store(0, std::memory_order_relaxed);\n        profitable_trades_.store(0, std::memory_order_relaxed);\n        losing_trades_.store(0, std::memory_order_relaxed);\n        \n        metrics_start_time_ns_ = now_ns();\n    }\n    \nprivate:\n    double calculate_sharpe_ratio() const {\n        if (pnl_time_series_.size() < 2) return 0.0;\n        \n        // Calculate returns from P&L time series\n        std::vector<double> returns;\n        for (size_t i = 1; i < pnl_time_series_.size(); ++i) {\n            int64_t pnl_diff = pnl_time_series_[i].second - pnl_time_series_[i-1].second;\n            double return_rate = static_cast<double>(pnl_diff) / 100.0; // Convert cents to dollars\n            returns.push_back(return_rate);\n        }\n        \n        if (returns.empty()) return 0.0;\n        \n        // Calculate mean return\n        double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();\n        \n        // Calculate standard deviation\n        double variance = 0.0;\n        for (double ret : returns) {\n            variance += (ret - mean_return) * (ret - mean_return);\n        }\n        variance /= returns.size();\n        double std_dev = std::sqrt(variance);\n        \n        if (std_dev == 0.0) return 0.0;\n        \n        // Annualize (assuming returns are per trade, convert to annual)\n        uint64_t duration_seconds = (now_ns() - metrics_start_time_ns_) / 1000000000ULL;\n        if (duration_seconds == 0) return 0.0;\n        \n        double trades_per_year = (returns.size() * 365.25 * 24 * 3600) / duration_seconds;\n        \n        double annualized_return = mean_return * trades_per_year;\n        double annualized_vol = std_dev * std::sqrt(trades_per_year);\n        \n        return (annualized_return - risk_free_rate_) / annualized_vol;\n    }\n    \n    double calculate_sortino_ratio() const {\n        if (pnl_time_series_.size() < 2) return 0.0;\n        \n        std::vector<double> returns;\n        for (size_t i = 1; i < pnl_time_series_.size(); ++i) {\n            int64_t pnl_diff = pnl_time_series_[i].second - pnl_time_series_[i-1].second;\n            double return_rate = static_cast<double>(pnl_diff) / 100.0;\n            returns.push_back(return_rate);\n        }\n        \n        if (returns.empty()) return 0.0;\n        \n        double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();\n        \n        // Calculate downside deviation (only negative returns)\n        double downside_variance = 0.0;\n        int negative_count = 0;\n        for (double ret : returns) {\n            if (ret < 0) {\n                downside_variance += ret * ret;\n                negative_count++;\n            }\n        }\n        \n        if (negative_count == 0) return std::numeric_limits<double>::max(); // No negative returns\n        \n        double downside_dev = std::sqrt(downside_variance / negative_count);\n        if (downside_dev == 0.0) return 0.0;\n        \n        // Annualize\n        uint64_t duration_seconds = (now_ns() - metrics_start_time_ns_) / 1000000000ULL;\n        if (duration_seconds == 0) return 0.0;\n        \n        double trades_per_year = (returns.size() * 365.25 * 24 * 3600) / duration_seconds;\n        double annualized_return = mean_return * trades_per_year;\n        double annualized_downside_dev = downside_dev * std::sqrt(trades_per_year);\n        \n        return (annualized_return - risk_free_rate_) / annualized_downside_dev;\n    }\n    \n    double calculate_annualized_volatility() const {\n        if (pnl_time_series_.size() < 2) return 0.0;\n        \n        std::vector<double> returns;\n        for (size_t i = 1; i < pnl_time_series_.size(); ++i) {\n            int64_t pnl_diff = pnl_time_series_[i].second - pnl_time_series_[i-1].second;\n            double return_rate = static_cast<double>(pnl_diff) / 100.0;\n            returns.push_back(return_rate);\n        }\n        \n        if (returns.size() < 2) return 0.0;\n        \n        double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();\n        \n        double variance = 0.0;\n        for (double ret : returns) {\n            variance += (ret - mean_return) * (ret - mean_return);\n        }\n        variance /= (returns.size() - 1);\n        \n        double std_dev = std::sqrt(variance);\n        \n        // Annualize\n        uint64_t duration_seconds = (now_ns() - metrics_start_time_ns_) / 1000000000ULL;\n        if (duration_seconds == 0) return 0.0;\n        \n        double trades_per_year = (returns.size() * 365.25 * 24 * 3600) / duration_seconds;\n        return std_dev * std::sqrt(trades_per_year);\n    }\n    \n    void update_drawdown_tracking(int64_t current_pnl_cents, uint64_t timestamp_ns) {\n        static int64_t running_peak = 0;\n        static bool in_drawdown = false;\n        static DrawdownPeriod current_drawdown(0, 0);\n        \n        if (current_pnl_cents > running_peak) {\n            running_peak = current_pnl_cents;\n            \n            if (in_drawdown) {\n                // End of drawdown period\n                current_drawdown.end_time_ns = timestamp_ns;\n                current_drawdown.duration_ms = (timestamp_ns - current_drawdown.start_time_ns) / 1000000;\n                drawdown_periods_.push_back(current_drawdown);\n                in_drawdown = false;\n            }\n        } else if (current_pnl_cents < running_peak) {\n            if (!in_drawdown) {\n                // Start of new drawdown\n                current_drawdown = DrawdownPeriod(timestamp_ns, running_peak);\n                in_drawdown = true;\n            }\n            \n            // Update current drawdown\n            current_drawdown.trough_value_cents = current_pnl_cents;\n            current_drawdown.drawdown_cents = running_peak - current_pnl_cents;\n            if (running_peak > 0) {\n                current_drawdown.drawdown_percentage = \n                    static_cast<double>(current_drawdown.drawdown_cents) / running_peak * 100.0;\n            }\n        }\n    }\n};\n\n} // namespace hft