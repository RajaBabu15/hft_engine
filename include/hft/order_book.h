#pragma once
#include <hft/command.h>
#include <hft/trade.h>
#include <hft/types.h>
#include <hft/order.h>
#include <hft/ultra_optimized_hft_engine.h>

#include <map>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <atomic>

namespace hft {

    // High-performance order book implementation optimized for HFT
    class OrderBook {
    private:
        // Ultra-optimized engine for maximum performance
        std::unique_ptr<hft::ultra::UltraOptimizedMatchingEngine> ultra_engine_;
        std::atomic<bool> ultra_mode_enabled_{false};
        
        // Use sorted vectors for better cache locality and SIMD operations
        struct PriceLevel {
            Price price;
            Quantity total_qty;
            uint32_t order_count;
            std::vector<Order> orders;
            
            PriceLevel() : price(0), total_qty(0), order_count(0) {}
            
            // Optimized order insertion with binary search
            void add_order(const Order& order) {
                // Find insertion point using binary search
                auto it = std::lower_bound(orders.begin(), orders.end(), order,
                    [](const Order& a, const Order& b) {
                        return a.timestamp < b.timestamp; // FIFO ordering
                    });
                orders.insert(it, order);
                total_qty += order.qty;
                order_count++;
            }
            
            // Optimized order removal
            bool remove_order(OrderId order_id) {
                auto it = std::find_if(orders.begin(), orders.end(),
                    [order_id](const Order& order) {
                        return order.id == order_id;
                    });
                
                if (it != orders.end()) {
                    total_qty -= it->qty;
                    order_count--;
                    orders.erase(it);
                    return true;
                }
                return false;
            }
            
            // Update order quantity
            bool update_order(OrderId order_id, Quantity new_qty) {
                auto it = std::find_if(orders.begin(), orders.end(),
                    [order_id](const Order& order) {
                        return order.id == order_id;
                    });
                
                if (it != orders.end()) {
                    total_qty = total_qty - it->qty + new_qty;
                    it->qty = new_qty;
                    return true;
                }
                return false;
            }
        };
        
        // Separate bid and ask books for better cache locality
        std::vector<PriceLevel> bids_;  // Sorted by price (descending)
        std::vector<PriceLevel> asks_;  // Sorted by price (ascending)
        
        // Cache-friendly price level lookup
        std::map<Price, size_t> bid_index_;
        std::map<Price, size_t> ask_index_;
        
        // Best bid/ask cache
        Price best_bid_{0};
        Price best_ask_{std::numeric_limits<Price>::max()};
        
        // Performance optimization: batch updates
        static constexpr size_t BATCH_SIZE = 64;
        std::vector<Order> pending_orders_;
        
    public:
        OrderBook() {
            bids_.reserve(1000);  // Pre-allocate for common price levels
            asks_.reserve(1000);
            pending_orders_.reserve(BATCH_SIZE);
            
            // Initialize ultra-optimized engine
            ultra_engine_ = std::make_unique<hft::ultra::UltraOptimizedMatchingEngine>();
        }
        
        ~OrderBook() {
            disable_ultra_mode();
        }
        
        // Enable ultra-optimized mode with ARM NEON acceleration
        void enable_ultra_mode(int core_id = -1) {
            if (!ultra_mode_enabled_.load(std::memory_order_acquire)) {
                ultra_engine_->start(core_id);
                ultra_mode_enabled_.store(true, std::memory_order_release);
            }
        }
        
        void disable_ultra_mode() {
            if (ultra_mode_enabled_.load(std::memory_order_acquire)) {
                ultra_engine_->stop();
                ultra_mode_enabled_.store(false, std::memory_order_release);
            }
        }
        
        // High-performance order matching with SIMD-friendly loops
        bool match(Order& incoming_order, std::vector<Trade>& trades) {
            // Ultra-optimized path for maximum performance
            if (ultra_mode_enabled_.load(std::memory_order_acquire)) {
                return ultra_engine_->submit_order(std::move(incoming_order));
            }
            
            // Legacy path for compatibility
            if (incoming_order.side == Side::BUY) {
                return match_buy_order(incoming_order, trades);
            } else {
                return match_sell_order(incoming_order, trades);
            }
        }
        
