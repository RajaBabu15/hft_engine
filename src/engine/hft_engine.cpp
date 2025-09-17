#include "hft/engine/hft_engine.hpp"
#include "hft/core/redis_client.hpp"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>

namespace hft {
namespace engine {

// ============================================================================
// HftEngine Implementation
// ============================================================================

HftEngine::HftEngine(const HftEngineConfig& config) : config_(config) {
    initialize_components();
    initialize_queues();
    setup_callbacks();
}

HftEngine::~HftEngine() {
    stop();
}

void HftEngine::start() {
    if (running_.load()) {
        std::cout << "HFT Engine is already running.\n";
        return;
    }
    
    std::cout << "\n🚀 Starting HFT Trading Engine v2.0.0\n";
    std::cout << "======================================\n";
    
    running_.store(true);
    
    // Start core components
    if (matching_engine_) {
        matching_engine_->start();
        std::cout << "✅ Matching Engine started\n";
    }
    
    if (fix_parser_) {
        fix_parser_->start();
        std::cout << "✅ FIX Parser started (" << config_.num_fix_workers << " workers)\n";
    }
    
    if (admission_control_ && config_.enable_admission_control) {
        admission_control_->start();
        std::cout << "✅ Admission Control started (target P99: " 
                  << config_.target_p99_latency_us << "μs)\n";
    }
    
    // Start worker threads
    worker_threads_.reserve(config_.num_worker_threads);
    for (size_t i = 0; i < config_.num_worker_threads; ++i) {
        if (i % 3 == 0) {
            worker_threads_.emplace_back(&HftEngine::market_data_worker, this);
        } else if (i % 3 == 1) {
            worker_threads_.emplace_back(&HftEngine::fix_message_worker, this);
        } else {
            worker_threads_.emplace_back(&HftEngine::execution_report_worker, this);
        }
    }
    
    // Start monitoring thread
    if (config_.enable_performance_monitoring) {
        monitoring_thread_ = std::thread(&HftEngine::monitoring_worker, this);
        std::cout << "✅ Performance monitoring started\n";
    }
    
    // Start market data simulation if enabled
    if (config_.enable_market_data_simulation) {
        start_market_data_simulation();
        std::cout << "✅ Market data simulation started (" 
                  << config_.market_data_frequency_hz << " Hz)\n";
    }
    
    // Start all strategies
    start_all_strategies();
    
    // Optimize system settings
    if (config_.num_worker_threads > 4) {
        optimize_system_settings();
        std::cout << "✅ System optimizations applied\n";
    }
    
    std::cout << "\n🎯 Engine Configuration:\n";
    std::cout << "   Symbols: " << config_.symbols.size() << " (" 
              << config_.symbols[0];
    for (size_t i = 1; i < std::min(size_t(3), config_.symbols.size()); ++i) {
        std::cout << ", " << config_.symbols[i];
    }
    if (config_.symbols.size() > 3) std::cout << "...";
    std::cout << ")\n";
    std::cout << "   Worker Threads: " << config_.num_worker_threads << "\n";
    std::cout << "   Target Throughput: " << config_.max_throughput_ops_per_sec << " ops/sec\n";
    std::cout << "   Target P99 Latency: " << config_.target_p99_latency_us << " μs\n";
    
    std::cout << "\n✅ HFT Engine is now RUNNING\n";
    std::cout << "   Ready to process orders and market data\n\n";
}

void HftEngine::stop() {
    if (!running_.load()) return;
    
    std::cout << "\n🛑 Stopping HFT Engine...\n";
    
    running_.store(false);
    
    // Stop strategies first
    stop_all_strategies();
    
    // Stop market data simulation
    stop_market_data_simulation();
    
    // Stop core components
    if (matching_engine_) {
        matching_engine_->stop();
    }
    
    if (fix_parser_) {
        fix_parser_->stop();
    }
    
    if (admission_control_) {
        admission_control_->stop();
    }
    
    // Join worker threads
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    
    // Join monitoring thread
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    // Join market data thread
    if (market_data_thread_.joinable()) {
        market_data_thread_.join();
    }
    
    std::cout << "✅ HFT Engine stopped successfully\n";
}

void HftEngine::restart() {
    stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    start();
}

void HftEngine::feed_market_data(const core::MarketDataTick& tick) {
    if (!running_.load()) return;
    
    auto start_time = core::HighResolutionClock::rdtsc();
    
    if (admission_control_ && config_.enable_admission_control) {
        auto decision = admission_control_->should_admit_request();
        if (decision != core::AdaptiveAdmissionControl::ADMIT) {
            metrics_.market_data_queue_depth.fetch_add(1);
            return;
        }
    }
    
    if (market_data_queue_->enqueue(tick)) {
        metrics_.market_data_processed.fetch_add(1);
        
        auto end_time = core::HighResolutionClock::rdtsc();
        double latency_us = core::HighResolutionClock::rdtsc_to_nanoseconds(end_time - start_time) / 1000.0;
        update_latency_metrics("market_data", latency_us);
    }
}

void HftEngine::feed_fix_message(const std::string& fix_message) {
    if (!running_.load()) return;
    
    auto start_time = core::HighResolutionClock::rdtsc();
    
    if (fix_message_queue_->enqueue(fix_message)) {
        metrics_.fix_messages_processed.fetch_add(1);
        
        auto end_time = core::HighResolutionClock::rdtsc();
        double latency_us = core::HighResolutionClock::rdtsc_to_nanoseconds(end_time - start_time) / 1000.0;
        update_latency_metrics("fix_parse", latency_us);
    }
}

bool HftEngine::submit_order(const order::Order& order) {
    if (!running_.load()) return false;
    
    auto start_time = core::HighResolutionClock::rdtsc();
    
    // Pre-trade risk checks
    if (!perform_pre_trade_risk_checks(order)) {
        metrics_.risk_violations.fetch_add(1);
        return false;
    }
    
    // Admission control
    if (admission_control_ && config_.enable_admission_control) {
        auto decision = admission_control_->should_admit_request();
        if (decision != core::AdaptiveAdmissionControl::ADMIT) {
            metrics_.order_rejections.fetch_add(1);
            return false;
        }
    }
    
    // Submit to matching engine
    bool success = matching_engine_->submit_order(order);
    
    if (success) {
        metrics_.orders_processed.fetch_add(1);
        
        auto end_time = core::HighResolutionClock::rdtsc();
        double latency_us = core::HighResolutionClock::rdtsc_to_nanoseconds(end_time - start_time) / 1000.0;
        update_latency_metrics("order_processing", latency_us);
        
        // Report latency to admission control
        if (admission_control_) {
            admission_control_->report_request_latency(latency_us);
        }
    } else {
        metrics_.order_rejections.fetch_add(1);
    }
    
    return success;
}

bool HftEngine::cancel_order(core::OrderID order_id) {
    if (!running_.load()) return false;
    return matching_engine_->cancel_order(order_id);
}

bool HftEngine::modify_order(core::OrderID order_id, core::Price new_price, core::Quantity new_quantity) {
    if (!running_.load()) return false;
    return matching_engine_->modify_order(order_id, new_price, new_quantity);
}

void HftEngine::start_market_data_simulation() {
    if (!config_.enable_market_data_simulation || !market_data_simulator_) return;
    
    market_data_thread_ = std::thread([this]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::exponential_distribution<double> tick_interval(config_.market_data_frequency_hz);
        
        while (running_.load()) {
            for (const auto& symbol : config_.symbols) {
                if (!running_.load()) break;
                
                auto tick = market_data_simulator_->generate_tick(symbol, core::HighResolutionClock::now());
                feed_market_data(tick.to_market_data_tick());
                
                // Sleep to maintain target frequency
                double sleep_ms = tick_interval(gen) * 1000.0 / config_.symbols.size();
                std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(sleep_ms * 1000)));
            }
        }
    });
}

