
#include "hft/network.h"
#include "hft/matching_engine.h"
#include <iostream>
#include <signal.h>
#include <unordered_map>
#include <iomanip>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <chrono>

// Global shutdown flag for graceful termination
std::atomic<bool> g_shutdown{false};

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    g_shutdown.store(true, std::memory_order_release);
}

// Order Book implementation for demonstration
class SimpleOrderBook {
public:
    struct PriceLevel {
        hft::Price price;
        hft::Quantity total_qty;
        uint32_t order_count;
        uint64_t last_update_ns;
    };
    
    struct BookSnapshot {
        std::vector<PriceLevel> bids;
        std::vector<PriceLevel> asks;
        uint64_t timestamp_ns;
        uint32_t symbol_id;
    };
    
    void update_level(uint32_t symbol_id, hft::Price price, hft::Quantity qty, 
                      hft::Side side, uint64_t timestamp_ns) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto& book = books_[symbol_id];
        auto& levels = (side == hft::Side::BUY) ? book.bids : book.asks;
        
        // Find or create price level
        auto it = std::find_if(levels.begin(), levels.end(),
            [price](const PriceLevel& level) { return level.price == price; });
        
        if (qty == 0) {
            // Remove level
            if (it != levels.end()) {
                levels.erase(it);
            }
        } else {
            // Update or add level
            if (it != levels.end()) {
                it->total_qty = qty;
                it->last_update_ns = timestamp_ns;
            } else {
                levels.push_back({price, qty, 1, timestamp_ns});
            }
        }
        
        // Keep levels sorted (bids descending, asks ascending)
        if (side == hft::Side::BUY) {
            std::sort(levels.begin(), levels.end(),
                [](const PriceLevel& a, const PriceLevel& b) { return a.price > b.price; });
        } else {
            std::sort(levels.begin(), levels.end(),
                [](const PriceLevel& a, const PriceLevel& b) { return a.price < b.price; });
        }
        
        book.last_update_ns = timestamp_ns;
        
        // Trigger callbacks if needed
        if (update_callback_) {
            update_callback_(symbol_id, get_snapshot(symbol_id));
        }
    }
    
    BookSnapshot get_snapshot(uint32_t symbol_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        BookSnapshot snapshot;
        
        auto it = books_.find(symbol_id);
        if (it != books_.end()) {
            snapshot.bids = it->second.bids;
            snapshot.asks = it->second.asks;
            snapshot.timestamp_ns = it->second.last_update_ns;
        }
        
        snapshot.symbol_id = symbol_id;
        return snapshot;
    }
    
    void set_update_callback(std::function<void(uint32_t, const BookSnapshot&)> cb) {
        update_callback_ = std::move(cb);
    }
    
    void print_book(uint32_t symbol_id, int depth = 5) const {
        auto snapshot = get_snapshot(symbol_id);
        
        std::cout << "\n=== Order Book for Symbol " << symbol_id << " ===" << std::endl;
        std::cout << "Timestamp: " << snapshot.timestamp_ns << std::endl;
        
        std::cout << "\nAsks (Sell Orders):" << std::endl;
        for (int i = std::min(depth, (int)snapshot.asks.size()) - 1; i >= 0; --i) {
            const auto& level = snapshot.asks[i];
            std::cout << std::fixed << std::setprecision(4)
                     << "  " << level.price / 10000.0 
                     << " x " << level.total_qty 
                     << " (" << level.order_count << " orders)" << std::endl;
        }
        
        std::cout << "  ---------- SPREAD ----------" << std::endl;
        
        std::cout << "\nBids (Buy Orders):" << std::endl;
        for (int i = 0; i < std::min(depth, (int)snapshot.bids.size()); ++i) {
            const auto& level = snapshot.bids[i];
            std::cout << std::fixed << std::setprecision(4)
                     << "  " << level.price / 10000.0 
                     << " x " << level.total_qty 
                     << " (" << level.order_count << " orders)" << std::endl;
        }
        std::cout << std::endl;
    }

private:
    struct Book {
        std::vector<PriceLevel> bids;
        std::vector<PriceLevel> asks;
        uint64_t last_update_ns = 0;
    };
    
    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, Book> books_;
    std::function<void(uint32_t, const BookSnapshot&)> update_callback_;
};