        // Update order book from command
        void update(const Command& cmd) {
            // Clear existing books
            bids_.clear();
            asks_.clear();
            bid_index_.clear();
            ask_index_.clear();
            
            // Process bids (descending order)
            for (const auto& level : cmd.bids_) {
                PriceLevel pl;
                pl.price = level.price;
                pl.total_qty = level.qty;
                pl.order_count = 1;
                
                Order order{};
                order.init(0, 0, Side::BUY, OrderType::LIMIT, level.price, level.qty, TimeInForce::GTC);
                pl.orders.push_back(order);
                
                bids_.push_back(pl);
                bid_index_[level.price] = bids_.size() - 1;
            }
            
            // Process asks (ascending order)
            for (const auto& level : cmd.asks_) {
                PriceLevel pl;
                pl.price = level.price;
                pl.total_qty = level.qty;
                pl.order_count = 1;
                
                Order order{};
                order.init(0, 0, Side::SELL, OrderType::LIMIT, level.price, level.qty, TimeInForce::GTC);
                pl.orders.push_back(order);
                
                asks_.push_back(pl);
                ask_index_[level.price] = asks_.size() - 1;
            }
            
            // Update best prices
            update_best_prices();
        }
        
        // Get current best bid/ask
        Price get_best_bid() const { 
            if (ultra_mode_enabled_.load(std::memory_order_acquire)) {
                auto stats = ultra_engine_->get_stats();
                return stats.current_best_bid;
            }
            return best_bid_; 
        }
        
        Price get_best_ask() const { 
            if (ultra_mode_enabled_.load(std::memory_order_acquire)) {
                auto stats = ultra_engine_->get_stats();
                return stats.current_best_ask;
            }
            return best_ask_; 
        }
        
        // Get spread
        Price get_spread() const { 
            return (best_ask_ != std::numeric_limits<Price>::max() && best_bid_ > 0) 
                   ? best_ask_ - best_bid_ : 0; 
        }
        
        // Get mid price
        Price get_mid_price() const {
            return (best_bid_ + best_ask_) / 2;
        }
        
        // Get total volume at price level
        Quantity get_volume_at_price(Price price, Side side) const {
            if (side == Side::BUY) {
                auto it = bid_index_.find(price);
                return (it != bid_index_.end()) ? bids_[it->second].total_qty : 0;
            } else {
                auto it = ask_index_.find(price);
                return (it != ask_index_.end()) ? asks_[it->second].total_qty : 0;
            }
        }
        
        // Performance optimization: batch order processing
        void add_pending_order(const Order& order) {
            // Ultra mode doesn't use pending orders - submit directly
            if (ultra_mode_enabled_.load(std::memory_order_acquire)) {
                Order order_copy = order;
                ultra_engine_->submit_order(std::move(order_copy));
                return;
            }
            
            pending_orders_.push_back(order);
            if (pending_orders_.size() >= BATCH_SIZE) {
                process_pending_orders();
            }
        }
        
        void process_pending_orders() {
            // Ultra mode doesn't use pending orders
            if (ultra_mode_enabled_.load(std::memory_order_acquire)) {
                pending_orders_.clear();
                return;
            }
            
            for (const auto& order : pending_orders_) {
                add_order(order);
            }
            pending_orders_.clear();
        }
        
        // Get ultra mode performance statistics
        struct UltraStats {
            uint64_t total_trades;
            uint64_t total_accepts;
            uint64_t total_rejects;
            Price current_best_bid;
            Price current_best_ask;
        };
        
        UltraStats get_ultra_stats() const {
            if (ultra_mode_enabled_.load(std::memory_order_acquire)) {
                auto stats = ultra_engine_->get_stats();
                return UltraStats{
                    .total_trades = stats.total_trades,
                    .total_accepts = stats.total_accepts,
                    .total_rejects = stats.total_rejects,
                    .current_best_bid = stats.current_best_bid,
                    .current_best_ask = stats.current_best_ask
                };
            }
            return UltraStats{0, 0, 0, 0, 0};
        }
        