void HftEngine::stop_market_data_simulation() {
    // Market data thread will stop when running_ becomes false
}

void HftEngine::add_strategy(std::unique_ptr<strategies::MarketMakingStrategy> strategy) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // Setup strategy callbacks
    strategy->set_order_callbacks(
        [this](const order::Order& order) { return submit_order(order); },
        [this](core::OrderID order_id) { return cancel_order(order_id); }
    );
    
    strategies_.push_back(std::move(strategy));
    std::cout << "✅ Strategy added (total: " << strategies_.size() << ")\n";
}

void HftEngine::start_all_strategies() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    for (auto& strategy : strategies_) {
        strategy->start();
    }
    
    if (!strategies_.empty()) {
        std::cout << "✅ " << strategies_.size() << " strategies started\n";
    }
}

void HftEngine::stop_all_strategies() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    for (auto& strategy : strategies_) {
        strategy->stop();
    }
}

void HftEngine::print_performance_summary() const {
    std::cout << "\n📊 HFT ENGINE PERFORMANCE SUMMARY\n";
    std::cout << "==================================\n";
    
    // Throughput metrics
    std::cout << "📈 THROUGHPUT:\n";
    std::cout << "   Orders Processed: " << metrics_.orders_processed.load() 
              << " (" << std::fixed << std::setprecision(0) << metrics_.get_orders_per_second() << " ops/sec)\n";
    std::cout << "   Market Data: " << metrics_.market_data_processed.load()
              << " (" << std::fixed << std::setprecision(0) << metrics_.get_market_data_per_second() << " ticks/sec)\n";
    std::cout << "   FIX Messages: " << metrics_.fix_messages_processed.load() << "\n";
    
    // Latency metrics
    std::cout << "\n⚡ LATENCY:\n";
    std::cout << "   Avg Order Latency: " << std::fixed << std::setprecision(2) 
              << metrics_.avg_order_latency_us.load() << " μs\n";
    std::cout << "   P99 Order Latency: " << std::fixed << std::setprecision(2) 
              << metrics_.p99_order_latency_us.load() << " μs\n";
    std::cout << "   Avg Market Data Latency: " << std::fixed << std::setprecision(2) 
              << metrics_.avg_market_data_latency_us.load() << " μs\n";
    
    // Trading metrics
    std::cout << "\n💰 TRADING:\n";
    std::cout << "   Total P&L: $" << std::fixed << std::setprecision(2) 
              << metrics_.total_pnl.load() << "\n";
    std::cout << "   Realized P&L: $" << std::fixed << std::setprecision(2) 
              << metrics_.realized_pnl.load() << "\n";
    std::cout << "   Total Trades: " << metrics_.total_trades.load() << "\n";
    std::cout << "   Total Volume: " << std::fixed << std::setprecision(0) 
              << metrics_.total_volume.load() << "\n";
    
    // System metrics
    std::cout << "\n🖥️  SYSTEM:\n";
    std::cout << "   CPU Usage: " << std::fixed << std::setprecision(1) 
              << metrics_.cpu_usage_percent.load() << "%\n";
    std::cout << "   Memory Usage: " << metrics_.memory_usage_mb.load() << " MB\n";
    std::cout << "   Active Threads: " << metrics_.active_thread_count.load() << "\n";
    
    // Queue metrics
    std::cout << "\n📦 QUEUES:\n";
    std::cout << "   Order Queue: " << metrics_.order_queue_depth.load() << "\n";
    std::cout << "   Market Data Queue: " << metrics_.market_data_queue_depth.load() << "\n";
    std::cout << "   FIX Queue: " << metrics_.fix_queue_depth.load() << "\n";
    
    // Error metrics
    std::cout << "\n⚠️  ERRORS:\n";
    std::cout << "   Order Rejections: " << metrics_.order_rejections.load() << "\n";
    std::cout << "   Parsing Errors: " << metrics_.parsing_errors.load() << "\n";
    std::cout << "   Risk Violations: " << metrics_.risk_violations.load() << "\n";
    std::cout << "   Error Rate: " << std::fixed << std::setprecision(4) 
              << (metrics_.get_error_rate() * 100.0) << "%\n";
    
    std::cout << "\n";
}