// Performance monitoring and statistics
class PerformanceMonitor {
public:
    struct Stats {
        std::atomic<uint64_t> messages_received{0};
        std::atomic<uint64_t> orders_processed{0};
        std::atomic<uint64_t> l2_updates_processed{0};
        std::atomic<uint64_t> orders_sent{0};
        std::atomic<uint64_t> execution_reports{0};
        std::atomic<uint64_t> latency_sum_ns{0};
        std::atomic<uint64_t> latency_count{0};
        std::atomic<uint64_t> max_latency_ns{0};
        std::atomic<uint64_t> min_latency_ns{UINT64_MAX};
    };
    
    PerformanceMonitor() : start_time_(hft::net::TimeUtils::now_ns()) {
        // Start monitoring thread
        monitoring_thread_ = std::thread([this]() { this->monitor_loop(); });
    }
    
    ~PerformanceMonitor() {
        running_.store(false, std::memory_order_release);
        if (monitoring_thread_.joinable()) {
            monitoring_thread_.join();
        }
    }
    
    void record_message_received() {
        stats_.messages_received.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_order_processed() {
        stats_.orders_processed.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_l2_update() {
        stats_.l2_updates_processed.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_order_sent() {
        stats_.orders_sent.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_execution_report() {
        stats_.execution_reports.fetch_add(1, std::memory_order_relaxed);
    }
    
    void record_latency(uint64_t latency_ns) {
        stats_.latency_sum_ns.fetch_add(latency_ns, std::memory_order_relaxed);
        stats_.latency_count.fetch_add(1, std::memory_order_relaxed);
        
        // Update min/max latency
        uint64_t current_max = stats_.max_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns > current_max && 
               !stats_.max_latency_ns.compare_exchange_weak(current_max, latency_ns, 
                                                           std::memory_order_relaxed)) {}
        
        uint64_t current_min = stats_.min_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns < current_min && 
               !stats_.min_latency_ns.compare_exchange_weak(current_min, latency_ns, 
                                                           std::memory_order_relaxed)) {}
    }
    
    void print_stats() const {
        uint64_t now = hft::net::TimeUtils::now_ns();
        uint64_t elapsed_s = (now - start_time_) / 1000000000ULL;
        if (elapsed_s == 0) elapsed_s = 1;
        
        uint64_t messages = stats_.messages_received.load(std::memory_order_relaxed);
        uint64_t orders = stats_.orders_processed.load(std::memory_order_relaxed);
        uint64_t l2_updates = stats_.l2_updates_processed.load(std::memory_order_relaxed);
        uint64_t orders_sent = stats_.orders_sent.load(std::memory_order_relaxed);
        uint64_t executions = stats_.execution_reports.load(std::memory_order_relaxed);
        
        uint64_t latency_sum = stats_.latency_sum_ns.load(std::memory_order_relaxed);
        uint64_t latency_count = stats_.latency_count.load(std::memory_order_relaxed);
        uint64_t max_latency = stats_.max_latency_ns.load(std::memory_order_relaxed);
        uint64_t min_latency = stats_.min_latency_ns.load(std::memory_order_relaxed);
        
        std::cout << "\n=== Performance Statistics ===" << std::endl;
        std::cout << "Runtime: " << elapsed_s << " seconds" << std::endl;
        std::cout << "Messages Received: " << messages << " (" 
                  << messages / elapsed_s << "/sec)" << std::endl;
        std::cout << "Orders Processed: " << orders << " (" 
                  << orders / elapsed_s << "/sec)" << std::endl;
        std::cout << "L2 Updates: " << l2_updates << " (" 
                  << l2_updates / elapsed_s << "/sec)" << std::endl;
        std::cout << "Orders Sent: " << orders_sent << " (" 
                  << orders_sent / elapsed_s << "/sec)" << std::endl;
        std::cout << "Execution Reports: " << executions << std::endl;
        
        if (latency_count > 0) {
            uint64_t avg_latency = latency_sum / latency_count;
            std::cout << "Average Latency: " << avg_latency / 1000.0 << " μs" << std::endl;
            if (max_latency < UINT64_MAX) {
                std::cout << "Max Latency: " << max_latency / 1000.0 << " μs" << std::endl;
            }
            if (min_latency < UINT64_MAX) {
                std::cout << "Min Latency: " << min_latency / 1000.0 << " μs" << std::endl;
            }
        }
        std::cout << "==============================\n" << std::endl;
    }

private:
    Stats stats_;
    uint64_t start_time_;
    std::atomic<bool> running_{true};
    std::thread monitoring_thread_;
    
    void monitor_loop() {
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (running_.load(std::memory_order_relaxed)) {
                print_stats();
            }
        }
    }
};

// Trading Strategy Interface
class ITradingStrategy {
public:
    virtual ~ITradingStrategy() = default;
    virtual void on_market_data(uint32_t symbol_id, const hft::net::L2Update& update) = 0;
    virtual void on_order_book_update(uint32_t symbol_id, const SimpleOrderBook::BookSnapshot& book) = 0;
    virtual void on_execution_report(const std::string& execution_report) = 0;
};

// Simple Market Making Strategy Example
class SimpleMarketMaker : public ITradingStrategy {
public:
    struct Config {
        uint32_t target_symbol;
        hft::Price spread_ticks = 10; // Minimum spread in price ticks
        hft::Quantity max_position = 1000;
        hft::Quantity order_size = 100;
        bool enabled = true;
    };
    
