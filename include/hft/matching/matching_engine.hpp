#pragma once
#include "hft/core/types.hpp"
#include "hft/core/lock_free_queue.hpp"
#include "hft/core/async_logger.hpp"
#include "hft/core/memory_optimized_segment_tree.hpp"
#include "hft/order/order.hpp"
#include "hft/order/order_book.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
namespace hft {
namespace matching {
struct Fill {
    core::OrderID aggressive_order_id;
    core::OrderID passive_order_id;
    core::Price price;
    core::Quantity quantity;
    core::TimePoint timestamp;
    core::Symbol symbol;
    Fill() = default;
    Fill(core::OrderID aggressive, core::OrderID passive, core::Price p,
         core::Quantity q, const core::Symbol& sym, core::TimePoint ts)
        : aggressive_order_id(aggressive), passive_order_id(passive),
          price(p), quantity(q), symbol(sym), timestamp(ts) {}
};
struct ExecutionReport {
    core::OrderID order_id;
    core::Symbol symbol;
    core::Side side;
    core::OrderStatus status;
    core::Price price;
    core::Quantity original_quantity;
    core::Quantity executed_quantity;
    core::Quantity remaining_quantity;
    core::Price avg_executed_price;
    core::TimePoint timestamp;
    std::string execution_id;
    std::vector<Fill> fills;
    ExecutionReport() = default;
    ExecutionReport(const order::Order& order)
        : order_id(order.id), symbol(order.symbol), side(order.side),
          status(order.status), price(order.price),
          original_quantity(order.quantity), executed_quantity(order.filled_quantity),
          remaining_quantity(order.remaining_quantity()), avg_executed_price(0.0),
          timestamp(order.timestamp) {}
};
struct MatchingStats {
    std::atomic<uint64_t> orders_processed{0};
    std::atomic<uint64_t> orders_matched{0};
    std::atomic<uint64_t> orders_rejected{0};
    std::atomic<uint64_t> total_fills{0};
    std::atomic<double> total_volume{0.0};
    std::atomic<double> total_notional{0.0};
    std::atomic<double> avg_matching_latency_ns{0.0};
    std::atomic<double> max_matching_latency_ns{0.0};
    std::atomic<uint64_t> matching_operations{0};
    void reset() {
        orders_processed = 0;
        orders_matched = 0;
        orders_rejected = 0;
        total_fills = 0;
        total_volume = 0.0;
        total_notional = 0.0;
        avg_matching_latency_ns = 0.0;
        max_matching_latency_ns = 0.0;
        matching_operations = 0;
    }
};
enum class MatchingAlgorithm {
    PRICE_TIME_PRIORITY,
    PRO_RATA,
    SIZE_PRIORITY,
    TIME_PRIORITY
};
class MatchingEngine {
public:
    using ExecutionCallback = std::function<void(const ExecutionReport&)>;
    using FillCallback = std::function<void(const Fill&)>;
    using ErrorCallback = std::function<void(const std::string&, const std::string&)>;
private:
    static constexpr size_t ORDER_QUEUE_SIZE = 65536; 
    static constexpr size_t MAX_SYMBOLS = 1000;
    
    // Legacy order books (for backward compatibility)
    std::unordered_map<core::Symbol, std::unique_ptr<order::OrderBook>> order_books_;
    
    // High-performance segment tree order books
    std::unordered_map<core::Symbol, std::unique_ptr<core::OrderBookSegmentTree>> segment_tree_books_;
    std::unordered_map<core::OrderID, order::Order> segment_tree_orders_;
    bool use_segment_tree_;
    
    std::unique_ptr<core::LockFreeQueue<order::Order, ORDER_QUEUE_SIZE>> incoming_orders_;

    std::atomic<bool> running_{false};
    std::thread matching_thread_;
    MatchingAlgorithm algorithm_;
    ExecutionCallback execution_callback_;
    FillCallback fill_callback_;
    ErrorCallback error_callback_;
    MatchingStats stats_;
    std::unordered_map<core::OrderID, order::Order> active_orders_;
    std::atomic<core::OrderID> next_execution_id_{1};
    std::unique_ptr<core::AsyncLogger> logger_;
public:
    explicit MatchingEngine(MatchingAlgorithm algorithm = MatchingAlgorithm::PRICE_TIME_PRIORITY, 
                           const std::string& log_path = "logs/engine_logs.log",
                           bool use_segment_tree = false);
    ~MatchingEngine();
    MatchingEngine(const MatchingEngine&) = delete;
    MatchingEngine& operator=(const MatchingEngine&) = delete;
    MatchingEngine(MatchingEngine&&) = delete;
    MatchingEngine& operator=(MatchingEngine&&) = delete;
    void set_execution_callback(ExecutionCallback callback);
    void set_fill_callback(FillCallback callback);
    void set_error_callback(ErrorCallback callback);
    void set_matching_algorithm(MatchingAlgorithm algorithm);
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    bool submit_order(const order::Order& order);
    bool cancel_order(core::OrderID order_id);
    bool modify_order(core::OrderID order_id, core::Price new_price, core::Quantity new_quantity);
    order::OrderBook* get_order_book(const core::Symbol& symbol);
    const order::OrderBook* get_order_book(const core::Symbol& symbol) const;
    std::vector<core::Symbol> get_symbols() const;
    const MatchingStats& get_stats() const { return stats_; }
    void reset_stats() { stats_.reset(); }
    bool has_order(core::OrderID order_id) const;
    order::Order get_order(core::OrderID order_id) const;
    std::vector<order::Order> get_orders_for_symbol(const core::Symbol& symbol) const;
private:
    void matching_worker();
    void process_order(const order::Order& order);
    std::vector<Fill> match_order_price_time_priority(const order::Order& incoming_order,
                                                     order::OrderBook& book);
    std::vector<Fill> match_order_pro_rata(const order::Order& incoming_order,
                                          order::OrderBook& book);
    std::vector<Fill> match_order_size_priority(const order::Order& incoming_order,
                                               order::OrderBook& book);
    std::vector<Fill> match_order_time_priority(const order::Order& incoming_order,
                                               order::OrderBook& book);
    