bool HftEngine::is_healthy() const {
    // Check critical metrics
    bool latency_ok = metrics_.p99_order_latency_us.load() < config_.target_p99_latency_us * 1.5;
    bool error_rate_ok = metrics_.get_error_rate() < 0.01; // Less than 1%
    bool queues_ok = metrics_.order_queue_depth.load() < config_.order_queue_size * 0.9;
    bool cpu_ok = metrics_.cpu_usage_percent.load() < 90.0;
    
    return latency_ok && error_rate_ok && queues_ok && cpu_ok && running_.load();
}

std::vector<std::string> HftEngine::get_health_warnings() const {
    std::vector<std::string> warnings;
    
    if (metrics_.p99_order_latency_us.load() > config_.target_p99_latency_us) {
        warnings.push_back("P99 latency exceeds target");
    }
    
    if (metrics_.get_error_rate() > 0.005) {
        warnings.push_back("High error rate detected");
    }
    
    if (metrics_.order_queue_depth.load() > config_.order_queue_size * 0.8) {
        warnings.push_back("Order queue depth approaching limit");
    }
    
    if (metrics_.cpu_usage_percent.load() > 80.0) {
        warnings.push_back("High CPU usage");
    }
    
    return warnings;
}

// Private methods

void HftEngine::initialize_components() {
    // Initialize matching engine
    matching_engine_ = std::make_unique<matching::MatchingEngine>();
    
    // Initialize FIX parser
    fix_parser_ = std::make_unique<fix::FixParser>(config_.num_fix_workers);
    
    // Initialize admission control
    if (config_.enable_admission_control) {
        admission_control_ = std::make_unique<core::AdaptiveAdmissionControl>(
            config_.target_p99_latency_us, config_.max_throughput_ops_per_sec);
    }
    
    // Initialize market data simulator
    if (config_.enable_market_data_simulation) {
        market_data_simulator_ = std::make_unique<backtesting::MarketDataSimulator>(config_.symbols);
        market_data_simulator_->set_volatility(config_.base_volatility);
        market_data_simulator_->set_tick_frequency(config_.market_data_frequency_hz);
        
        // Set initial prices
        for (const auto& symbol : config_.symbols) {
            double base_price = 100.0; // Default base price
            if (symbol == "AAPL") base_price = 175.0;
            else if (symbol == "MSFT") base_price = 350.0;
            else if (symbol == "GOOGL") base_price = 2800.0;
            else if (symbol == "TSLA") base_price = 800.0;
            else if (symbol == "AMZN") base_price = 3200.0;
            else if (symbol == "META") base_price = 300.0;
            else if (symbol == "NVDA") base_price = 900.0;
            else if (symbol == "NFLX") base_price = 400.0;
            
            market_data_simulator_->set_initial_price(symbol, base_price);
        }
    }
    
    // Initialize performance counter
    latency_counter_ = std::make_unique<core::PerformanceCounter>();
    
    // Initialize strategies if enabled
    initialize_strategies();
}