    SimpleMarketMaker(Config config, hft::net::OrderEntryGateway* gateway, 
                      PerformanceMonitor* monitor)
        : config_(config), gateway_(gateway), monitor_(monitor), 
          current_position_(0), bid_order_id_(0), ask_order_id_(0) {}
    
    void on_market_data(uint32_t symbol_id, const hft::net::L2Update& update) override {
        if (!config_.enabled || symbol_id != config_.target_symbol) return;
        
        monitor_->record_l2_update();
        
        // Simple market making logic based on top of book
        if (update.level_count > 0) {
            last_market_update_ = hft::net::TimeUtils::now_ns();
            
            // Extract best bid/ask from the update
        hft::Price best_bid = 0, best_ask = 0;
        for (uint32_t i = 0; i < update.level_count; ++i) {
                if (update.levels[i].side == hft::Side::BUY) {
                    if (best_bid == 0 || update.levels[i].price > best_bid) {
                        best_bid = update.levels[i].price;
                    }
                } else {
                    if (best_ask == 0 || update.levels[i].price < best_ask) {
                        best_ask = update.levels[i].price;
                    }
                }
            }
            
            if (best_bid > 0 && best_ask > 0) {
                update_quotes(best_bid, best_ask);
            }
        }
    }
    
    void on_order_book_update(uint32_t symbol_id, const SimpleOrderBook::BookSnapshot& book) override {
        if (!config_.enabled || symbol_id != config_.target_symbol) return;
        
        // Could use full book information for more sophisticated strategies
        if (!book.bids.empty() && !book.asks.empty()) {
            hft::Price best_bid = book.bids[0].price;
            hft::Price best_ask = book.asks[0].price;
            update_quotes(best_bid, best_ask);
        }
    }
    
    void on_execution_report(const std::string& execution_report) override {
        monitor_->record_execution_report();
        
        // Parse execution report to update position and outstanding orders
        // This is a simplified version - production code would have proper FIX parsing
        if (execution_report.find("39=2") != std::string::npos) { // Filled
            // Update position based on execution
            std::cout << "Order filled: " << execution_report << std::endl;
        }
    }
    
    void set_enabled(bool enabled) {
        config_.enabled = enabled;
        if (!enabled) {
            // Cancel all outstanding orders
            cancel_all_orders();
        }
    }

private:
    Config config_;
    hft::net::OrderEntryGateway* gateway_;
    PerformanceMonitor* monitor_;
    
    std::atomic<int64_t> current_position_;
    std::atomic<uint64_t> bid_order_id_;
    std::atomic<uint64_t> ask_order_id_;
    uint64_t last_market_update_ = 0;
    
