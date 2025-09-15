#include "hft/logger.h"
#include "hft/risk_manager.h"
#include "hft/order.h"
#include "hft/matching_engine.h"
#include "hft/deep_profiler.h"

// New enhanced features
#include "hft/latency_metrics.h"
#include "hft/lockfree_queue.h"
#include "hft/fix_parser.h"
#include "hft/admission_control.h"
#include "hft/market_simulator.h"
#include "hft/tick_replay.h"
#include "hft/pnl_tracker.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <random>
#include <atomic>

using namespace hft;

// Enhanced trading strategy with all new features
class EnhancedMarketMaker {
private:
    Logger& logger_;
    MatchingEngine& engine_;
    AdaptiveAdmissionControl admission_control_;
    LatencyTracker latency_tracker_;
    PnLManager pnl_manager_;
    
    // Strategy parameters
    Symbol target_symbol_;
    std::string strategy_id_;
    Price spread_ticks_;
    Quantity max_position_;
    
    // Market state
    std::atomic<Price> best_bid_{0};
    std::atomic<Price> best_ask_{0};
    std::atomic<int64_t> current_position_{0};
    
    // Order management
    std::atomic<OrderId> next_order_id_{1};
    OrderQueue pending_orders_;
    
    // Statistics
    std::atomic<uint64_t> orders_sent_{0};
    std::atomic<uint64_t> orders_executed_{0};
    std::atomic<uint64_t> orders_rejected_{0};

public:
    EnhancedMarketMaker(Logger& logger, MatchingEngine& engine, 
                       Symbol symbol, const std::string& strategy_id)
        : logger_(logger)
        , engine_(engine)
        , target_symbol_(symbol)
        , strategy_id_(strategy_id)
        , spread_ticks_(4)
        , max_position_(1000)
        , admission_control_(AdaptiveAdmissionControl::Config{})
    {
        // Register strategy for P&L tracking
        pnl_manager_.register_strategy(strategy_id_);
    }
    
    void on_market_data_update(const MarketDataUpdate& update) {
        auto latency_measure = latency_tracker_.measure(LatencyPoint::MARKET_DATA_PROCESSING);
        
        if (update.symbol_id != target_symbol_) return;
        
        // Check admission control
        if (!admission_control_.allow_market_data_processing()) {
            return; // Rate limited
        }
        
        // Update market prices
        if (update.side == 0) { // Bid
            best_bid_.store(update.price, std::memory_order_relaxed);
        } else if (update.side == 1) { // Ask
            best_ask_.store(update.price, std::memory_order_relaxed);
        }
        
        // Update P&L with latest market prices
        Price mid_price = (best_bid_.load() + best_ask_.load()) / 2;
        pnl_manager_.update_market_price(target_symbol_, mid_price);
        
        // Generate new quotes
        update_quotes();
    }
    
    void on_execution_report(OrderId order_id, Price executed_price, Quantity executed_qty, Side side) {
        auto latency_measure = latency_tracker_.measure(LatencyPoint::ORDER_EXECUTION);
        
        orders_executed_.fetch_add(1, std::memory_order_relaxed);
        
        // Record execution for P&L tracking
        Price expected_price = (side == Side::BUY) ? best_ask_.load() : best_bid_.load();
        pnl_manager_.record_execution(strategy_id_, order_id, target_symbol_, 
                                     side, executed_qty, executed_price, expected_price);
        
        // Update position
        int64_t direction = (side == Side::BUY) ? 1 : -1;
        current_position_.fetch_add(direction * executed_qty, std::memory_order_relaxed);
        
        // Report latency for admission control
        uint64_t latency_ns = 50000; // Simulated execution latency
        admission_control_.report_latency(latency_ns);
    }
    
    void update_quotes() {
        if (!admission_control_.allow_order_submission()) {
            return; // Rate limited
        }
        
        auto latency_measure = latency_tracker_.measure(LatencyPoint::ORDER_SUBMISSION);
        
        Price bid = best_bid_.load();
        Price ask = best_ask_.load();
        
        if (bid == 0 || ask == 0) return;
        
        // Calculate our quote prices
        Price our_bid = bid + 1; // One tick better
        Price our_ask = ask - 1; // One tick better
        
        // Ensure minimum spread
        if (our_ask - our_bid < spread_ticks_) {
            Price mid = (our_bid + our_ask) / 2;
            our_bid = mid - spread_ticks_ / 2;
            our_ask = mid + spread_ticks_ / 2;
        }
        
        // Check position limits
        int64_t pos = current_position_.load(std::memory_order_relaxed);
        
        // Send bid order if not too long
        if (pos < max_position_) {
            submit_order(Side::BUY, our_bid, 10);
        }
        
        // Send ask order if not too short
        if (pos > -max_position_) {
            submit_order(Side::SELL, our_ask, 10);
        }
    }
    