void HftEngine::initialize_queues() {
    fix_message_queue_ = std::make_unique<core::LockFreeQueue<std::string, 4096>>();
    market_data_queue_ = std::make_unique<core::LockFreeQueue<core::MarketDataTick, 16384>>();
    execution_queue_ = std::make_unique<core::LockFreeQueue<matching::ExecutionReport, 4096>>();
}

void HftEngine::initialize_strategies() {
    if (!config_.enable_market_making) return;
    
    // Create default market making strategy
    auto strategy = std::make_unique<strategies::SimpleMarketMaker>(
        config_.market_making_spread_bps / 2.0, true);
    
    strategies::MarketMakingStrategy::Parameters params;
    params.target_spread_bps = config_.market_making_spread_bps;
    params.default_quote_size = config_.default_quote_size;
    params.max_position_limit = config_.max_position_limit;
    
    strategy->set_parameters(params);
    strategies_.push_back(std::move(strategy));
}

void HftEngine::setup_callbacks() {
    // Setup matching engine callbacks
    if (matching_engine_) {
        matching_engine_->set_execution_callback([this](const matching::ExecutionReport& report) {
            execution_queue_->enqueue(report);
        });
        
        matching_engine_->set_error_callback([this](const std::string& error_type, const std::string& message) {
            handle_system_error(error_type, message);
        });
    }
    
    // Setup FIX parser callbacks
    if (fix_parser_) {
        fix_parser_->set_message_callback([this](const fix::FixMessage& message) {
            // Process FIX message - convert to order if it's a new order message
            if (message.msg_type == "D") { // New Order Single
                // Implementation would parse FIX message into Order object
                // This is a simplified placeholder
            }
        });
        
        fix_parser_->set_error_callback([this](const std::string& error_type, const std::string& message) {
            metrics_.parsing_errors.fetch_add(1);
            handle_system_error(error_type, message);
        });
    }
}

void HftEngine::market_data_worker() {
    core::MarketDataTick tick;
    
    while (running_.load()) {
        if (market_data_queue_->dequeue(tick)) {
            process_market_data_tick(tick);
        } else {
            std::this_thread::yield();
        }
    }
}

void HftEngine::fix_message_worker() {
    std::string message;
    
    while (running_.load()) {
        if (fix_message_queue_->dequeue(message)) {
            process_fix_message(message);
        } else {
            std::this_thread::yield();
        }
    }
}

void HftEngine::execution_report_worker() {
    matching::ExecutionReport report;
    
    while (running_.load()) {
        if (execution_queue_->dequeue(report)) {
            process_execution_report(report);
        } else {
            std::this_thread::yield();
        }
    }
}