    void update_quotes(hft::Price best_bid, hft::Price best_ask) {
        uint64_t now = hft::net::TimeUtils::now_ns();
        
        // Calculate our quote prices
        hft::Price our_bid = best_bid + 1; // One tick inside
        hft::Price our_ask = best_ask - 1; // One tick inside
        
        // Ensure minimum spread
        if (our_ask - our_bid < config_.spread_ticks) {
            hft::Price mid = (our_bid + our_ask) / 2;
            our_bid = mid - config_.spread_ticks / 2;
            our_ask = mid + config_.spread_ticks / 2;
        }
        
        // Check position limits
        int64_t pos = current_position_.load(std::memory_order_relaxed);
        
        // Send bid order if we're not too long
        if (pos < static_cast<int64_t>(config_.max_position)) {
            hft::Order bid_order{};
            bid_order.symbol = static_cast<hft::Symbol>(config_.target_symbol);
            bid_order.side = hft::Side::BUY;
            bid_order.type = hft::OrderType::LIMIT;
            bid_order.tif = hft::TimeInForce::GTC;
            bid_order.price = our_bid;
            bid_order.qty = config_.order_size;
            bid_order.user_id = 1; // Market maker ID
            
            if (gateway_->send_new_order(bid_order)) {
                monitor_->record_order_sent();
                uint64_t latency = hft::net::TimeUtils::now_ns() - last_market_update_;
                monitor_->record_latency(latency);
            }
        }
        
        // Send ask order if we're not too short
        if (pos > -static_cast<int64_t>(config_.max_position)) {
            hft::Order ask_order{};
            ask_order.symbol = static_cast<hft::Symbol>(config_.target_symbol);
            ask_order.side = hft::Side::SELL;
            ask_order.type = hft::OrderType::LIMIT;
            ask_order.tif = hft::TimeInForce::GTC;
            ask_order.price = our_ask;
            ask_order.qty = config_.order_size;
            ask_order.user_id = 1; // Market maker ID
            
            if (gateway_->send_new_order(ask_order)) {
                monitor_->record_order_sent();
                uint64_t latency = hft::net::TimeUtils::now_ns() - last_market_update_;
                monitor_->record_latency(latency);
            }
        }
    }
    
    void cancel_all_orders() {
        // Implementation would send cancel requests for outstanding orders
        std::cout << "Cancelling all orders for market maker" << std::endl;
    }
};

// Main application class
class TradingApplication {
public:
    TradingApplication() {
        // Initialize performance monitor
        monitor_ = std::make_unique<PerformanceMonitor>();
        
        // Initialize order book
        order_book_ = std::make_unique<SimpleOrderBook>();
        order_book_->set_update_callback([this](uint32_t symbol_id, const SimpleOrderBook::BookSnapshot& book) {
            if (strategy_) {
                strategy_->on_order_book_update(symbol_id, book);
            }
        });
        
        // Setup signal handlers
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
    }
    
    ~TradingApplication() {
        shutdown();
    }
    
