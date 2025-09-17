#include "hft/matching/matching_engine.hpp"
#include "hft/core/clock.hpp"
#include <algorithm>
#include <sstream>
#include <filesystem>
namespace hft {
namespace matching {
MatchingEngine::MatchingEngine(MatchingAlgorithm algorithm, const std::string& log_path)
    : algorithm_(algorithm)
{
    incoming_orders_ = std::make_unique<core::LockFreeQueue<order::Order, ORDER_QUEUE_SIZE>>();
    
    // Initialize logger
    logger_ = std::make_unique<core::AsyncLogger>(log_path, core::LogLevel::INFO);
    logger_->start();
    
    logger_->info("MatchingEngine initialized with algorithm: " + 
                  std::to_string(static_cast<int>(algorithm)), "ENGINE");
}
MatchingEngine::~MatchingEngine() {
    stop();
    if (logger_) {
        logger_->info("MatchingEngine shutting down", "ENGINE");
        logger_->stop();
    }
}
void MatchingEngine::set_execution_callback(ExecutionCallback callback) {
    execution_callback_ = std::move(callback);
}
void MatchingEngine::set_fill_callback(FillCallback callback) {
    fill_callback_ = std::move(callback);
}
void MatchingEngine::set_error_callback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}
void MatchingEngine::set_matching_algorithm(MatchingAlgorithm algorithm) {
    algorithm_ = algorithm;
}
void MatchingEngine::start() {
    if (running_.exchange(true)) {
        if (logger_) {
            logger_->warn("Attempted to start already running MatchingEngine", "ENGINE");
        }
        return;
    }
    if (logger_) {
        logger_->info("Starting MatchingEngine", "ENGINE");
    }
    matching_thread_ = std::thread(&MatchingEngine::matching_worker, this);
}
void MatchingEngine::stop() {
    if (!running_.exchange(false)) {
        if (logger_) {
            logger_->warn("Attempted to stop already stopped MatchingEngine", "ENGINE");
        }
        return;
    }
    if (logger_) {
        logger_->info("Stopping MatchingEngine", "ENGINE");
    }
    if (matching_thread_.joinable()) {
        matching_thread_.join();
    }
    if (logger_) {
        logger_->info("MatchingEngine stopped successfully", "ENGINE");
    }
}
bool MatchingEngine::submit_order(const order::Order& order) {
    if (logger_) {
        logger_->log_order_received(order.id, order.symbol, order.price, order.quantity);
    }
    
    if (!validate_order(order)) {
        if (error_callback_) {
            error_callback_("VALIDATION_ERROR", "Order failed validation");
        }
        if (logger_) {
            logger_->error("Order " + std::to_string(order.id) + " failed validation", "ORDER_MGMT");
        }
        stats_.orders_rejected.fetch_add(1);
        return false;
    }
    if (!perform_risk_checks(order)) {
        if (error_callback_) {
            error_callback_("RISK_CHECK_FAILED", "Order failed risk checks");
        }
        if (logger_) {
            logger_->error("Order " + std::to_string(order.id) + " failed risk checks", "ORDER_MGMT");
        }
        stats_.orders_rejected.fetch_add(1);
        return false;
    }
    order::Order order_copy = order;
    bool enqueued = incoming_orders_->enqueue(std::move(order_copy));
    if (!enqueued && logger_) {
        logger_->warn("Order queue full, order " + std::to_string(order.id) + " dropped", "ENGINE");
    }
    return enqueued;
}
bool MatchingEngine::cancel_order(core::OrderID order_id) {
    auto it = active_orders_.find(order_id);
    if (it == active_orders_.end()) {
        if (logger_) {
            logger_->warn("Attempted to cancel non-existent order " + std::to_string(order_id), "ORDER_MGMT");
        }
        return false;
    }
    order::Order& order = it->second;
    order.status = core::OrderStatus::CANCELLED;
    auto* book = get_order_book(order.symbol);
    if (book) {
        book->cancel_order(order_id);
    }
    if (logger_) {
        logger_->log_order_cancelled(order_id, "User requested");
    }
    ExecutionReport report(order);
    report.status = core::OrderStatus::CANCELLED;
    if (execution_callback_) {
        execution_callback_(report);
    }
    active_orders_.erase(it);
    return true;
}
bool MatchingEngine::modify_order(core::OrderID order_id, core::Price new_price, core::Quantity new_quantity) {
    auto it = active_orders_.find(order_id);
    if (it == active_orders_.end()) {
        return false;
    }
    cancel_order(order_id);
    order::Order modified_order = it->second;
    modified_order.id = next_execution_id_.fetch_add(1);
    modified_order.price = new_price;
    modified_order.quantity = new_quantity;
    modified_order.timestamp = core::HighResolutionClock::now();
    return submit_order(modified_order);
}
order::OrderBook* MatchingEngine::get_order_book(const core::Symbol& symbol) {
    auto it = order_books_.find(symbol);
    return (it != order_books_.end()) ? it->second.get() : nullptr;
}
const order::OrderBook* MatchingEngine::get_order_book(const core::Symbol& symbol) const {
    auto it = order_books_.find(symbol);
    return (it != order_books_.end()) ? it->second.get() : nullptr;
}
std::vector<core::Symbol> MatchingEngine::get_symbols() const {
    std::vector<core::Symbol> symbols;
    symbols.reserve(order_books_.size());
    for (const auto& pair : order_books_) {
        symbols.push_back(pair.first);
    }
    return symbols;
}
bool MatchingEngine::has_order(core::OrderID order_id) const {
    return active_orders_.find(order_id) != active_orders_.end();
}
order::Order MatchingEngine::get_order(core::OrderID order_id) const {
    auto it = active_orders_.find(order_id);
    if (it != active_orders_.end()) {
        return it->second;
    }
    return order::Order();
}
std::vector<order::Order> MatchingEngine::get_orders_for_symbol(const core::Symbol& symbol) const {
    std::vector<order::Order> orders;
    for (const auto& pair : active_orders_) {
        if (pair.second.symbol == symbol) {
            orders.push_back(pair.second);
        }
    }
    return orders;
}
void MatchingEngine::matching_worker() {
    order::Order incoming_order;
    uint64_t processed_count = 0;
    auto last_throughput_log = core::HighResolutionClock::now();
    
    if (logger_) {
        logger_->info("Matching worker thread started", "ENGINE");
    }
    
    while (running_.load()) {
        if (incoming_orders_->dequeue(incoming_order)) {
            process_order(incoming_order);
            processed_count++;
            
            // Log throughput every 10,000 messages or every 1 second
            auto now = core::HighResolutionClock::now();
            auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_throughput_log).count();
            if (processed_count >= 10000 || elapsed_ns >= 1000000000) {
                uint64_t msgs_per_sec = processed_count * 1000000000ULL / elapsed_ns;
                if (logger_) {
                    logger_->log_throughput_measurement(msgs_per_sec, stats_.orders_processed.load());
                }
                processed_count = 0;
                last_throughput_log = now;
            }
        } else {
            std::this_thread::yield();
        }
    }
    