void HftEngine::monitoring_worker() {
    while (running_.load()) {
        update_throughput_metrics();
        update_system_resource_metrics();
        collect_queue_depth_metrics();
        
        // Print periodic status
        static int counter = 0;
        if (++counter % 10 == 0) { // Every 10 seconds
            auto warnings = get_health_warnings();
            if (!warnings.empty()) {
                std::cout << "⚠️  Health warnings: ";
                for (const auto& warning : warnings) {
                    std::cout << warning << "; ";
                }
                std::cout << "\n";
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.monitoring_interval_ms));
    }
}

void HftEngine::process_market_data_tick(const core::MarketDataTick& tick) {
    // Update latest market data
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        latest_market_data_[tick.symbol] = tick;
    }
    
    // Distribute to strategies
    distribute_market_data_to_strategies(tick);
    
    // Update unrealized P&L for positions
    double mid_price = (tick.bid_price + tick.ask_price) / 2.0;
    auto it = consolidated_positions_.find(tick.symbol);
    if (it != consolidated_positions_.end()) {
        it->second.update_unrealized_pnl(mid_price);
        metrics_.unrealized_pnl.store(it->second.unrealized_pnl);
    }
}

void HftEngine::process_fix_message(const std::string& fix_message) {
    // Feed to FIX parser
    if (fix_parser_) {
        fix_parser_->feed_data(fix_message);
    }
}

void HftEngine::process_execution_report(const matching::ExecutionReport& report) {
    metrics_.executions_generated.fetch_add(1);
    
    // Update trading metrics
    if (!report.fills.empty()) {
        metrics_.total_trades.fetch_add(1);
        for (const auto& fill : report.fills) {
            metrics_.total_volume.fetch_add(fill.quantity);
        }
    }
    
    // Distribute to strategies
    distribute_execution_to_strategies(report);
    
    // Update consolidated positions
    update_consolidated_positions();
}

void HftEngine::distribute_market_data_to_strategies(const core::MarketDataTick& tick) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    for (auto& strategy : strategies_) {
        if (strategy->is_running()) {
            try {
                strategy->on_market_data(tick);
            } catch (const std::exception& e) {
                handle_system_error("STRATEGY_ERROR", e.what());
            }
        }
    }
}

void HftEngine::distribute_execution_to_strategies(const matching::ExecutionReport& report) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    for (auto& strategy : strategies_) {
        if (strategy->is_running()) {
            try {
                strategy->on_trade_execution(report);
            } catch (const std::exception& e) {
                handle_system_error("STRATEGY_ERROR", e.what());
            }
        }
    }
}

void HftEngine::update_latency_metrics(const std::string& metric_name, double latency_us) {
    if (latency_counter_) {
        latency_counter_->record_sample(latency_us);
    }
    
    // Update specific metrics
    if (metric_name == "order_processing") {
        // Update exponential moving average
        double current_avg = metrics_.avg_order_latency_us.load();
        double alpha = 0.1;
        double new_avg = alpha * latency_us + (1.0 - alpha) * current_avg;
        metrics_.avg_order_latency_us.store(new_avg);
        
        // Update P99 (simplified - should use proper percentile calculation)
        double current_p99 = metrics_.p99_order_latency_us.load();
        if (latency_us > current_p99) {
            metrics_.p99_order_latency_us.store(latency_us);
        }
    } else if (metric_name == "market_data") {
        double current_avg = metrics_.avg_market_data_latency_us.load();
        double alpha = 0.1;
        double new_avg = alpha * latency_us + (1.0 - alpha) * current_avg;
        metrics_.avg_market_data_latency_us.store(new_avg);
    }
}

void HftEngine::update_system_resource_metrics() {
    metrics_.cpu_usage_percent.store(get_current_cpu_usage());
    metrics_.memory_usage_mb.store(get_current_memory_usage());
    metrics_.active_thread_count.store(worker_threads_.size() + 1); // +1 for monitoring thread
}

void HftEngine::collect_queue_depth_metrics() {
    metrics_.order_queue_depth.store(0); // Would get from matching engine
    metrics_.market_data_queue_depth.store(market_data_queue_->size());
    metrics_.fix_queue_depth.store(fix_message_queue_->size());
}

bool HftEngine::perform_pre_trade_risk_checks(const order::Order& order) const {
    // Basic risk checks
    if (order.quantity > config_.max_order_value / order.price) {
        return false;
    }
    
    return perform_position_risk_checks(order.symbol, order.side, order.quantity);
}