    private:
        // Optimized buy order matching
        bool match_buy_order(Order& incoming_order, std::vector<Trade>& trades) {
            Quantity remaining_qty = incoming_order.qty;
            
            // Match against ask orders (ascending price)
            for (auto& ask_level : asks_) {
                if (ask_level.price > incoming_order.price || remaining_qty == 0) {
                    break;
                }
                
                // SIMD-friendly loop for order matching
                for (auto& ask_order : ask_level.orders) {
                    if (remaining_qty == 0) break;
                    
                    Quantity match_qty = std::min(remaining_qty, ask_order.qty);
                    
                    // Create trade
                    Trade trade;
                    trade.buy_order_id = incoming_order.id;
                    trade.sell_order_id = ask_order.id;
                    trade.price = ask_level.price;
                    trade.qty = match_qty;
                    trade.timestamp = now_ns();
                    trades.push_back(trade);
                    
                    // Update quantities
                    remaining_qty -= match_qty;
                    ask_order.qty -= match_qty;
                    ask_level.total_qty -= match_qty;
                    incoming_order.qty -= match_qty;
                    
                    // Remove filled orders
                    if (ask_order.qty == 0) {
                        ask_level.order_count--;
                    }
                }
                
                // Remove empty price levels
                ask_level.orders.erase(
                    std::remove_if(ask_level.orders.begin(), ask_level.orders.end(),
                        [](const Order& order) { return order.qty == 0; }),
                    ask_level.orders.end()
                );
            }
            
            // Update best prices
            update_best_prices();
            
            return remaining_qty == 0;
        }
        
        // Optimized sell order matching
        bool match_sell_order(Order& incoming_order, std::vector<Trade>& trades) {
            Quantity remaining_qty = incoming_order.qty;
            
            // Match against bid orders (descending price)
            for (auto& bid_level : bids_) {
                if (bid_level.price < incoming_order.price || remaining_qty == 0) {
                    break;
                }
                
                // SIMD-friendly loop for order matching
                for (auto& bid_order : bid_level.orders) {
                    if (remaining_qty == 0) break;
                    
                    Quantity match_qty = std::min(remaining_qty, bid_order.qty);
                    
                    // Create trade
                    Trade trade;
                    trade.buy_order_id = bid_order.id;
                    trade.sell_order_id = incoming_order.id;
                    trade.price = bid_level.price;
                    trade.qty = match_qty;
                    trade.timestamp = now_ns();
                    trades.push_back(trade);
                    
                    // Update quantities
                    remaining_qty -= match_qty;
                    bid_order.qty -= match_qty;
                    bid_level.total_qty -= match_qty;
                    incoming_order.qty -= match_qty;
                    
                    // Remove filled orders
                    if (bid_order.qty == 0) {
                        bid_level.order_count--;
                    }
                }
                
                // Remove empty price levels
                bid_level.orders.erase(
                    std::remove_if(bid_level.orders.begin(), bid_level.orders.end(),
                        [](const Order& order) { return order.qty == 0; }),
                    bid_level.orders.end()
                );
            }
            
            // Update best prices
            update_best_prices();
            
            return remaining_qty == 0;
        }
        
        // Add order to appropriate book
        void add_order(const Order& order) {
            if (order.side == Side::BUY) {
                add_bid_order(order);
            } else {
                add_ask_order(order);
            }
        }
        
        void add_bid_order(const Order& order) {
            auto it = bid_index_.find(order.price);
            if (it != bid_index_.end()) {
                bids_[it->second].add_order(order);
            } else {
                PriceLevel pl;
                pl.price = order.price;
                pl.add_order(order);
                bids_.push_back(pl);
                bid_index_[order.price] = bids_.size() - 1;
                
                // Maintain descending order
                std::sort(bids_.begin(), bids_.end(),
                    [](const PriceLevel& a, const PriceLevel& b) {
                        return a.price > b.price;
                    });
                
                // Update indices
                for (size_t i = 0; i < bids_.size(); ++i) {
                    bid_index_[bids_[i].price] = i;
                }
            }
        }
        
        void add_ask_order(const Order& order) {
            auto it = ask_index_.find(order.price);
            if (it != ask_index_.end()) {
                asks_[it->second].add_order(order);
            } else {
                PriceLevel pl;
                pl.price = order.price;
                pl.add_order(order);
                asks_.push_back(pl);
                ask_index_[order.price] = asks_.size() - 1;
                
                // Maintain ascending order
                std::sort(asks_.begin(), asks_.end(),
                    [](const PriceLevel& a, const PriceLevel& b) {
                        return a.price < b.price;
                    });
                
                // Update indices
                for (size_t i = 0; i < asks_.size(); ++i) {
                    ask_index_[asks_[i].price] = i;
                }
            }
        }
        
        // Update best bid/ask prices
        void update_best_prices() {
            best_bid_ = bids_.empty() ? 0 : bids_.front().price;
            best_ask_ = asks_.empty() ? std::numeric_limits<Price>::max() : asks_.front().price;
        }
    };
}