    if (logger_) {
        logger_->info("Matching worker thread stopped", "ENGINE");
    }
}
void MatchingEngine::process_order(const order::Order& order) {
    const auto start_time = core::HighResolutionClock::rdtsc();
    
    if (logger_) {
        logger_->debug("Processing order " + std::to_string(order.id) + " for symbol " + order.symbol, "ENGINE");
    }
    
    order::OrderBook& book = get_or_create_order_book(order.symbol);
    active_orders_[order.id] = order;
    order::Order& active_order = active_orders_[order.id];
    std::vector<Fill> fills;
    switch (algorithm_) {
        case MatchingAlgorithm::PRICE_TIME_PRIORITY:
            fills = match_order_price_time_priority(active_order, book);
            break;
        case MatchingAlgorithm::PRO_RATA:
            fills = match_order_pro_rata(active_order, book);
            break;
        case MatchingAlgorithm::SIZE_PRIORITY:
            fills = match_order_size_priority(active_order, book);
            break;
        case MatchingAlgorithm::TIME_PRIORITY:
            fills = match_order_time_priority(active_order, book);
            break;
    }
    update_order_status(active_order, fills);
    if (active_order.remaining_quantity() > 0 &&
        active_order.status != core::OrderStatus::CANCELLED) {
        book.add_order(active_order);
    } else {
        active_orders_.erase(active_order.id);
    }
    ExecutionReport execution_report = create_execution_report(active_order, fills);
    const auto end_time = core::HighResolutionClock::rdtsc();
    double latency_ns = static_cast<double>(end_time - start_time) / 2.5;
    update_matching_latency(latency_ns);
    stats_.orders_processed.fetch_add(1);
    
    if (logger_) {
        logger_->log_latency_measurement("order_processing", latency_ns);
    }
    
    if (!fills.empty()) {
        stats_.orders_matched.fetch_add(1);
        if (logger_) {
            for (const auto& fill : fills) {
                logger_->log_order_matched(fill.aggressive_order_id, fill.quantity, fill.price);
            }
        }
    }
    if (execution_callback_) {
        execution_callback_(execution_report);
    }
    for (const auto& fill : fills) {
        record_fill(fill);
        if (fill_callback_) {
            fill_callback_(fill);
        }
    }
}
std::vector<Fill> MatchingEngine::match_order_price_time_priority(const order::Order& incoming_order,
                                                                order::OrderBook& book) {
    std::vector<Fill> fills;
    if (incoming_order.side == core::Side::BUY) {
        while (incoming_order.remaining_quantity() > 0) {
            auto best_ask = book.get_best_ask();
            if (best_ask == 0.0 || incoming_order.price < best_ask) {
                break;
            }
            auto ask_orders = book.get_orders_at_price_level(best_ask, core::Side::SELL);
            if (ask_orders.empty()) {
                break;
            }
            auto& passive_order = ask_orders[0];
            core::Quantity fill_quantity = std::min(incoming_order.remaining_quantity(),
                                                   passive_order.remaining_quantity());
            Fill fill(incoming_order.id, passive_order.id, best_ask, fill_quantity,
                     incoming_order.symbol, core::HighResolutionClock::now());
            fills.push_back(fill);
            const_cast<order::Order&>(incoming_order).filled_quantity += fill_quantity;
            passive_order.filled_quantity += fill_quantity;
            if (passive_order.remaining_quantity() == 0) {
                book.cancel_order(passive_order.id);
                active_orders_.erase(passive_order.id);
            }
        }
    } else {
        while (incoming_order.remaining_quantity() > 0) {
            auto best_bid = book.get_best_bid();
            if (best_bid == 0.0 || incoming_order.price > best_bid) {
                break;
            }
            auto bid_orders = book.get_orders_at_price_level(best_bid, core::Side::BUY);
            if (bid_orders.empty()) {
                break;
            }
            auto& passive_order = bid_orders[0];
            core::Quantity fill_quantity = std::min(incoming_order.remaining_quantity(),
                                                   passive_order.remaining_quantity());
            Fill fill(incoming_order.id, passive_order.id, best_bid, fill_quantity,
                     incoming_order.symbol, core::HighResolutionClock::now());
            fills.push_back(fill);
            const_cast<order::Order&>(incoming_order).filled_quantity += fill_quantity;
            passive_order.filled_quantity += fill_quantity;
            if (passive_order.remaining_quantity() == 0) {
                book.cancel_order(passive_order.id);
                active_orders_.erase(passive_order.id);
            }
        }
    }
    return fills;
}
std::vector<Fill> MatchingEngine::match_order_pro_rata(const order::Order& incoming_order,
                                                     order::OrderBook& book) {
    return match_order_price_time_priority(incoming_order, book);
}
std::vector<Fill> MatchingEngine::match_order_size_priority(const order::Order& incoming_order,
                                                          order::OrderBook& book) {
    return match_order_price_time_priority(incoming_order, book);
}
std::vector<Fill> MatchingEngine::match_order_time_priority(const order::Order& incoming_order,
                                                          order::OrderBook& book) {
    return match_order_price_time_priority(incoming_order, book);
}
order::OrderBook& MatchingEngine::get_or_create_order_book(const core::Symbol& symbol) {
    auto it = order_books_.find(symbol);
    if (it == order_books_.end()) {
        order_books_[symbol] = std::make_unique<order::OrderBook>(symbol);
        return *order_books_[symbol];
    }
    return *it->second;
}
void MatchingEngine::update_order_status(order::Order& order, const std::vector<Fill>& fills) {
    if (!fills.empty()) {
        if (order.remaining_quantity() == 0) {
            order.status = core::OrderStatus::FILLED;
        } else {
            order.status = core::OrderStatus::PARTIALLY_FILLED;
        }
    } else if (order.status == core::OrderStatus::PENDING) {
    }
}
ExecutionReport MatchingEngine::create_execution_report(const order::Order& order,
                                                      const std::vector<Fill>& fills) {
    ExecutionReport report(order);
    report.fills = fills;
    report.execution_id = generate_execution_id();
    report.avg_executed_price = calculate_volume_weighted_price(fills);
    return report;
}
std::string MatchingEngine::generate_execution_id() {
    std::ostringstream oss;
    oss << "EXE" << next_execution_id_.fetch_add(1);
    return oss.str();
}
bool MatchingEngine::validate_order(const order::Order& order) const {
    return validate_price(order.price) && validate_quantity(order.quantity);
}
bool MatchingEngine::validate_price(core::Price price) const {
    return price > 0.0 && price < 1000000.0;
}
bool MatchingEngine::validate_quantity(core::Quantity quantity) const {
    return quantity > 0 && quantity <= 1000000;
}
void MatchingEngine::update_matching_latency(double latency_ns) {
    uint64_t ops = stats_.matching_operations.fetch_add(1);
    double current_avg = stats_.avg_matching_latency_ns.load();
    double new_avg = (current_avg * ops + latency_ns) / (ops + 1);
    stats_.avg_matching_latency_ns.store(new_avg);
    double current_max = stats_.max_matching_latency_ns.load();
    if (latency_ns > current_max) {
        stats_.max_matching_latency_ns.store(latency_ns);
    }
}
void MatchingEngine::record_fill(const Fill& fill) {
    stats_.total_fills.fetch_add(1);
    double current_volume = stats_.total_volume.load();
    while (!stats_.total_volume.compare_exchange_weak(current_volume, current_volume + fill.quantity)) {}
    double notional_value = fill.price * fill.quantity;
    double current_notional = stats_.total_notional.load();
    while (!stats_.total_notional.compare_exchange_weak(current_notional, current_notional + notional_value)) {}
}
bool MatchingEngine::perform_risk_checks(const order::Order& order) const {
    return check_position_limits(order) && check_order_limits(order);
}
bool MatchingEngine::check_position_limits(const order::Order& order) const {
    return true;
}
bool MatchingEngine::check_order_limits(const order::Order& order) const {
    return order.quantity <= 100000 && order.price * order.quantity <= 10000000.0;
}
double MatchingEngine::calculate_market_impact(const order::Order& order, order::OrderBook& book) const {
    auto depth = (order.side == core::Side::BUY) ? book.get_asks(5) : book.get_bids(5);
    double total_liquidity = 0.0;
    for (const auto& level : depth) {
        total_liquidity += level.second;
    }
    return total_liquidity > 0 ? static_cast<double>(order.quantity) / total_liquidity : 0.0;
}
core::Price MatchingEngine::calculate_volume_weighted_price(const std::vector<Fill>& fills) const {
    if (fills.empty()) return 0.0;
    double total_notional = 0.0;
    core::Quantity total_quantity = 0;
    for (const auto& fill : fills) {
        total_notional += fill.price * fill.quantity;
        total_quantity += fill.quantity;
    }
    return total_quantity > 0 ? total_notional / total_quantity : 0.0;
}
MarketMakingEngine::MarketMakingEngine(double spread_bps, core::Quantity default_size)
    : spread_bps_(spread_bps), default_size_(default_size),
      max_position_size_(1000000.0), inventory_skew_factor_(0.1)
{
    matching_engine_ = std::make_unique<MatchingEngine>();
    matching_engine_->set_execution_callback([this](const ExecutionReport& report) {
        on_trade_execution(report);
    });
    matching_engine_->set_fill_callback([this](const Fill& fill) {
        if (auto it = active_quotes_.find(fill.symbol); it != active_quotes_.end()) {
            if (fill.aggressive_order_id == it->second.first ||
                fill.aggressive_order_id == it->second.second) {
                update_position(fill.symbol, fill);
            }
        }
    });
}
MarketMakingEngine::~MarketMakingEngine() {
    stop_market_making();
}
void MarketMakingEngine::set_spread(double spread_bps) {
    spread_bps_ = spread_bps;
}
void MarketMakingEngine::set_default_size(core::Quantity size) {
    default_size_ = size;
}
void MarketMakingEngine::set_max_position_size(double max_size) {
    max_position_size_ = max_size;
}
void MarketMakingEngine::set_inventory_skew_factor(double factor) {
    inventory_skew_factor_ = factor;
}
void MarketMakingEngine::start_market_making() {
    matching_engine_->start();
}
void MarketMakingEngine::stop_market_making() {
    matching_engine_->stop();
}
void MarketMakingEngine::update_quotes(const core::Symbol& symbol, core::Price reference_price) {
    if (!should_quote(symbol)) {
        cancel_quotes(symbol);
        return;
    }
    cancel_quotes(symbol);
    auto [bid_price, ask_price] = calculate_quote_prices(symbol, reference_price);
    core::Quantity bid_size = calculate_quote_size(symbol, core::Side::BUY);
    core::Quantity ask_size = calculate_quote_size(symbol, core::Side::SELL);
    order::Order bid_order = create_quote_order(symbol, core::Side::BUY, bid_price, bid_size);
    order::Order ask_order = create_quote_order(symbol, core::Side::SELL, ask_price, ask_size);
    if (matching_engine_->submit_order(bid_order) && matching_engine_->submit_order(ask_order)) {
        active_quotes_[symbol] = {bid_order.id, ask_order.id};
    }
}
void MarketMakingEngine::cancel_quotes(const core::Symbol& symbol) {
    auto it = active_quotes_.find(symbol);
    if (it != active_quotes_.end()) {
        matching_engine_->cancel_order(it->second.first);
        matching_engine_->cancel_order(it->second.second);
        active_quotes_.erase(it);
    }
}
double MarketMakingEngine::get_position(const core::Symbol& symbol) const {
    auto it = positions_.find(symbol);
    return (it != positions_.end()) ? it->second : 0.0;
}
double MarketMakingEngine::get_unrealized_pnl(const core::Symbol& symbol) const {
    auto it = unrealized_pnl_.find(symbol);
    return (it != unrealized_pnl_.end()) ? it->second : 0.0;
}
double MarketMakingEngine::get_total_pnl() const {
    double total = 0.0;
    for (const auto& pnl : unrealized_pnl_) {
        total += pnl.second;
    }
    return total;
}
void MarketMakingEngine::on_market_data_update(const core::MarketDataTick& tick) {
    update_quotes(tick.symbol, tick.last_price);
    update_unrealized_pnl(tick.symbol, tick.last_price);
}
void MarketMakingEngine::on_trade_execution(const ExecutionReport& execution) {
    for (const auto& fill : execution.fills) {
        update_position(execution.symbol, fill);
    }
}
std::pair<core::Price, core::Price> MarketMakingEngine::calculate_quote_prices(const core::Symbol& symbol,
                                                                             core::Price reference_price) {
    double spread_fraction = spread_bps_ / 10000.0;
    double half_spread = reference_price * spread_fraction / 2.0;
    double inventory_skew = calculate_inventory_skew(symbol);
    double skew_adjustment = reference_price * inventory_skew * inventory_skew_factor_;
    core::Price bid_price = reference_price - half_spread - skew_adjustment;
    core::Price ask_price = reference_price + half_spread - skew_adjustment;
    return {bid_price, ask_price};
}
core::Quantity MarketMakingEngine::calculate_quote_size(const core::Symbol& symbol, core::Side side) {
    double position = get_position(symbol);
    double position_ratio = std::abs(position) / max_position_size_;
    double size_factor = std::max(0.1, 1.0 - position_ratio);
    return static_cast<core::Quantity>(default_size_ * size_factor);
}
void MarketMakingEngine::update_position(const core::Symbol& symbol, const Fill& fill) {
    double position_change = static_cast<double>(fill.quantity);
    if (fill.aggressive_order_id == active_quotes_[symbol].second) {
        position_change = -position_change;
    }
    positions_[symbol] += position_change;
}
void MarketMakingEngine::update_unrealized_pnl(const core::Symbol& symbol, core::Price mark_price) {
    double position = get_position(symbol);
    if (std::abs(position) > 1e-6) {
        unrealized_pnl_[symbol] = position * mark_price;
    }
}
bool MarketMakingEngine::should_quote(const core::Symbol& symbol) const {
    double position = get_position(symbol);
    return std::abs(position) < max_position_size_;
}
double MarketMakingEngine::calculate_inventory_skew(const core::Symbol& symbol) const {
    double position = get_position(symbol);
    return position / max_position_size_;
}
order::Order MarketMakingEngine::create_quote_order(const core::Symbol& symbol, core::Side side,
                                                   core::Price price, core::Quantity quantity) {
    core::OrderID id = next_quote_id_.fetch_add(1);
    return order::Order(id, symbol, side, core::OrderType::LIMIT, price, quantity);
}
double calculate_price_improvement(core::Price execution_price, core::Price reference_price, core::Side side) {
    if (side == core::Side::BUY) {
        return std::max(0.0, reference_price - execution_price);
    } else {
        return std::max(0.0, execution_price - reference_price);
    }
}
double calculate_effective_spread(core::Price bid, core::Price ask) {
    return (ask > bid) ? ask - bid : 0.0;
}
double calculate_mid_price(core::Price bid, core::Price ask) {
    return (bid + ask) / 2.0;
}
bool prices_match(core::Price incoming_price, core::Price book_price, core::Side incoming_side) {
    if (incoming_side == core::Side::BUY) {
        return incoming_price >= book_price;
    } else {
        return incoming_price <= book_price;
    }
}
core::Price get_better_price(core::Price price1, core::Price price2, core::Side side) {
    if (side == core::Side::BUY) {
        return std::max(price1, price2);
    } else {
        return std::min(price1, price2);
    }
}
bool is_marketable(const order::Order& order, const order::OrderBook& book) {
    if (order.side == core::Side::BUY) {
        auto best_ask = book.get_best_ask();
        return best_ask > 0.0 && order.price >= best_ask;
    } else {
        auto best_bid = book.get_best_bid();
        return best_bid > 0.0 && order.price <= best_bid;
    }
}
bool has_time_priority(const order::Order& order1, const order::Order& order2) {
    return order1.timestamp < order2.timestamp;
}
std::vector<order::Order> sort_by_time_priority(const std::vector<order::Order>& orders) {
    std::vector<order::Order> sorted_orders = orders;
    std::sort(sorted_orders.begin(), sorted_orders.end(),
             [](const order::Order& a, const order::Order& b) {
                 return a.timestamp < b.timestamp;
             });
    return sorted_orders;
}
}
}