    bool initialize() {
        std::cout << "Initializing Ultra-Low Latency Trading System..." << std::endl;
        
        // Configure market data manager
        hft::net::MarketDataManager::ManagerConfig manager_config;
        
        // Add multiple market data feeds
        hft::net::MarketDataFeed::FeedConfig nasdaq_feed;
        nasdaq_feed.name = "NASDAQ_ITCH";
        nasdaq_feed.type = hft::net::FeedType::ITCH_50;
        nasdaq_feed.multicast_addr = "233.54.12.111";
        nasdaq_feed.port = 26477;
        nasdaq_feed.interface = "eth0"; // Adjust for your system
        nasdaq_feed.cpu_affinity = 2;   // Dedicate CPU core 2 for NASDAQ feed
        nasdaq_feed.realtime_priority = true;
        manager_config.feeds.push_back(nasdaq_feed);
        
        // Add FIX feed for another exchange
        hft::net::MarketDataFeed::FeedConfig fix_feed;
        fix_feed.name = "EXCHANGE_FIX";
        fix_feed.type = hft::net::FeedType::FIX_44;
        fix_feed.multicast_addr = "239.192.1.1";
        fix_feed.port = 9001;
        fix_feed.interface = "eth0";
        fix_feed.cpu_affinity = 3;   // Dedicate CPU core 3 for FIX feed
        fix_feed.realtime_priority = true;
        manager_config.feeds.push_back(fix_feed);
        
        // Configure order entry gateway
        manager_config.gateway.host = "192.168.1.100"; // Exchange gateway IP
        manager_config.gateway.port = 7001;
        manager_config.gateway.cpu_affinity = 4; // Dedicate CPU core 4 for order entry
        manager_config.gateway.realtime_priority = true;
        manager_config.gateway.send_buffer_size = 64 * 1024;
        manager_config.gateway.recv_buffer_size = 64 * 1024;
        
        manager_config.processing_threads = 2;
        
        // Initialize market data manager
        data_manager_ = std::make_unique<hft::net::MarketDataManager>();
        data_manager_->set_orderbook_callback([this](uint32_t symbol_id, const hft::net::L2Update& update) {
            this->on_market_data_update(symbol_id, update);
        });
        
        if (!data_manager_->start(manager_config)) {
            std::cerr << "Failed to start market data manager" << std::endl;
            return false;
        }
        
        std::cout << "Market data feeds started successfully" << std::endl;
        
        // Initialize trading strategy
        SimpleMarketMaker::Config strategy_config;
        strategy_config.target_symbol = 12345; // Example symbol ID
        strategy_config.spread_ticks = 5;
        strategy_config.max_position = 500;
        strategy_config.order_size = 100;
        strategy_config.enabled = true;
        
        // Note: In production, you'd get gateway reference from data_manager_
        // For this example, we'll create a separate gateway connection
        gateway_ = std::make_unique<hft::net::OrderEntryGateway>();
        hft::net::OrderEntryGateway::GatewayConfig gw_config;
        gw_config.host = "192.168.1.100";
        gw_config.port = 7001;
        gw_config.cpu_affinity = 5;
        gw_config.realtime_priority = true;
        
        if (!gateway_->connect(gw_config)) {
            std::cerr << "Failed to connect to order entry gateway" << std::endl;
            return false;
        }
        
        gateway_->set_execution_callback([this](const std::string& report) {
            if (strategy_) {
                strategy_->on_execution_report(report);
            }
        });
        
        strategy_ = std::make_unique<SimpleMarketMaker>(strategy_config, gateway_.get(), monitor_.get());
        
        std::cout << "Trading strategy initialized" << std::endl;
        
        return true;
    }
    
    void run() {
        std::cout << "Trading system running. Press Ctrl+C to shutdown gracefully." << std::endl;
        std::cout << "Commands: 'stats' - show statistics, 'book <symbol_id>' - show order book" << std::endl;
        
        // Command processing loop
        std::string command;
        while (!g_shutdown.load(std::memory_order_relaxed)) {
            std::cout << "> ";
            std::getline(std::cin, command);
            
            if (command == "stats") {
                monitor_->print_stats();
            } else if (command.substr(0, 4) == "book") {
                uint32_t symbol_id = 12345; // Default symbol
                if (command.length() > 5) {
                    try {
                        symbol_id = std::stoul(command.substr(5));
                    } catch (...) {
                        std::cout << "Invalid symbol ID" << std::endl;
                        continue;
                    }
                }
                order_book_->print_book(symbol_id);
            } else if (command == "quit" || command == "exit") {
                break;
            } else if (command == "help") {
                std::cout << "Available commands:" << std::endl;
                std::cout << "  stats - Show performance statistics" << std::endl;
                std::cout << "  book [symbol_id] - Show order book for symbol" << std::endl;
                std::cout << "  quit/exit - Shutdown application" << std::endl;
            } else if (!command.empty()) {
                std::cout << "Unknown command: " << command << std::endl;
            }
        }
    }
    
