#include "hft/matching/matching_engine.hpp"
#include "hft/core/clock.hpp"
#include <algorithm>
#include <sstream>
#include <filesystem>
namespace hft {
namespace matching {
MatchingEngine::MatchingEngine(MatchingAlgorithm algorithm, const std::string& log_path, bool use_segment_tree)
    : algorithm_(algorithm), use_segment_tree_(use_segment_tree)
{
    incoming_orders_ = std::make_unique<core::LockFreeQueue<order::Order, ORDER_QUEUE_SIZE>>();
    logger_ = std::make_unique<core::AsyncLogger>(log_path, core::LogLevel::INFO);
    logger_->start();
    std::string implementation_type = use_segment_tree_ ? "Segment Tree" : "Legacy";
    logger_->info("MatchingEngine initialized with algorithm: " +
                  std::to_string(static_cast<int>(algorithm)) +
                  ", Implementation: " + implementation_type, "ENGINE");
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
        std::string side_str = (order.side == core::Side::BUY) ? "BUY" : "SELL";
        logger_->log_order_received(order.id, order.symbol, order.price, order.quantity, side_str);
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
    order_copy.timestamp = core::HighResolutionClock::now();
    bool enqueued = incoming_orders_->enqueue(std::move(order_copy));
    if (!enqueued && logger_) {
        logger_->warn("Order queue full, order " + std::to_string(order.id) + " dropped", "ENGINE");
    }
    return enqueued;
}
bool MatchingEngine::cancel_order(core::OrderID order_id) {
    order::Order cancelled_order_copy;
    bool found = false;
    if (use_segment_tree_) {
        auto it = segment_tree_orders_.find(order_id);
        if (it == segment_tree_orders_.end()) {
            if (logger_) {
                logger_->warn("Attempted to cancel non-existent order " + std::to_string(order_id), "ORDER_MGMT");
            }
            return false;
        }
        cancelled_order_copy = it->second;
        cancelled_order_copy.status = core::OrderStatus::CANCELLED;
        found = true;
        auto book_it = segment_tree_books_.find(cancelled_order_copy.symbol);
        if (book_it != segment_tree_books_.end()) {
            book_it->second->remove_order(cancelled_order_copy.price, cancelled_order_copy.remaining_quantity(), cancelled_order_copy.side);
        }
        segment_tree_orders_.erase(it);
    } else {
        auto it = active_orders_.find(order_id);
        if (it == active_orders_.end()) {
            if (logger_) {
                logger_->warn("Attempted to cancel non-existent order " + std::to_string(order_id), "ORDER_MGMT");
            }
            return false;
        }
        cancelled_order_copy = it->second;
        cancelled_order_copy.status = core::OrderStatus::CANCELLED;
        found = true;
        auto* book = get_order_book(cancelled_order_copy.symbol);
        if (book) {
            book->cancel_order(order_id);
        }
        active_orders_.erase(it);
    }
    if (logger_) {
        logger_->log_order_cancelled(order_id, "User requested");
    }
    if (!found) {
        return false;
    }
    ExecutionReport report(cancelled_order_copy);
    report.status = core::OrderStatus::CANCELLED;
    if (execution_callback_) {
        execution_callback_(report);
    }
    return true;
}
bool MatchingEngine::modify_order(core::OrderID order_id, core::Price new_price, core::Quantity new_quantity) {
    order::Order modified_order;
    bool found = false;
    if (use_segment_tree_) {
        auto it = segment_tree_orders_.find(order_id);
        if (it != segment_tree_orders_.end()) {
            modified_order = it->second;
            found = true;
        }
    } else {
        auto it = active_orders_.find(order_id);
        if (it != active_orders_.end()) {
            modified_order = it->second;
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    cancel_order(order_id);
    modified_order.id = next_execution_id_.fetch_add(1);
    modified_order.price = new_price;
    modified_order.quantity = new_quantity;
    modified_order.filled_quantity = 0;
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
    if (use_segment_tree_) {
        symbols.reserve(segment_tree_books_.size());
        for (const auto& pair : segment_tree_books_) {
            symbols.push_back(pair.first);
        }
    } else {
        symbols.reserve(order_books_.size());
        for (const auto& pair : order_books_) {
            symbols.push_back(pair.first);
        }
    }
    return symbols;
}
bool MatchingEngine::has_order(core::OrderID order_id) const {
    if (use_segment_tree_) {
        return segment_tree_orders_.find(order_id) != segment_tree_orders_.end();
    } else {
        return active_orders_.find(order_id) != active_orders_.end();
    }
}
order::Order MatchingEngine::get_order(core::OrderID order_id) const {
    if (use_segment_tree_) {
        auto it = segment_tree_orders_.find(order_id);
        if (it != segment_tree_orders_.end()) {
            return it->second;
        }
    } else {
        auto it = active_orders_.find(order_id);
        if (it != active_orders_.end()) {
            return it->second;
        }
    }
    return order::Order();
}
std::vector<order::Order> MatchingEngine::get_orders_for_symbol(const core::Symbol& symbol) const {
    std::vector<order::Order> orders;
    if (use_segment_tree_) {
        for (const auto& pair : segment_tree_orders_) {
            if (pair.second.symbol == symbol) {
                orders.push_back(pair.second);
            }
        }
    } else {
        for (const auto& pair : active_orders_) {
            if (pair.second.symbol == symbol) {
                orders.push_back(pair.second);
            }
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
    std::vector<Fill> fills;
    order::Order active_order = order;
    if (use_segment_tree_) {
        core::OrderBookSegmentTree& segment_book = get_or_create_segment_tree_book(order.symbol);
        segment_tree_orders_[order.id] = order;
        fills = match_order_segment_tree_price_time(active_order, segment_book);
        segment_tree_orders_[order.id] = active_order;
        if (active_order.remaining_quantity() > 0 &&
            active_order.status != core::OrderStatus::CANCELLED) {
            segment_book.add_order(active_order.price, active_order.remaining_quantity(), active_order.side);
        } else {
            segment_tree_orders_.erase(order.id);
        }
    } else {
        order::OrderBook& book = get_or_create_order_book(order.symbol);
        active_orders_[order.id] = order;
        active_order = active_orders_[order.id];
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
        active_orders_[order.id] = active_order;
        if (active_order.remaining_quantity() > 0 &&
            active_order.status != core::OrderStatus::CANCELLED) {
            book.add_order(active_order);
        } else {
            active_orders_.erase(active_order.id);
        }
    }
    update_order_status(active_order, fills);
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
    order::Order mutable_incoming = incoming_order;
    if (mutable_incoming.side == core::Side::BUY) {
        while (mutable_incoming.remaining_quantity() > 0) {
            auto best_ask = book.get_best_ask();
            if (best_ask == 0.0 || mutable_incoming.price < best_ask) {
                break;
            }
            auto ask_orders = book.get_orders_at_price_level(best_ask, core::Side::SELL);
            if (ask_orders.empty()) {
                break;
            }
            auto passive_order_id = ask_orders[0].id;
            auto passive_order_it = active_orders_.find(passive_order_id);
            if (passive_order_it == active_orders_.end()) {
                book.cancel_order(passive_order_id);
                continue;
            }
            order::Order& passive_order = passive_order_it->second;
            core::Quantity fill_quantity = std::min(mutable_incoming.remaining_quantity(),
                                                   passive_order.remaining_quantity());
            Fill fill(mutable_incoming.id, passive_order.id, best_ask, fill_quantity,
                     mutable_incoming.symbol, core::HighResolutionClock::now());
            fills.push_back(fill);
            mutable_incoming.filled_quantity += fill_quantity;
            passive_order.filled_quantity += fill_quantity;
            book.fill_order(passive_order.id, fill_quantity);
            if (passive_order.remaining_quantity() == 0) {
                passive_order.status = core::OrderStatus::FILLED;
                active_orders_.erase(passive_order_it);
            } else {
                passive_order.status = core::OrderStatus::PARTIALLY_FILLED;
            }
        }
    } else {
        while (mutable_incoming.remaining_quantity() > 0) {
            auto best_bid = book.get_best_bid();
            if (best_bid == 0.0 || mutable_incoming.price > best_bid) {
                break;
            }
            auto bid_orders = book.get_orders_at_price_level(best_bid, core::Side::BUY);
            if (bid_orders.empty()) {
                break;
            }
            auto passive_order_id = bid_orders[0].id;
            auto passive_order_it = active_orders_.find(passive_order_id);
            if (passive_order_it == active_orders_.end()) {
                book.cancel_order(passive_order_id);
                continue;
            }
            order::Order& passive_order = passive_order_it->second;
            core::Quantity fill_quantity = std::min(mutable_incoming.remaining_quantity(),
                                                   passive_order.remaining_quantity());
            Fill fill(mutable_incoming.id, passive_order.id, best_bid, fill_quantity,
                     mutable_incoming.symbol, core::HighResolutionClock::now());
            fills.push_back(fill);
            mutable_incoming.filled_quantity += fill_quantity;
            passive_order.filled_quantity += fill_quantity;
            book.fill_order(passive_order.id, fill_quantity);
            if (passive_order.remaining_quantity() == 0) {
                passive_order.status = core::OrderStatus::FILLED;
                active_orders_.erase(passive_order_it);
            } else {
                passive_order.status = core::OrderStatus::PARTIALLY_FILLED;
            }
        }
    }
    auto incoming_it = active_orders_.find(mutable_incoming.id);
    if (incoming_it != active_orders_.end()) {
        incoming_it->second.filled_quantity = mutable_incoming.filled_quantity;
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
core::OrderBookSegmentTree& MatchingEngine::get_or_create_segment_tree_book(const core::Symbol& symbol) {
    auto it = segment_tree_books_.find(symbol);
    if (it == segment_tree_books_.end()) {
        segment_tree_books_[symbol] = std::make_unique<core::OrderBookSegmentTree>();
        if (logger_) {
            logger_->info("Created new segment tree order book for symbol: " + symbol, "ENGINE");
        }
        return *segment_tree_books_[symbol];
    }
    return *it->second;
}
std::vector<Fill> MatchingEngine::match_order_segment_tree(const order::Order& incoming_order,
                                                          core::OrderBookSegmentTree& segment_book) {
    switch (algorithm_) {
        case MatchingAlgorithm::PRICE_TIME_PRIORITY:
            return match_order_segment_tree_price_time(incoming_order, segment_book);
        case MatchingAlgorithm::PRO_RATA:
            return match_order_segment_tree_price_time(incoming_order, segment_book);
        case MatchingAlgorithm::SIZE_PRIORITY:
            return match_order_segment_tree_price_time(incoming_order, segment_book);
        case MatchingAlgorithm::TIME_PRIORITY:
            return match_order_segment_tree_price_time(incoming_order, segment_book);
        default:
            return match_order_segment_tree_price_time(incoming_order, segment_book);
    }
}
std::vector<Fill> MatchingEngine::match_order_segment_tree_price_time(const order::Order& incoming_order,
                                                                     core::OrderBookSegmentTree& segment_book) {
    std::vector<Fill> fills;
    order::Order mutable_incoming = incoming_order;
    if (mutable_incoming.side == core::Side::BUY) {
        while (mutable_incoming.remaining_quantity() > 0) {
            core::Price best_ask = segment_book.get_best_ask();
            if (best_ask == 0.0 || mutable_incoming.price < best_ask) {
                break;
            }
            auto ask_depth = segment_book.get_ask_depth(1);
            if (ask_depth.empty()) {
                break;
            }
            core::Quantity available_quantity = std::min(
                mutable_incoming.remaining_quantity(),
                ask_depth[0].second
            );
            if (available_quantity > 0) {
                core::OrderID passive_id = generate_synthetic_order_id();
                Fill fill(
                    mutable_incoming.id,
                    passive_id,
                    best_ask,
                    available_quantity,
                    mutable_incoming.symbol,
                    core::HighResolutionClock::now()
                );
                fills.push_back(fill);
                mutable_incoming.filled_quantity += available_quantity;
                segment_book.remove_order(best_ask, available_quantity, core::Side::SELL);
                if (logger_) {
                    logger_->debug(
                        "Segment tree match: " + std::to_string(mutable_incoming.id) +
                        " @ " + std::to_string(best_ask),
                        "MATCHING"
                    );
                }
            } else {
                break;
            }
        }
    } else {
        while (mutable_incoming.remaining_quantity() > 0) {
            core::Price best_bid = segment_book.get_best_bid();
            if (best_bid == 0.0 || mutable_incoming.price > best_bid) {
                break;
            }
            auto bid_depth = segment_book.get_bid_depth(1);
            if (bid_depth.empty()) {
                break;
            }
            core::Quantity available_quantity = std::min(
                mutable_incoming.remaining_quantity(),
                bid_depth[0].second
            );
            if (available_quantity > 0) {
                core::OrderID passive_id = generate_synthetic_order_id();
                Fill fill(
                    mutable_incoming.id,
                    passive_id,
                    best_bid,
                    available_quantity,
                    mutable_incoming.symbol,
                    core::HighResolutionClock::now()
                );
                fills.push_back(fill);
                mutable_incoming.filled_quantity += available_quantity;
                segment_book.remove_order(best_bid, available_quantity, core::Side::BUY);
                if (logger_) {
                    logger_->debug(
                        "Segment tree match: " + std::to_string(mutable_incoming.id) +
                        " @ " + std::to_string(best_bid),
                        "MATCHING"
                    );
                }
            } else {
                break;
            }
        }
    }
    if (segment_tree_orders_.find(mutable_incoming.id) != segment_tree_orders_.end()) {
        segment_tree_orders_[mutable_incoming.id].filled_quantity = mutable_incoming.filled_quantity;
    }
    return fills;
}
std::vector<order::Order> MatchingEngine::get_segment_tree_orders_at_price(core::Price price, core::Side side) const {
    std::vector<order::Order> orders_at_price;
    for (const auto& pair : segment_tree_orders_) {
        const order::Order& order = pair.second;
        if (order.price == price && order.side == side &&
            order.remaining_quantity() > 0 &&
            order.status != core::OrderStatus::CANCELLED &&
            order.status != core::OrderStatus::FILLED) {
            orders_at_price.push_back(order);
        }
    }
    std::sort(orders_at_price.begin(), orders_at_price.end(),
             [](const order::Order& a, const order::Order& b) {
                 return a.timestamp < b.timestamp;
             });
    return orders_at_price;
}
core::OrderID MatchingEngine::generate_synthetic_order_id() {
    static std::atomic<core::OrderID> synthetic_counter{900000000};
    return synthetic_counter.fetch_add(1);
}
void MatchingEngine::store_segment_tree_order(const order::Order& order) {
    segment_tree_orders_[order.id] = order;
    if (logger_) {
        logger_->debug("Stored order " + std::to_string(order.id) + " in segment tree storage", "ENGINE");
    }
}
void MatchingEngine::update_segment_tree_order(const order::Order& order) {
    auto it = segment_tree_orders_.find(order.id);
    if (it != segment_tree_orders_.end()) {
        it->second = order;
        if (logger_) {
            logger_->debug("Updated order " + std::to_string(order.id) + " in segment tree storage", "ENGINE");
        }
    }
}
void MatchingEngine::remove_segment_tree_order(core::OrderID order_id) {
    auto erased = segment_tree_orders_.erase(order_id);
    if (logger_ && erased > 0) {
        logger_->debug("Removed order " + std::to_string(order_id) + " from segment tree storage", "ENGINE");
    }
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
    report.timestamp = core::HighResolutionClock::now();
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
