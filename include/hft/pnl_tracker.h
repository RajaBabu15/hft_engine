#pragma once

#include "hft/types.h"
#include "hft/trade.h"
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <iostream>
#include <iomanip>

namespace hft {

// Position information per symbol
struct Position {
    Symbol symbol;
    Quantity net_position;
    Price average_price;
    int64_t realized_pnl;     // In price ticks
    int64_t unrealized_pnl;   // In price ticks
    uint64_t total_volume;
    uint32_t trade_count;
    uint64_t last_update_time;
    
    Position() : symbol(0), net_position(0), average_price(0), realized_pnl(0),
                 unrealized_pnl(0), total_volume(0), trade_count(0), last_update_time(0) {}
};

// Trade execution record for slippage calculation
struct ExecutionRecord {
    OrderId order_id;
    Symbol symbol;
    Side side;
    Quantity quantity;
    Price executed_price;
    Price expected_price;    // Price at order submission
    uint64_t execution_time;
    int64_t slippage_ticks; // executed_price - expected_price (signed)
    
    ExecutionRecord() : order_id(0), symbol(0), side(Side::BUY), quantity(0),
                       executed_price(0), expected_price(0), execution_time(0), slippage_ticks(0) {}
};

// Strategy-specific P&L tracking
class StrategyPnLTracker {
private:
    std::string strategy_id_;
    std::unordered_map<Symbol, Position> positions_;
    std::vector<ExecutionRecord> executions_;
    mutable std::mutex mutex_;
    
    // Aggregate statistics
    std::atomic<int64_t> total_realized_pnl_{0};
    std::atomic<int64_t> total_unrealized_pnl_{0};
    std::atomic<int64_t> total_slippage_{0};
    std::atomic<uint64_t> total_volume_{0};
    std::atomic<uint32_t> total_trades_{0};
    
    // Market data cache for unrealized P&L calculation
    std::unordered_map<Symbol, Price> last_market_prices_;
    
    uint64_t now_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }

public:
    StrategyPnLTracker(const std::string& strategy_id) : strategy_id_(strategy_id) {}
    
    // Record a trade execution
    void record_execution(OrderId order_id, Symbol symbol, Side side, 
                         Quantity quantity, Price executed_price, Price expected_price = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        uint64_t timestamp = now_ns();
        
        // Create execution record
        ExecutionRecord execution;
        execution.order_id = order_id;
        execution.symbol = symbol;
        execution.side = side;
        execution.quantity = quantity;
        execution.executed_price = executed_price;
        execution.expected_price = expected_price;
        execution.execution_time = timestamp;
        execution.slippage_ticks = executed_price - expected_price;
        
        executions_.push_back(execution);
        
        // Update position
        auto& position = positions_[symbol];
        position.symbol = symbol;
        position.last_update_time = timestamp;
        position.trade_count++;
        position.total_volume += quantity;
        
        // Calculate new average price and realized P&L
        int64_t direction = (side == Side::BUY) ? 1 : -1;
        Quantity new_net_position = position.net_position + (direction * quantity);
        
        if (position.net_position == 0) {
            // Opening position
            position.net_position = new_net_position;
            position.average_price = executed_price;
        } else if ((position.net_position > 0 && side == Side::BUY) ||
                   (position.net_position < 0 && side == Side::SELL)) {
            // Adding to position
            int64_t total_value = position.average_price * std::abs(position.net_position) +
                                 executed_price * quantity;
            position.average_price = total_value / std::abs(new_net_position);
            position.net_position = new_net_position;
        } else {
            // Reducing or closing position
            Quantity reduction = std::min(quantity, static_cast<Quantity>(std::abs(position.net_position)));
            
            // Calculate realized P&L for the reduced portion
            int64_t pnl_per_share = (side == Side::SELL) ? 
                (executed_price - position.average_price) :
                (position.average_price - executed_price);
            
            int64_t realized_pnl = pnl_per_share * reduction;
            position.realized_pnl += realized_pnl;
            
            // Update position
            position.net_position = new_net_position;
            
            // If position is reversed, set new average price
            if (new_net_position != 0 && 
                ((position.net_position >= 0 && new_net_position < 0) ||
                 (position.net_position <= 0 && new_net_position > 0))) {
                position.average_price = executed_price;
            }
        }
        
        // Update atomic counters
        total_volume_.fetch_add(quantity, std::memory_order_relaxed);
        total_trades_.fetch_add(1, std::memory_order_relaxed);
        total_realized_pnl_.store(calculate_total_realized_pnl(), std::memory_order_relaxed);
        
        if (expected_price > 0) {
            total_slippage_.fetch_add(execution.slippage_ticks, std::memory_order_relaxed);
        }
    }
    
    // Update market price for unrealized P&L calculation
    void update_market_price(Symbol symbol, Price market_price) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_market_prices_[symbol] = market_price;
        
        // Update unrealized P&L for this symbol
        auto it = positions_.find(symbol);
        if (it != positions_.end()) {
            Position& position = it->second;
            if (position.net_position != 0) {
                int64_t direction = (position.net_position > 0) ? 1 : -1;
                position.unrealized_pnl = direction * 
                    (market_price - position.average_price) * std::abs(position.net_position);
            } else {
                position.unrealized_pnl = 0;
            }
        }
        