    void submit_order(Side side, Price price, Quantity qty) {
        Order order;
        OrderId order_id = next_order_id_.fetch_add(1, std::memory_order_relaxed);
        order.init(order_id, target_symbol_, side, OrderType::LIMIT, price, qty);
        
        if (engine_.submit_order(std::move(order))) {
            orders_sent_.fetch_add(1, std::memory_order_relaxed);
        } else {
            orders_rejected_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void print_statistics() const {
        std::cout << "\n=== ENHANCED MARKET MAKER STATISTICS ===" << std::endl;
        std::cout << "Strategy: " << strategy_id_ << std::endl;
        std::cout << "Orders Sent: " << orders_sent_.load() << std::endl;
        std::cout << "Orders Executed: " << orders_executed_.load() << std::endl;
        std::cout << "Orders Rejected: " << orders_rejected_.load() << std::endl;
        std::cout << "Current Position: " << current_position_.load() << std::endl;
        std::cout << "Best Bid/Ask: " << best_bid_.load() << "/" << best_ask_.load() << std::endl;
        
        // Print latency metrics
        latency_tracker_.print_report();
        
        // Print admission control status
        auto ac_status = admission_control_.get_status();
        std::cout << "\n--- Admission Control ---" << std::endl;
        std::cout << "Order Tokens Available: " << ac_status.order_tokens_available 
                  << "/" << ac_status.order_capacity << std::endl;
        std::cout << "Recent Avg Latency: " << ac_status.recent_avg_latency_ns / 1000.0 << " μs" << std::endl;
        
        // Print P&L report
        auto* pnl_tracker = const_cast<PnLManager&>(pnl_manager_).get_strategy_tracker(strategy_id_);
        if (pnl_tracker) {
            pnl_tracker->print_detailed_report();
        }
    }
};

// FIX message processor example
class FixMessageProcessor {
private:
    FIXParser parser_;
    LatencyTracker& latency_tracker_;
    EnhancedMarketMaker& strategy_;

public:
    FixMessageProcessor(LatencyTracker& tracker, EnhancedMarketMaker& strategy) 
        : parser_(4, &tracker), latency_tracker_(tracker), strategy_(strategy) {}
    
    void process_fix_message(const std::string& fix_message) {
        // Submit for multithreaded parsing
        if (parser_.submit_for_parsing(fix_message.c_str(), fix_message.length())) {
            // Check for parsed messages
            ParsedFixMessage parsed_msg;
            if (parser_.get_parsed_message(parsed_msg)) {
                if (parsed_msg.msg_type == FixMessageType::NEW_ORDER_SINGLE) {
                    Order order = parser_.convert_to_order(parsed_msg);
                    // Process order...
                }
            }
        }
    }
    
    void print_parsing_stats() const {
        auto stats = parser_.get_stats();
        std::cout << "\n--- FIX Parser Statistics ---" << std::endl;
        std::cout << "Messages Pending: " << stats.messages_pending << std::endl;
        std::cout << "Messages Parsed: " << stats.messages_parsed << std::endl;
        std::cout << "Threads Running: " << (stats.threads_running ? "Yes" : "No") << std::endl;
    }
};

int main() {
    std::cout << "Starting Enhanced HFT Engine Demo..." << std::endl;
    
    // Initialize core components
    Logger logger;
    RiskManager risk_manager(1000, 10'000'000, 10000);
    MatchingEngine engine(risk_manager, logger, 1 << 20);
    
    // Initialize enhanced components
    LatencyTracker latency_tracker;
    
    // Create enhanced strategy
    EnhancedMarketMaker strategy(logger, engine, 1, "enhanced_mm_v1");
    
    // Create FIX processor
    FixMessageProcessor fix_processor(latency_tracker, strategy);
    
    // Create market simulator
    MarketSimulator::Config sim_config;
    sim_config.symbol_id = 1;
    sim_config.regime = MarketRegime::STABLE;
    sim_config.tick_interval_ns = 1000000; // 1ms ticks
    
    MarketSimulator market_sim(sim_config);
    
    // Create tick data replay harness and generate sample data
    TickDataReplayHarness replay_harness;
    
    std::cout << "Generating sample tick data..." << std::endl;
    if (!TickDataReplayHarness::generate_sample_csv("sample_ticks.csv", 1000)) {
        std::cerr << "Failed to generate sample data" << std::endl;
        return 1;
    }
    
    if (!replay_harness.load_data("sample_ticks.csv")) {
        std::cerr << "Failed to load tick data" << std::endl;
        return 1;
    }
    
    // Start market simulation
    std::cout << "Starting market simulation..." << std::endl;
    market_sim.start_simulation();
    
    // Start tick data replay in accelerated mode
    std::cout << "Starting tick data replay..." << std::endl;
    replay_harness.set_replay_mode(ReplayMode::ACCELERATED, 10.0); // 10x speed
    replay_harness.start_replay();
    
    // Run simulation for a few seconds
    std::atomic<bool> running{true};
    std::thread data_processor([&]() {
        MarketDataUpdate update;
        
        while (running.load()) {
            // Process market simulator updates
            if (market_sim.get_market_update(update)) {
                strategy.on_market_data_update(update);
            }
            
            // Process replay harness updates
            if (replay_harness.get_market_update(update)) {
                strategy.on_market_data_update(update);
            }
            
            std::this_thread::yield();
        }
    });
    
    // Simulate some FIX messages
    std::thread fix_simulator([&]() {
        std::vector<std::string> sample_fix_messages = {
            "8=FIX.4.4\x01" "9=165\x01" "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" 
            "34=1\x01" "52=20231215-10:30:00\x01" "11=ORDER1\x01" "55=SYMBOL1\x01" 
            "54=1\x01" "38=100\x01" "40=2\x01" "44=50.25\x01" "10=123\x01",
            
            "8=FIX.4.4\x01" "9=165\x01" "35=D\x01" "49=SENDER\x01" "56=TARGET\x01" 
            "34=2\x01" "52=20231215-10:30:01\x01" "11=ORDER2\x01" "55=SYMBOL1\x01" 
            "54=2\x01" "38=200\x01" "40=2\x01" "44=49.75\x01" "10=124\x01"
        };
        
        for (int i = 0; i < 100 && running.load(); ++i) {
            for (const auto& msg : sample_fix_messages) {
                fix_processor.process_fix_message(msg);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    });
    
    // Run for 10 seconds
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // Stop simulation
    running.store(false);
    market_sim.stop_simulation();
    replay_harness.stop_replay();
    
    if (data_processor.joinable()) data_processor.join();
    if (fix_simulator.joinable()) fix_simulator.join();
    
    // Print comprehensive statistics
    std::cout << "\n" << std::string(80, '=') << std::endl;
    std::cout << "FINAL COMPREHENSIVE REPORT" << std::endl;
    std::cout << std::string(80, '=') << std::endl;
    
    // Strategy statistics
    strategy.print_statistics();
    
    // FIX parser statistics
    fix_processor.print_parsing_stats();
    
    // Market simulator statistics
    auto market_state = market_sim.get_market_state();
    std::cout << "\n--- Market Simulator ---" << std::endl;
    std::cout << "Simulation Time: " << market_state.simulation_time_ns / 1e9 << " seconds" << std::endl;
    std::cout << "Final Bid/Ask: " << market_state.current_bid << "/" << market_state.current_ask << std::endl;
    std::cout << "Pending Updates: " << market_state.pending_updates << std::endl;
    
    // Replay harness statistics
    auto replay_stats = replay_harness.get_stats();
    std::cout << "\n--- Tick Data Replay ---" << std::endl;
    std::cout << "Total Ticks: " << replay_stats.total_ticks << std::endl;
    std::cout << "Ticks Processed: " << replay_stats.ticks_processed << std::endl;
    std::cout << "Ticks Skipped: " << replay_stats.ticks_skipped << std::endl;
    std::cout << "Progress: " << std::fixed << std::setprecision(1) 
              << replay_stats.progress_percent << "%" << std::endl;
    
    // Deep profiling report
    std::cout << "\n" << DeepProfiler::instance().generate_report() << std::endl;
    
    std::cout << "\nEnhanced HFT Engine Demo completed successfully!" << std::endl;
    
    return 0;
}