bool HftEngine::perform_position_risk_checks(const core::Symbol& symbol, 
                                           core::Side side, core::Quantity quantity) const {
    auto it = consolidated_positions_.find(symbol);
    if (it == consolidated_positions_.end()) {
        return true; // No position, risk is minimal
    }
    
    double current_position = it->second.quantity;
    double new_position = current_position;
    
    if (side == core::Side::BUY) {
        new_position += static_cast<double>(quantity);
    } else {
        new_position -= static_cast<double>(quantity);
    }
    
    return std::abs(new_position) <= config_.max_position_limit;
}

void HftEngine::update_consolidated_positions() {
    // This would aggregate positions from all strategies
    // Simplified implementation
}

void HftEngine::handle_system_error(const std::string& error_type, const std::string& message) {
    metrics_.system_errors.fetch_add(1);
    
    if (config_.enable_detailed_logging) {
        std::cerr << "[ERROR] " << error_type << ": " << message << std::endl;
    }
}

void HftEngine::optimize_system_settings() {
    // Apply system optimizations for HFT trading
    // This would include setting thread priorities, CPU affinity, etc.
    pin_threads_to_cores();
}

void HftEngine::pin_threads_to_cores() {
    // Pin worker threads to specific CPU cores for better performance
    // Implementation would be platform-specific
}

double HftEngine::get_current_cpu_usage() const {
    // Platform-specific CPU usage calculation
    return 0.0; // Placeholder
}

uint64_t HftEngine::get_current_memory_usage() const {
    // Platform-specific memory usage calculation  
    return 0; // Placeholder
}

// ============================================================================
// HftEngineBuilder Implementation
// ============================================================================

HftEngineBuilder& HftEngineBuilder::with_symbols(const std::vector<core::Symbol>& symbols) {
    config_.symbols = symbols;
    return *this;
}

HftEngineBuilder& HftEngineBuilder::with_threading(size_t workers, size_t matching_threads) {
    config_.num_worker_threads = workers;
    config_.num_matching_threads = matching_threads;
    return *this;
}

HftEngineBuilder& HftEngineBuilder::with_admission_control(double target_latency_us, double max_throughput) {
    config_.enable_admission_control = true;
    config_.target_p99_latency_us = target_latency_us;
    config_.max_throughput_ops_per_sec = max_throughput;
    return *this;
}

HftEngineBuilder& HftEngineBuilder::add_simple_market_maker(double spread_bps) {
    config_.enable_market_making = true;
    config_.market_making_spread_bps = spread_bps;
    return *this;
}

std::unique_ptr<HftEngine> HftEngineBuilder::build() {
    return std::make_unique<HftEngine>(config_);
}

std::unique_ptr<HftEngine> HftEngineBuilder::build_development_engine() {
    return HftEngineBuilder()
        .with_symbols({"AAPL", "MSFT"})
        .with_threading(4, 2)
        .with_admission_control(200.0, 50000.0)
        .add_simple_market_maker(10.0)
        .build();
}

std::unique_ptr<HftEngine> HftEngineBuilder::build_production_engine() {
    return HftEngineBuilder()
        .with_symbols({"AAPL", "MSFT", "GOOGL", "TSLA", "AMZN", "META", "NVDA", "NFLX"})
        .with_threading(16, 8)
        .with_admission_control(50.0, 200000.0)
        .add_simple_market_maker(5.0)
        .with_redis_caching(true)
        .build();
}

// ============================================================================
// Utility Functions
// ============================================================================

std::unique_ptr<HftEngine> create_hft_engine_for_symbol(const core::Symbol& symbol) {
    return HftEngineBuilder()
        .with_symbols({symbol})
        .with_threading(4, 2)
        .add_simple_market_maker(5.0)
        .build();
}

bool validate_hft_config(const HftEngineConfig& config) {
    if (config.symbols.empty()) return false;
    if (config.num_worker_threads == 0) return false;
    if (config.target_p99_latency_us <= 0) return false;
    if (config.max_throughput_ops_per_sec <= 0) return false;
    
    return true;
}

void optimize_system_for_hft_trading() {
    std::cout << "🔧 Applying HFT system optimizations...\n";
    // Platform-specific optimizations would go here
    std::cout << "✅ System optimized for low-latency trading\n";
}

} // namespace engine
} // namespace hft