        // Update total unrealized P&L
        total_unrealized_pnl_.store(calculate_total_unrealized_pnl(), std::memory_order_relaxed);
    }
    
    // Get position for a specific symbol
    Position get_position(Symbol symbol) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = positions_.find(symbol);
        return (it != positions_.end()) ? it->second : Position{};
    }
    
    // Get all positions
    std::vector<Position> get_all_positions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Position> result;
        result.reserve(positions_.size());
        
        for (const auto& pair : positions_) {
            result.push_back(pair.second);
        }
        
        return result;
    }
    
    // Get recent executions
    std::vector<ExecutionRecord> get_recent_executions(size_t max_count = 100) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (executions_.size() <= max_count) {
            return executions_;
        }
        
        return std::vector<ExecutionRecord>(
            executions_.end() - max_count, executions_.end());
    }
    
    // Calculate total realized P&L (thread-safe internal calculation)
    int64_t calculate_total_realized_pnl() const {
        int64_t total = 0;
        for (const auto& pair : positions_) {
            total += pair.second.realized_pnl;
        }
        return total;
    }
    
    // Calculate total unrealized P&L (thread-safe internal calculation)
    int64_t calculate_total_unrealized_pnl() const {
        int64_t total = 0;
        for (const auto& pair : positions_) {
            total += pair.second.unrealized_pnl;
        }
        return total;
    }
    
    // Get aggregated statistics
    struct PnLSummary {
        std::string strategy_id;
        int64_t total_realized_pnl;
        int64_t total_unrealized_pnl;
        int64_t total_pnl;
        int64_t total_slippage;
        uint64_t total_volume;
        uint32_t total_trades;
        double avg_slippage_per_trade;
        size_t open_positions;
        uint64_t last_update_time;
    };
    
    PnLSummary get_summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        PnLSummary summary;
        summary.strategy_id = strategy_id_;
        summary.total_realized_pnl = total_realized_pnl_.load(std::memory_order_relaxed);
        summary.total_unrealized_pnl = total_unrealized_pnl_.load(std::memory_order_relaxed);
        summary.total_pnl = summary.total_realized_pnl + summary.total_unrealized_pnl;
        summary.total_slippage = total_slippage_.load(std::memory_order_relaxed);
        summary.total_volume = total_volume_.load(std::memory_order_relaxed);
        summary.total_trades = total_trades_.load(std::memory_order_relaxed);
        
        if (summary.total_trades > 0) {
            summary.avg_slippage_per_trade = static_cast<double>(summary.total_slippage) / summary.total_trades;
        } else {
            summary.avg_slippage_per_trade = 0.0;
        }
        
        summary.open_positions = 0;
        summary.last_update_time = 0;
        for (const auto& pair : positions_) {
            if (pair.second.net_position != 0) {
                summary.open_positions++;
            }
            summary.last_update_time = std::max(summary.last_update_time, pair.second.last_update_time);
        }
        
        return summary;
    }
    
    // Reset all statistics (for backtesting)
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        positions_.clear();
        executions_.clear();
        last_market_prices_.clear();
        
        total_realized_pnl_.store(0, std::memory_order_relaxed);
        total_unrealized_pnl_.store(0, std::memory_order_relaxed);
        total_slippage_.store(0, std::memory_order_relaxed);
        total_volume_.store(0, std::memory_order_relaxed);
        total_trades_.store(0, std::memory_order_relaxed);
    }
    
    // Print detailed P&L report
    void print_detailed_report() const {
        auto summary = get_summary();
        auto positions = get_all_positions();
        
        std::cout << "\n=== P&L REPORT: " << summary.strategy_id << " ===" << std::endl;
        std::cout << "Total P&L: " << summary.total_pnl << " ticks" << std::endl;
        std::cout << "  Realized P&L: " << summary.total_realized_pnl << " ticks" << std::endl;
        std::cout << "  Unrealized P&L: " << summary.total_unrealized_pnl << " ticks" << std::endl;
        std::cout << "Total Volume: " << summary.total_volume << std::endl;
        std::cout << "Total Trades: " << summary.total_trades << std::endl;
        std::cout << "Total Slippage: " << summary.total_slippage << " ticks" << std::endl;
        std::cout << "Avg Slippage/Trade: " << std::fixed << std::setprecision(2) 
                  << summary.avg_slippage_per_trade << " ticks" << std::endl;
        std::cout << "Open Positions: " << summary.open_positions << std::endl;
        
        if (!positions.empty()) {
            std::cout << "\n--- POSITIONS ---" << std::endl;
            std::cout << "Symbol\t\tNet Pos\t\tAvg Price\tRealized\tUnrealized\tVolume\t\tTrades" << std::endl;
            std::cout << "-------------------------------------------------------------------------------" << std::endl;
            
            for (const auto& pos : positions) {
                std::cout << pos.symbol << "\t\t"
                         << pos.net_position << "\t\t"
                         << pos.average_price << "\t\t"
                         << pos.realized_pnl << "\t\t"
                         << pos.unrealized_pnl << "\t\t"
                         << pos.total_volume << "\t\t"
                         << pos.trade_count << std::endl;
            }
        }
        
        std::cout << "================================================================" << std::endl;
    }
};

