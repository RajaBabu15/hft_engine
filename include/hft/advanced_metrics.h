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
    [[maybe_unused]] double risk_free_rate_ = 0.02;  // 2% annual risk-free rate

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
        }
        
        if (slippage_cents != 0) {
            total_slippage_cents_.fetch_add(slippage_cents, std::memory_order_relaxed);
            slippage_trade_count_.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Calculate market impact
        if (intended_price > 0) {
            uint64_t impact_bps = static_cast<uint64_t>(
                std::abs(executed_price - intended_price) * 10000.0 / intended_price);
            trade.market_impact_bps = impact_bps;
            market_impacts_bps_.push_back(impact_bps);
        }
        
        trade.order_to_fill_latency_ns = order_to_fill_latency_ns;
        if (order_to_fill_latency_ns > 0) {
            execution_latencies_ns_.push_back(order_to_fill_latency_ns);
        }
        
        trades_.push_back(trade);
        
        // Update aggregate metrics
        total_volume_.fetch_add(qty, std::memory_order_relaxed);
        total_trade_count_.fetch_add(1, std::memory_order_relaxed);
        
        if (trade_pnl > 0) {
            profitable_trades_.fetch_add(1, std::memory_order_relaxed);
        } else if (trade_pnl < 0) {
            losing_trades_.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Update P&L time series (calculate directly to avoid recursive mutex lock)
        int64_t total_realized = 0;
        for (const auto& [symbol, position] : positions_) {
            total_realized += position.realized_pnl_cents;
        }
        int64_t total_pnl = total_realized + total_unrealized_pnl_cents_.load(std::memory_order_relaxed);
        pnl_time_series_.emplace_back(timestamp_ns, total_pnl);
        
        // Update drawdown tracking
        update_drawdown_tracking(total_pnl, timestamp_ns);
    }
    
    // Real-time metrics access
    int64_t get_total_pnl_cents() const {
        return get_realized_pnl_cents() + total_unrealized_pnl_cents_.load(std::memory_order_relaxed);
    }
    
    int64_t get_realized_pnl_cents() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        int64_t total = 0;
        for (const auto& [symbol, position] : positions_) {
            total += position.realized_pnl_cents;
        }
        return total;
    }
    
    uint64_t get_trade_count() const {
        return total_trade_count_.load(std::memory_order_relaxed);
    }
    
    double get_win_rate() const {
        uint64_t total = total_trade_count_.load(std::memory_order_relaxed);
        uint64_t wins = profitable_trades_.load(std::memory_order_relaxed);
        return total > 0 ? static_cast<double>(wins) / total : 0.0;
    }
    
    // Get slippage metrics
    int64_t get_total_slippage_cents() const {
        return total_slippage_cents_.load(std::memory_order_relaxed);
    }
    
    double get_average_slippage_cents() const {
        uint64_t count = slippage_trade_count_.load(std::memory_order_relaxed);
        int64_t total = total_slippage_cents_.load(std::memory_order_relaxed);
        return count > 0 ? static_cast<double>(total) / count : 0.0;
    }
    
    // Get volume metrics
    uint64_t get_total_volume() const {
        return total_volume_.load(std::memory_order_relaxed);
    }
    
    // Get drawdown metrics
    std::vector<DrawdownPeriod> get_drawdown_periods() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        return drawdown_periods_;
    }
    
    // Get execution latency statistics
    struct LatencyStats {
        uint64_t count = 0;
        double average_ns = 0.0;
        uint64_t p50_ns = 0;
        uint64_t p95_ns = 0;
        uint64_t p99_ns = 0;
        uint64_t min_ns = 0;
        uint64_t max_ns = 0;
    };
    
    LatencyStats get_execution_latency_stats() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        LatencyStats stats;
        
        if (execution_latencies_ns_.empty()) {
            return stats;
        }
        
        std::vector<uint64_t> latencies = execution_latencies_ns_;
        std::sort(latencies.begin(), latencies.end());
        
        stats.count = latencies.size();
        stats.min_ns = latencies.front();
        stats.max_ns = latencies.back();
        
        uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0ULL);
        stats.average_ns = static_cast<double>(sum) / latencies.size();
        
        stats.p50_ns = latencies[latencies.size() * 50 / 100];
        stats.p95_ns = latencies[latencies.size() * 95 / 100];
        stats.p99_ns = latencies[latencies.size() * 99 / 100];
        
        return stats;
    }
    
    // Get market impact statistics
    struct MarketImpactStats {
        uint64_t count = 0;
        double average_bps = 0.0;
        uint64_t p50_bps = 0;
        uint64_t p95_bps = 0;
        uint64_t p99_bps = 0;
        uint64_t max_bps = 0;
    };
    
    MarketImpactStats get_market_impact_stats() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        MarketImpactStats stats;
        
        if (market_impacts_bps_.empty()) {
            return stats;
        }
        
        std::vector<uint64_t> impacts = market_impacts_bps_;
        std::sort(impacts.begin(), impacts.end());
        
        stats.count = impacts.size();
        stats.max_bps = impacts.back();
        
        uint64_t sum = std::accumulate(impacts.begin(), impacts.end(), 0ULL);
        stats.average_bps = static_cast<double>(sum) / impacts.size();
        
        stats.p50_bps = impacts[impacts.size() * 50 / 100];
        stats.p95_bps = impacts[impacts.size() * 95 / 100];
        stats.p99_bps = impacts[impacts.size() * 99 / 100];
        
        return stats;
    }

private:
    void update_drawdown_tracking(int64_t current_pnl_cents, uint64_t timestamp_ns) {
        static int64_t running_peak = 0;
        static bool in_drawdown = false;
        static DrawdownPeriod current_drawdown(0, 0);
        
        if (current_pnl_cents > running_peak) {
            running_peak = current_pnl_cents;
            
            if (in_drawdown) {
                // End of drawdown period
                current_drawdown.end_time_ns = timestamp_ns;
                current_drawdown.duration_ms = (timestamp_ns - current_drawdown.start_time_ns) / 1000000;
                drawdown_periods_.push_back(current_drawdown);
                in_drawdown = false;
            }
        } else if (current_pnl_cents < running_peak) {
            if (!in_drawdown) {
                // Start of new drawdown
                current_drawdown = DrawdownPeriod(timestamp_ns, running_peak);
                in_drawdown = true;
            }
            
            // Update current drawdown
            current_drawdown.trough_value_cents = current_pnl_cents;
            current_drawdown.drawdown_cents = running_peak - current_pnl_cents;
            if (running_peak > 0) {
                current_drawdown.drawdown_percentage = 
                    static_cast<double>(current_drawdown.drawdown_cents) / running_peak * 100.0;
            }
        }
    }
};

} // namespace hft