    void shutdown() {
        std::cout << "Shutting down trading system..." << std::endl;
        
        // Strategy cleanup would go here
        
        if (data_manager_) {
            data_manager_->stop();
        }
        
        if (gateway_) {
            gateway_->disconnect();
        }
        
        // Final statistics
        if (monitor_) {
            monitor_->print_stats();
        }
        
        std::cout << "Shutdown complete." << std::endl;
    }

private:
    std::unique_ptr<PerformanceMonitor> monitor_;
    std::unique_ptr<SimpleOrderBook> order_book_;
    std::unique_ptr<hft::net::MarketDataManager> data_manager_;
    std::unique_ptr<hft::net::OrderEntryGateway> gateway_;
    std::unique_ptr<ITradingStrategy> strategy_;
    
    void on_market_data_update(uint32_t symbol_id, const hft::net::L2Update& update) {
        monitor_->record_message_received();
        
        // Update local order book
        for (uint32_t i = 0; i < update.level_count; ++i) {
            const auto& level = update.levels[i];
            order_book_->update_level(symbol_id, level.price, level.qty, 
                                    level.side, update.timestamp_ns);
        }
        
        // Forward to strategy
        if (strategy_) {
            strategy_->on_market_data(symbol_id, update);
        }
    }
};

// Application entry point with comprehensive error handling
int main(int argc, char* argv[]) {
    try {
        std::cout << "Ultra-Low Latency HFT Trading System v1.0" << std::endl;
        std::cout << "===========================================" << std::endl;
        
        // Check system capabilities
        std::cout << "System Information:" << std::endl;
        std::cout << "  CPU Cores: " << std::thread::hardware_concurrency() << std::endl;
        std::cout << "  Cache Line Size: " << hft::CACHE_LINE_SIZE << " bytes" << std::endl;
        
#ifdef __linux__
        std::cout << "  Platform: Linux (optimized)" << std::endl;
#elif defined(__APPLE__)
        std::cout << "  Platform: macOS" << std::endl;
#elif defined(_WIN32)
        std::cout << "  Platform: Windows" << std::endl;
#endif
        
        // Initialize and run application
        TradingApplication app;
        
        if (!app.initialize()) {
            std::cerr << "Failed to initialize trading application" << std::endl;
            return 1;
        }
        
        app.run();
        app.shutdown();
        
        std::cout << "Application terminated successfully" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}

// Additional utility functions for production deployment
namespace hft {
namespace utils {

// System optimization utilities
class SystemOptimizer {
public:
    // Set process to high priority and lock memory pages
    static bool optimize_process() {
#ifdef __linux__
        // Set real-time scheduling
        struct sched_param param;
        param.sched_priority = 99;
        if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
            std::cerr << "Warning: Could not set real-time priority" << std::endl;
        }
        
        // Lock all current and future pages in memory
        if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
            std::cerr << "Warning: Could not lock memory pages" << std::endl;
        }
        
        return true;
#elif defined(_WIN32)
        // Set high priority class
        if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
            std::cerr << "Warning: Could not set high priority" << std::endl;
        }
        return true;
#else
        return true;
#endif
    }
    
    // Configure network interface for low latency
    static bool optimize_network_interface(const std::string& interface) {
#ifdef __linux__
        // These would typically be set via ethtool or similar utilities
        std::cout << "Network optimization for " << interface << " should be done via:" << std::endl;
        std::cout << "  ethtool -C " << interface << " rx-usecs 0" << std::endl;
        std::cout << "  ethtool -C " << interface << " tx-usecs 0" << std::endl;
        std::cout << "  ethtool -K " << interface << " gro off" << std::endl;
        std::cout << "  ethtool -K " << interface << " lro off" << std::endl;
        return true;
#else
        return true;
#endif
    }
};

// Configuration management
class ConfigManager {
public:
    struct SystemConfig {
        std::string market_data_interface = "eth0";
        std::string order_entry_host = "192.168.1.100";
        uint16_t order_entry_port = 7001;
        bool enable_kernel_bypass = false;
        bool enable_cpu_isolation = true;
        std::vector<int> isolated_cpus = {2, 3, 4, 5};
    };
    
    static SystemConfig load_config(const std::string& config_file) {
        // In production, this would parse JSON/XML/YAML configuration
        SystemConfig config;
        
        // For now, return default configuration
        std::cout << "Using default configuration (implement config file parsing for production)" << std::endl;
        
        return config;
    }
};

} // namespace utils
} // namespace hft