    order::OrderBook& get_or_create_order_book(const core::Symbol& symbol);
    core::OrderBookSegmentTree& get_or_create_segment_tree_book(const core::Symbol& symbol);
    
    // Segment tree matching functions
    std::vector<Fill> match_order_segment_tree(const order::Order& incoming_order,
                                               core::OrderBookSegmentTree& segment_book);
    std::vector<Fill> match_order_segment_tree_price_time(const order::Order& incoming_order,
                                                         core::OrderBookSegmentTree& segment_book);
    
    // Helper functions for segment tree operations
    std::vector<order::Order> get_segment_tree_orders_at_price(core::Price price, core::Side side) const;
    core::OrderID generate_synthetic_order_id();
    
    // Order storage management for segment tree
    void store_segment_tree_order(const order::Order& order);
    void update_segment_tree_order(const order::Order& order);
    void remove_segment_tree_order(core::OrderID order_id);
    
    void update_order_status(order::Order& order, const std::vector<Fill>& fills);
    ExecutionReport create_execution_report(const order::Order& order,
                                          const std::vector<Fill>& fills);
    std::string generate_execution_id();
    bool validate_order(const order::Order& order) const;
    bool validate_price(core::Price price) const;
    bool validate_quantity(core::Quantity quantity) const;
    void update_matching_latency(double latency_ns);
    void record_fill(const Fill& fill);
    bool perform_risk_checks(const order::Order& order) const;
    bool check_position_limits(const order::Order& order) const;
    bool check_order_limits(const order::Order& order) const;
    double calculate_market_impact(const order::Order& order, order::OrderBook& book) const;
    core::Price calculate_volume_weighted_price(const std::vector<Fill>& fills) const;
};

class MarketMakingEngine {
private:
    std::unique_ptr<MatchingEngine> matching_engine_;
    double spread_bps_;
    core::Quantity default_size_;
    double max_position_size_;
    double inventory_skew_factor_;
    std::unordered_map<core::Symbol, double> positions_;
    std::unordered_map<core::Symbol, double> unrealized_pnl_;
    std::unordered_map<core::Symbol, std::pair<core::OrderID, core::OrderID>> active_quotes_;
    std::atomic<core::OrderID> next_quote_id_{1000000};
public:
    explicit MarketMakingEngine(double spread_bps = 5.0, core::Quantity default_size = 100);
    ~MarketMakingEngine();
    void set_spread(double spread_bps);
    void set_default_size(core::Quantity size);
    void set_max_position_size(double max_size);
    void set_inventory_skew_factor(double factor);
    void start_market_making();
    void stop_market_making();
    void update_quotes(const core::Symbol& symbol, core::Price reference_price);
    void cancel_quotes(const core::Symbol& symbol);
    double get_position(const core::Symbol& symbol) const;
    double get_unrealized_pnl(const core::Symbol& symbol) const;
    double get_total_pnl() const;
    void on_market_data_update(const core::MarketDataTick& tick);
    void on_trade_execution(const ExecutionReport& execution);
private:
    std::pair<core::Price, core::Price> calculate_quote_prices(const core::Symbol& symbol,
                                                              core::Price reference_price);
    core::Quantity calculate_quote_size(const core::Symbol& symbol, core::Side side);
    void update_position(const core::Symbol& symbol, const Fill& fill);
    void update_unrealized_pnl(const core::Symbol& symbol, core::Price mark_price);
    bool should_quote(const core::Symbol& symbol) const;
    double calculate_inventory_skew(const core::Symbol& symbol) const;
    order::Order create_quote_order(const core::Symbol& symbol, core::Side side,
                                   core::Price price, core::Quantity quantity);
};
double calculate_price_improvement(core::Price execution_price, core::Price reference_price, core::Side side);
double calculate_effective_spread(core::Price bid, core::Price ask);
double calculate_mid_price(core::Price bid, core::Price ask);
bool prices_match(core::Price incoming_price, core::Price book_price, core::Side incoming_side);
core::Price get_better_price(core::Price price1, core::Price price2, core::Side side);
bool is_marketable(const order::Order& order, const order::OrderBook& book);
bool has_time_priority(const order::Order& order1, const order::Order& order2);
std::vector<order::Order> sort_by_time_priority(const std::vector<order::Order>& orders);
}
}