// Multi-strategy P&L manager
class PnLManager {
private:
    std::unordered_map<std::string, std::unique_ptr<StrategyPnLTracker>> strategies_;
    mutable std::mutex strategies_mutex_;

public:
    // Register a new strategy
    void register_strategy(const std::string& strategy_id) {
        std::lock_guard<std::mutex> lock(strategies_mutex_);
        strategies_[strategy_id] = std::make_unique<StrategyPnLTracker>(strategy_id);
    }
    
    // Get strategy tracker
    StrategyPnLTracker* get_strategy_tracker(const std::string& strategy_id) {
        std::lock_guard<std::mutex> lock(strategies_mutex_);
        auto it = strategies_.find(strategy_id);
        return (it != strategies_.end()) ? it->second.get() : nullptr;
    }
    
    // Record execution for a strategy
    void record_execution(const std::string& strategy_id, OrderId order_id, 
                         Symbol symbol, Side side, Quantity quantity, 
                         Price executed_price, Price expected_price = 0) {
        auto* tracker = get_strategy_tracker(strategy_id);
        if (tracker) {
            tracker->record_execution(order_id, symbol, side, quantity, executed_price, expected_price);
        }
    }
    
    // Update market prices for all strategies
    void update_market_price(Symbol symbol, Price market_price) {
        std::lock_guard<std::mutex> lock(strategies_mutex_);
        for (auto& pair : strategies_) {
            pair.second->update_market_price(symbol, market_price);
        }
    }
    
    // Get consolidated P&L summary
    struct ConsolidatedSummary {
        int64_t total_pnl;
        int64_t total_realized_pnl;
        int64_t total_unrealized_pnl;
        int64_t total_slippage;
        uint64_t total_volume;
        uint32_t total_trades;
        size_t active_strategies;
        size_t total_open_positions;
    };
    
    ConsolidatedSummary get_consolidated_summary() const {
        std::lock_guard<std::mutex> lock(strategies_mutex_);
        
        ConsolidatedSummary consolidated;
        consolidated.total_pnl = 0;
        consolidated.total_realized_pnl = 0;
        consolidated.total_unrealized_pnl = 0;
        consolidated.total_slippage = 0;
        consolidated.total_volume = 0;
        consolidated.total_trades = 0;
        consolidated.active_strategies = strategies_.size();
        consolidated.total_open_positions = 0;
        
        for (const auto& pair : strategies_) {
            auto summary = pair.second->get_summary();
            consolidated.total_pnl += summary.total_pnl;
            consolidated.total_realized_pnl += summary.total_realized_pnl;
            consolidated.total_unrealized_pnl += summary.total_unrealized_pnl;
            consolidated.total_slippage += summary.total_slippage;
            consolidated.total_volume += summary.total_volume;
            consolidated.total_trades += summary.total_trades;
            consolidated.total_open_positions += summary.open_positions;
        }
        
        return consolidated;
    }
    
    // Print consolidated report
    void print_consolidated_report() const {
        auto consolidated = get_consolidated_summary();
        
        std::cout << "\n=== CONSOLIDATED P&L REPORT ===" << std::endl;
        std::cout << "Total P&L: " << consolidated.total_pnl << " ticks" << std::endl;
        std::cout << "  Realized P&L: " << consolidated.total_realized_pnl << " ticks" << std::endl;
        std::cout << "  Unrealized P&L: " << consolidated.total_unrealized_pnl << " ticks" << std::endl;
        std::cout << "Total Volume: " << consolidated.total_volume << std::endl;
        std::cout << "Total Trades: " << consolidated.total_trades << std::endl;
        std::cout << "Total Slippage: " << consolidated.total_slippage << " ticks" << std::endl;
        std::cout << "Active Strategies: " << consolidated.active_strategies << std::endl;
        std::cout << "Total Open Positions: " << consolidated.total_open_positions << std::endl;
        
        // Print individual strategy summaries
        std::lock_guard<std::mutex> lock(strategies_mutex_);
        for (const auto& pair : strategies_) {
            auto summary = pair.second->get_summary();
            std::cout << "\n--- " << summary.strategy_id << " ---" << std::endl;
            std::cout << "P&L: " << summary.total_pnl 
                     << " (R: " << summary.total_realized_pnl 
                     << ", U: " << summary.total_unrealized_pnl << ")" << std::endl;
            std::cout << "Volume: " << summary.total_volume 
                     << ", Trades: " << summary.total_trades 
                     << ", Open Pos: " << summary.open_positions << std::endl;
        }
        
        std::cout << "================================" << std::endl;
    }
    
    // Real-time P&L dashboard (simplified)
    void start_dashboard_thread(int update_interval_seconds = 5) {
        std::thread([this, update_interval_seconds]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(update_interval_seconds));
                print_consolidated_report();
            }
        }).detach();
    }
};

} // namespace hft