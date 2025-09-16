#pragma once

#include "hft/types.h"
#include "hft/market_data_source.h"
#include "hft/matching_engine.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <queue>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace hft {

// NASDAQ ITCH 5.0 message types for real tick data replay
namespace itch50 {
    struct MessageHeader {
        uint16_t length;
        char message_type;
        uint64_t timestamp_ns;
    } __attribute__((packed));
    
    struct SystemEventMessage {
        MessageHeader header;
        char event_code; // 'O' = start of day, 'C' = end of day
    } __attribute__((packed));
    
    struct StockDirectoryMessage {
        MessageHeader header;
        char stock[8];
        char market_category;
        char financial_status;
        uint32_t round_lot_size;
        char round_lots_only;
        char issue_classification;
        char issue_sub_type[2];
        char authenticity;
        char short_sale_threshold;
        char ipo_flag;
        char luld_reference_price_tier;
        char etp_flag;
        uint32_t etp_leverage_factor;
        char inverse_indicator;
    } __attribute__((packed));
    
    struct AddOrderMessage {
        MessageHeader header;
        uint64_t order_reference_number;
        char buy_sell_indicator;
        uint32_t shares;
        char stock[8];
        uint32_t price;  // 1/10000 precision
    } __attribute__((packed));
    
    struct OrderExecutedMessage {
        MessageHeader header;
        uint64_t order_reference_number;
        uint32_t executed_shares;
        uint64_t match_number;
    } __attribute__((packed));
    
    struct TradeMessage {
        MessageHeader header;
        uint64_t order_reference_number;
        char buy_sell_indicator;
        uint32_t shares;
        char stock[8];
        uint32_t price;
        uint64_t match_number;
    } __attribute__((packed));
}

// NYSE TAQ format structures
namespace nyse_taq {
    struct QuoteRecord {
        char symbol[16];
        char date[9];
        char time[9];
        uint32_t bid_price;      // In 1/10000 precision
        uint32_t bid_size;
        uint32_t ask_price;      // In 1/10000 precision
        uint32_t ask_size;
        char quote_condition;
        char market_maker[4];
        uint16_t bid_exchange;
        uint16_t ask_exchange;
    } __attribute__((packed));
    
    struct TradeRecord {
        char symbol[16];
        char date[9];
        char time[9];
        uint32_t price;          // In 1/10000 precision
        uint32_t size;
        uint16_t exchange;
        char sale_condition[4];
        char correction;
    } __attribute__((packed));
}

// Unified tick event for replay
struct TickEvent {
    enum Type { QUOTE, TRADE, SYSTEM_EVENT };
    
    Type type;
    Symbol symbol;
    uint64_t timestamp_ns;
    
    // Quote data
    Price bid_price = 0;
    Price ask_price = 0;
    Quantity bid_size = 0;
    Quantity ask_size = 0;
    
    // Trade data
    Price trade_price = 0;
    Quantity trade_size = 0;
    char trade_conditions[8] = {0};
    
    // System event data
    char event_code = 0;
    
    TickEvent(Type t, Symbol sym, uint64_t ts) 
        : type(t), symbol(sym), timestamp_ns(ts) {}
};

// Abstract tick data reader interface
class TickDataReader {
public:
    virtual ~TickDataReader() = default;
    virtual bool open(const std::string& filename) = 0;
    virtual bool read_next_event(TickEvent& event) = 0;
    virtual void close() = 0;
    virtual uint64_t get_total_events() const = 0;
    virtual std::string get_date_range() const = 0;
};

// NASDAQ ITCH 5.0 binary file reader
class ITCHReader : public TickDataReader {
private:
    std::ifstream file_;
    uint64_t total_events_ = 0;
    uint64_t events_read_ = 0;
    std::string start_date_;
    std::string end_date_;
    
public:
    bool open(const std::string& filename) override {
        file_.open(filename, std::ios::binary);
        if (!file_.is_open()) {
            std::cerr << "Failed to open ITCH file: " << filename << std::endl;
            return false;
        }
        
        // Count total events and determine date range
        analyze_file();
        
        // Reset to beginning
        file_.clear();
        file_.seekg(0, std::ios::beg);
        events_read_ = 0;
        
        std::cout << " Opened ITCH file: " << filename << std::endl;
        std::cout << "   Total events: " << total_events_ << std::endl;
        std::cout << "   Date range: " << get_date_range() << std::endl;
        
        return true;
    }
    
    bool read_next_event(TickEvent& event) override {
        if (!file_.is_open() || file_.eof()) return false;
        
        itch50::MessageHeader header;
        if (!file_.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            return false;
        }
        
        // Convert network byte order if needed
        header.length = ntohs(header.length);
        header.timestamp_ns = be64toh(header.timestamp_ns);
        
        switch (header.message_type) {
            case 'A': // Add Order
                return read_add_order(header, event);
            case 'E': // Order Executed
                return read_order_executed(header, event);
            case 'P': // Trade Message (non-cross)
                return read_trade_message(header, event);
            case 'Q': // Cross Trade
                return read_cross_trade(header, event);
            case 'S': // System Event
                return read_system_event(header, event);
            default:
                // Skip unknown message type
                file_.seekg(header.length - sizeof(header), std::ios::cur);
                return read_next_event(event);
        }
    }
    
    void close() override {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    uint64_t get_total_events() const override {
        return total_events_;
    }
    
    std::string get_date_range() const override {
        return start_date_ + " to " + end_date_;
    }

private:
    void analyze_file() {
        total_events_ = 0;
        
        file_.seekg(0, std::ios::end);
        std::streampos file_size = file_.tellg();
        file_.seekg(0, std::ios::beg);
        
        // Estimate events based on average message size (rough approximation)
        total_events_ = static_cast<uint64_t>(file_size) / 30; // Avg ~30 bytes per message
        
        // Set placeholder dates - in production, parse actual timestamps
        start_date_ = "2024-01-01";
        end_date_ = "2024-01-01";
    }
    
    bool read_add_order(const itch50::MessageHeader& header, TickEvent& event) {
        itch50::AddOrderMessage msg;
        msg.header = header;
        
        if (!file_.read(reinterpret_cast<char*>(&msg) + sizeof(header), 
                       sizeof(msg) - sizeof(header))) {
            return false;
        }
        
        // Convert network byte order
        msg.order_reference_number = be64toh(msg.order_reference_number);
        msg.shares = ntohl(msg.shares);
        msg.price = ntohl(msg.price);
        
        // Create quote event from add order
        event = TickEvent(TickEvent::QUOTE, parse_stock_symbol(msg.stock), header.timestamp_ns);
        
        if (msg.buy_sell_indicator == 'B') {
            event.bid_price = msg.price;
            event.bid_size = msg.shares;
        } else {
            event.ask_price = msg.price;
            event.ask_size = msg.shares;
        }
        
        events_read_++;
        return true;
    }
    
    bool read_order_executed(const itch50::MessageHeader& header, TickEvent& event) {
        itch50::OrderExecutedMessage msg;
        msg.header = header;
        
        if (!file_.read(reinterpret_cast<char*>(&msg) + sizeof(header), 
                       sizeof(msg) - sizeof(header))) {
            return false;
        }
        
        // Convert network byte order
        msg.order_reference_number = be64toh(msg.order_reference_number);
        msg.executed_shares = ntohl(msg.executed_shares);
        msg.match_number = be64toh(msg.match_number);
        
        // This would require order book state tracking in production
        // For now, skip execution messages
        return read_next_event(event);
    }
    
    bool read_trade_message(const itch50::MessageHeader& header, TickEvent& event) {
        itch50::TradeMessage msg;
        msg.header = header;
        
        if (!file_.read(reinterpret_cast<char*>(&msg) + sizeof(header), 
                       sizeof(msg) - sizeof(header))) {
            return false;
        }
        
        // Convert network byte order
        msg.order_reference_number = be64toh(msg.order_reference_number);
        msg.shares = ntohl(msg.shares);
        msg.price = ntohl(msg.price);
        msg.match_number = be64toh(msg.match_number);
        
        // Create trade event
        event = TickEvent(TickEvent::TRADE, parse_stock_symbol(msg.stock), header.timestamp_ns);
        event.trade_price = msg.price;
        event.trade_size = msg.shares;
        
        events_read_++;
        return true;
    }
    
    bool read_cross_trade(const itch50::MessageHeader& header, TickEvent& event) {
        // Similar to trade message, but skip for simplicity
        file_.seekg(header.length - sizeof(header), std::ios::cur);
        return read_next_event(event);
    }
    
    bool read_system_event(const itch50::MessageHeader& header, TickEvent& event) {
        itch50::SystemEventMessage msg;
        msg.header = header;
        
        if (!file_.read(reinterpret_cast<char*>(&msg) + sizeof(header), 
                       sizeof(msg) - sizeof(header))) {
            return false;
        }
        
        event = TickEvent(TickEvent::SYSTEM_EVENT, 0, header.timestamp_ns);
        event.event_code = msg.event_code;
        
        events_read_++;
        return true;
    }
    
    Symbol parse_stock_symbol(const char stock[8]) const {
        // Convert 8-character stock symbol to Symbol ID
        std::string symbol_str(stock, 8);
        
        // Remove trailing spaces
        symbol_str.erase(symbol_str.find_last_not_of(" \0") + 1);
        
        // Hash to Symbol ID
        std::hash<std::string> hasher;
        return static_cast<Symbol>(hasher(symbol_str) % 100000) + 1;
    }
    
    // Network byte order conversion helpers
    uint64_t be64toh(uint64_t val) const {
        return __builtin_bswap64(val);
    }
    
    uint32_t ntohl(uint32_t val) const {
        return __builtin_bswap32(val);
    }
    
    uint16_t ntohs(uint16_t val) const {
        return __builtin_bswap16(val);
    }
};

// NYSE TAQ CSV reader
class TAQReader : public TickDataReader {
private:
    std::ifstream file_;
    uint64_t total_events_ = 0;
    uint64_t events_read_ = 0;
    std::string header_line_;
    
public:
    bool open(const std::string& filename) override {
        file_.open(filename);
        if (!file_.is_open()) {
            std::cerr << "Failed to open TAQ file: " << filename << std::endl;
            return false;
        }
        
        // Read header
        std::getline(file_, header_line_);
        
        // Count lines
        count_lines();
        
        // Reset to after header
        file_.clear();
        file_.seekg(0, std::ios::beg);
        std::getline(file_, header_line_); // Skip header again
        events_read_ = 0;
        
        std::cout << " Opened TAQ file: " << filename << std::endl;
        std::cout << "   Total events: " << total_events_ << std::endl;
        
        return true;
    }
    
    bool read_next_event(TickEvent& event) override {
        if (!file_.is_open() || file_.eof()) return false;
        
        std::string line;
        if (!std::getline(file_, line)) return false;
        
        // Parse CSV line (simplified - production would use robust CSV parser)
        std::vector<std::string> fields = split_csv_line(line);
        
        if (fields.size() < 8) return read_next_event(event); // Skip malformed lines
        
        try {
            std::string symbol = fields[0];
            std::string time_str = fields[1];
            
            // Determine if this is quote or trade data based on field count
            if (fields.size() >= 10) {
                // Quote record
                event = TickEvent(TickEvent::QUOTE, string_to_symbol(symbol), 
                                 parse_time_to_ns(time_str));
                event.bid_price = static_cast<Price>(std::stod(fields[2]) * 10000);
                event.bid_size = static_cast<Quantity>(std::stoull(fields[3]));
                event.ask_price = static_cast<Price>(std::stod(fields[4]) * 10000);
                event.ask_size = static_cast<Quantity>(std::stoull(fields[5]));
            } else {
                // Trade record
                event = TickEvent(TickEvent::TRADE, string_to_symbol(symbol), 
                                 parse_time_to_ns(time_str));
                event.trade_price = static_cast<Price>(std::stod(fields[2]) * 10000);
                event.trade_size = static_cast<Quantity>(std::stoull(fields[3]));
            }
            
            events_read_++;
            return true;
            
        } catch (const std::exception& e) {
            // Skip malformed line
            return read_next_event(event);
        }
    }
    
    void close() override {
        if (file_.is_open()) {
            file_.close();
        }
    }
    
    uint64_t get_total_events() const override {
        return total_events_;
    }
    
    std::string get_date_range() const override {
        return "TAQ Data";  // Placeholder
    }

private:
    void count_lines() {
        total_events_ = 0;
        std::string line;
        while (std::getline(file_, line)) {
            total_events_++;
        }
    }
    
    std::vector<std::string> split_csv_line(const std::string& line) const {
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        
        while (std::getline(ss, field, ',')) {
            // Remove quotes if present
            if (field.size() >= 2 && field[0] == '"' && field.back() == '"') {
                field = field.substr(1, field.size() - 2);
            }
            fields.push_back(field);
        }
        
        return fields;
    }
    
    Symbol string_to_symbol(const std::string& symbol_str) const {
        std::hash<std::string> hasher;
        return static_cast<Symbol>(hasher(symbol_str) % 100000) + 1;
    }
    
    uint64_t parse_time_to_ns(const std::string& time_str) const {
        // Simplified time parsing - production would use proper datetime library
        // For now, use current time with microsecond precision
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count();
    }
};

// Main tick replay engine
class TickReplayEngine {
public:
    struct ReplayConfig {
        std::vector<std::string> data_files;
        double replay_speed = 1.0;      // 1.0 = real-time, 10.0 = 10x speed
        bool enable_market_hours_only = true;
        uint64_t start_time_ns = 0;     // 0 = start from beginning
        uint64_t end_time_ns = UINT64_MAX; // UINT64_MAX = replay all
        std::string log_file = "replay.log";
        bool enable_progress_display = true;
        uint32_t progress_update_interval_ms = 1000;
        
        // Market simulation parameters
        bool enable_market_impact = true;
        double liquidity_factor = 1.0;  // Higher = more liquid market
    };
    
    struct ReplayStatistics {
        uint64_t total_events_processed = 0;
        uint64_t quote_events = 0;
        uint64_t trade_events = 0;
        uint64_t system_events = 0;
        
        uint64_t replay_start_time_ns = 0;
        uint64_t replay_end_time_ns = 0;
        uint64_t market_start_time_ns = 0;
        uint64_t market_end_time_ns = 0;
        
        double actual_replay_speed = 0.0;
        uint64_t events_per_second = 0;
        
        // Market activity statistics
        std::unordered_map<Symbol, uint64_t> symbol_activity;
        std::unordered_map<Symbol, Price> symbol_last_price;
    };

private:
    ReplayConfig config_;
    std::vector<std::unique_ptr<TickDataReader>> readers_;
    std::priority_queue<TickEvent, std::vector<TickEvent>, 
                       std::function<bool(const TickEvent&, const TickEvent&)>> event_queue_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    
    // Callbacks
    std::function<void(const TickEvent&)> tick_callback_;
    std::function<void(const ReplayStatistics&)> stats_callback_;
    std::function<void(const std::string&)> error_callback_;
    
    ReplayStatistics stats_;
    std::thread replay_thread_;
    std::thread progress_thread_;
    
public:
    TickReplayEngine() 
        : event_queue_([](const TickEvent& a, const TickEvent& b) {
              return a.timestamp_ns > b.timestamp_ns; // Min heap by timestamp
          }) {}
    
    ~TickReplayEngine() {
        stop();
    }
    
    bool initialize(const ReplayConfig& config) {
        config_ = config;
        
        // Open all data files
        for (const auto& filename : config.data_files) {
            std::unique_ptr<TickDataReader> reader;
            
            // Determine file type by extension
            if (filename.ends_with(".itch") || filename.ends_with(".bin")) {
                reader = std::make_unique<ITCHReader>();
            } else if (filename.ends_with(".csv") || filename.ends_with(".taq")) {
                reader = std::make_unique<TAQReader>();
            } else {
                if (error_callback_) {
                    error_callback_("Unknown file format: " + filename);
                }
                continue;
            }
            
            if (reader->open(filename)) {
                readers_.push_back(std::move(reader));
            } else {
                if (error_callback_) {
                    error_callback_("Failed to open file: " + filename);
                }
            }
        }
        
        if (readers_.empty()) {
            if (error_callback_) {
                error_callback_("No valid data files opened");
            }
            return false;
        }
        
        std::cout << "🎬 Replay engine initialized with " << readers_.size() << " data files" << std::endl;
        return true;
    }
    
    bool start() {
        if (readers_.empty()) {
            return false;
        }
        
        // Load initial events into priority queue
        load_initial_events();
        
        running_.store(true, std::memory_order_release);
        stats_.replay_start_time_ns = now_ns();
        
        // Start replay thread
        replay_thread_ = std::thread([this]() { replay_loop(); });
        
        // Start progress display thread
        if (config_.enable_progress_display) {
            progress_thread_ = std::thread([this]() { progress_loop(); });
        }
        
        std::cout << "▶️  Started tick data replay" << std::endl;
        return true;
    }
    
    void pause() {
        paused_.store(true, std::memory_order_release);
        std::cout << "⏸️  Paused replay" << std::endl;
    }
    
    void resume() {
        paused_.store(false, std::memory_order_release);
        std::cout << "▶️  Resumed replay" << std::endl;
    }
    
    void stop() {
        running_.store(false, std::memory_order_release);
        
        if (replay_thread_.joinable()) {
            replay_thread_.join();
        }
        
        if (progress_thread_.joinable()) {
            progress_thread_.join();
        }
        
        stats_.replay_end_time_ns = now_ns();
        
        // Close all readers
        for (auto& reader : readers_) {
            reader->close();
        }
        
        std::cout << "⏹️  Stopped tick data replay" << std::endl;
        print_final_statistics();
    }
    
    // Callback setters
    void set_tick_callback(std::function<void(const TickEvent&)> callback) {
        tick_callback_ = callback;
    }
    
    void set_stats_callback(std::function<void(const ReplayStatistics&)> callback) {
        stats_callback_ = callback;
    }
    
    void set_error_callback(std::function<void(const std::string&)> callback) {
        error_callback_ = callback;
    }
    
    const ReplayStatistics& get_statistics() const {
        return stats_;
    }
    
    double get_progress() const {
        uint64_t total_events = 0;
        for (const auto& reader : readers_) {
            total_events += reader->get_total_events();
        }
        
        if (total_events == 0) return 0.0;
        return static_cast<double>(stats_.total_events_processed) / total_events;
    }

private:
    void load_initial_events() {
        // Load first event from each reader
        for (auto& reader : readers_) {
            TickEvent event(TickEvent::QUOTE, 0, 0);
            if (reader->read_next_event(event)) {
                event_queue_.push(event);
            }
        }
        
        std::cout << "📥 Loaded " << event_queue_.size() << " initial events" << std::endl;
    }
    
    void replay_loop() {
        uint64_t last_event_time_ns = 0;
        uint64_t replay_start_real_time = now_ns();
        
        while (running_.load(std::memory_order_relaxed) && !event_queue_.empty()) {
            // Handle pause
            while (paused_.load(std::memory_order_relaxed) && 
                   running_.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (!running_.load(std::memory_order_relaxed)) break;
            
            // Get next event
            TickEvent event = event_queue_.top();
            event_queue_.pop();
            
            // Time-based filtering
            if (event.timestamp_ns < config_.start_time_ns || 
                event.timestamp_ns > config_.end_time_ns) {
                continue;
            }
            
            // Market hours filtering
            if (config_.enable_market_hours_only && !is_market_hours(event.timestamp_ns)) {
                continue;
            }
            
            // Timing control for realistic replay speed
            if (last_event_time_ns > 0 && config_.replay_speed > 0.0) {
                uint64_t market_time_diff = event.timestamp_ns - last_event_time_ns;
                uint64_t replay_time_diff = static_cast<uint64_t>(
                    market_time_diff / config_.replay_speed);
                
                if (replay_time_diff > 0) {
                    // Sleep to maintain replay speed
                    auto sleep_duration = std::chrono::nanoseconds(replay_time_diff);
                    if (sleep_duration > std::chrono::nanoseconds(0)) {
                        std::this_thread::sleep_for(sleep_duration);
                    }
                }
            }
            
            // Process event
            process_event(event);
            last_event_time_ns = event.timestamp_ns;
            
            // Load next event from the same reader
            load_next_event_from_readers();
        }
        
        // Calculate actual replay speed
        uint64_t replay_duration = now_ns() - replay_start_real_time;
        uint64_t market_duration = last_event_time_ns - stats_.market_start_time_ns;
        
        if (replay_duration > 0) {
            stats_.actual_replay_speed = static_cast<double>(market_duration) / replay_duration;
            stats_.events_per_second = (stats_.total_events_processed * 1000000000ULL) / replay_duration;
        }
    }
    
    void process_event(const TickEvent& event) {
        // Update statistics
        stats_.total_events_processed++;
        
        switch (event.type) {
            case TickEvent::QUOTE:
                stats_.quote_events++;
                break;
            case TickEvent::TRADE:
                stats_.trade_events++;
                stats_.symbol_last_price[event.symbol] = event.trade_price;
                break;
            case TickEvent::SYSTEM_EVENT:
                stats_.system_events++;
                break;
        }
        
        stats_.symbol_activity[event.symbol]++;
        
        if (stats_.market_start_time_ns == 0) {
            stats_.market_start_time_ns = event.timestamp_ns;
        }
        stats_.market_end_time_ns = event.timestamp_ns;
        
        // Apply market impact simulation if enabled
        TickEvent processed_event = event;
        if (config_.enable_market_impact) {
            apply_market_impact(processed_event);
        }
        
        // Send to callback
        if (tick_callback_) {
            tick_callback_(processed_event);
        }
    }
    
    void apply_market_impact(TickEvent& event) {
        if (event.type != TickEvent::TRADE) return;
        
        // Simple market impact model - large trades move the market
        if (event.trade_size > 10000) { // Large trade threshold
            double impact_factor = std::log(event.trade_size / 10000.0) * 0.001;
            Price impact = static_cast<Price>(event.trade_price * impact_factor);
            
            // Adjust trade price based on impact
            event.trade_price += impact;
        }
    }
    
    void load_next_event_from_readers() {
        // Try to load next event from each reader that might have more data
        for (auto& reader : readers_) {
            TickEvent event(TickEvent::QUOTE, 0, 0);
            if (reader->read_next_event(event)) {
                event_queue_.push(event);
                break; // Only load one event per cycle to maintain ordering
            }
        }
    }
    
    bool is_market_hours(uint64_t timestamp_ns) const {
        // Simplified market hours check (9:30 AM - 4:00 PM EST)
        // In production, use proper timezone handling
        
        // Extract time of day from timestamp
        time_t timestamp_s = timestamp_ns / 1000000000ULL;
        struct tm* time_info = localtime(&timestamp_s);
        
        int hour = time_info->tm_hour;
        int minute = time_info->tm_min;
        int time_minutes = hour * 60 + minute;
        
        // Market hours: 9:30 AM (570 minutes) to 4:00 PM (960 minutes)
        return time_minutes >= 570 && time_minutes <= 960;
    }
    
    void progress_loop() {
        while (running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(config_.progress_update_interval_ms));
            
            if (!running_.load(std::memory_order_relaxed)) break;
            
            // Update statistics and display progress
            double progress = get_progress() * 100.0;
            
            std::cout << "\r🎬 Replay Progress: " << std::fixed << std::setprecision(1) 
                      << progress << "% | Events: " << stats_.total_events_processed 
                      << " | Speed: " << std::setprecision(2) << stats_.actual_replay_speed << "x | "
                      << stats_.events_per_second << " events/sec" << std::flush;
                      
            if (stats_callback_) {
                stats_callback_(stats_);
            }
        }
    }
    
    void print_final_statistics() {
        std::cout << "\n TICK REPLAY STATISTICS" << std::endl;
        std::cout << "========================" << std::endl;
        std::cout << "Total Events Processed: " << stats_.total_events_processed << std::endl;
        std::cout << "  - Quote Events: " << stats_.quote_events << std::endl;
        std::cout << "  - Trade Events: " << stats_.trade_events << std::endl;
        std::cout << "  - System Events: " << stats_.system_events << std::endl;
        
        double replay_duration_sec = (stats_.replay_end_time_ns - stats_.replay_start_time_ns) / 1e9;
        double market_duration_sec = (stats_.market_end_time_ns - stats_.market_start_time_ns) / 1e9;
        
        std::cout << "\nTiming:" << std::endl;
        std::cout << "  Replay Duration: " << std::fixed << std::setprecision(2) 
                  << replay_duration_sec << " seconds" << std::endl;
        std::cout << "  Market Duration: " << market_duration_sec << " seconds" << std::endl;
        std::cout << "  Actual Replay Speed: " << stats_.actual_replay_speed << "x" << std::endl;
        std::cout << "  Events per Second: " << stats_.events_per_second << std::endl;
        
        std::cout << "\nMost Active Symbols:" << std::endl;
        std::vector<std::pair<Symbol, uint64_t>> sorted_activity(
            stats_.symbol_activity.begin(), stats_.symbol_activity.end());
        std::sort(sorted_activity.begin(), sorted_activity.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        for (size_t i = 0; i < std::min(size_t(10), sorted_activity.size()); ++i) {
            std::cout << "  Symbol " << sorted_activity[i].first 
                      << ": " << sorted_activity[i].second << " events" << std::endl;
        }
    }
};

// Comprehensive backtesting framework
class BacktestEngine {
public:
    struct BacktestConfig {
        std::vector<std::string> data_files;
        uint64_t initial_capital_cents = 100000000;  // $1M in cents
        double max_position_size = 1000000;          // Max 1M shares per symbol
        double commission_per_share = 0.005;         // 0.5 cents per share
        bool enable_slippage_model = true;
        double slippage_bps = 2.0;                   // 2 basis points slippage
        std::string strategy_name = "default";
        std::string output_file = "backtest_results.json";
        bool save_trade_log = true;
        bool enable_risk_limits = true;
    };
    
    struct BacktestResults {
        // P&L metrics
        int64_t total_pnl_cents = 0;
        int64_t realized_pnl_cents = 0;
        int64_t unrealized_pnl_cents = 0;
        double total_return_pct = 0.0;
        double annualized_return_pct = 0.0;
        
        // Risk metrics
        double sharpe_ratio = 0.0;
        double max_drawdown_pct = 0.0;
        double volatility_pct = 0.0;
        double var_95_cents = 0.0;  // Value at Risk 95%
        
        // Trading statistics
        uint64_t total_trades = 0;
        uint64_t winning_trades = 0;
        uint64_t losing_trades = 0;
        double win_rate = 0.0;
        double avg_win_cents = 0.0;
        double avg_loss_cents = 0.0;
        double profit_factor = 0.0;
        
        // Volume and frequency
        uint64_t total_volume = 0;
        double avg_trade_size = 0.0;
        double trades_per_day = 0.0;
        int64_t total_commission_cents = 0;
        
        // Time series data
        std::vector<std::pair<uint64_t, int64_t>> equity_curve;  // timestamp_ns, equity_cents
        std::vector<std::pair<uint64_t, double>> drawdown_series;
        
        uint64_t backtest_duration_ns = 0;
        std::string start_date;
        std::string end_date;
    };

private:
    BacktestConfig config_;
    TickReplayEngine replay_engine_;
    
    // Portfolio state
    int64_t current_equity_cents_;
    std::unordered_map<Symbol, int64_t> positions_;  // Symbol -> signed quantity
    std::unordered_map<Symbol, Price> avg_costs_;    // Symbol -> average cost
    
    // P&L tracking
    std::vector<TradeRecord> trade_log_;
    std::vector<std::pair<uint64_t, int64_t>> equity_history_;
    
    // Risk tracking
    int64_t peak_equity_cents_;
    std::vector<double> daily_returns_;
    
    // Market data tracking
    std::unordered_map<Symbol, Price> current_prices_;
    
public:
    explicit BacktestEngine(const BacktestConfig& config)
        : config_(config), current_equity_cents_(config.initial_capital_cents),
          peak_equity_cents_(config.initial_capital_cents) {
        
        // Configure replay engine
        TickReplayEngine::ReplayConfig replay_config;
        replay_config.data_files = config.data_files;
        replay_config.replay_speed = 0.0;  // Run as fast as possible
        replay_config.enable_progress_display = false;
        
        replay_engine_.initialize(replay_config);
        
        // Set up callbacks
        replay_engine_.set_tick_callback([this](const TickEvent& event) {
            process_tick_event(event);
        });
    }
    
    // Run comprehensive backtest
    BacktestResults run_backtest() {
        std::cout << " Starting Comprehensive Backtest" << std::endl;
        std::cout << "==================================" << std::endl;
        std::cout << "Initial Capital: $" << config_.initial_capital_cents / 100.0 << std::endl;
        std::cout << "Strategy: " << config_.strategy_name << std::endl;
        
        // Reset state
        reset_backtest_state();
        
        // Run replay
        auto start_time = std::chrono::high_resolution_clock::now();
        replay_engine_.start();
        
        // Wait for completion - in a real implementation, this would be event-driven
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        replay_engine_.stop();
        auto end_time = std::chrono::high_resolution_clock::now();
        
        // Calculate results
        BacktestResults results;
        results.backtest_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end_time - start_time).count();
        
        calculate_backtest_results(results);
        print_backtest_results(results);
        
        if (config_.save_trade_log) {
            save_backtest_results(results);
        }
        
        return results;
    }
    
    // Strategy interface - override this in derived classes
    virtual void on_tick(const TickEvent& event) {
        // Default strategy: simple mean reversion
        if (event.type == TickEvent::TRADE) {
            implement_mean_reversion_strategy(event);
        }
    }

private:
    void process_tick_event(const TickEvent& event) {
        // Update current prices
        if (event.type == TickEvent::TRADE) {
            current_prices_[event.symbol] = event.trade_price;
        } else if (event.type == TickEvent::QUOTE) {
            Price mid_price = (event.bid_price + event.ask_price) / 2;
            if (mid_price > 0) {
                current_prices_[event.symbol] = mid_price;
            }
        }
        
        // Update unrealized P&L
        update_unrealized_pnl();
        
        // Record equity point
        equity_history_.emplace_back(event.timestamp_ns, get_total_equity());
        
        // Call strategy
        on_tick(event);
    }
    
    void implement_mean_reversion_strategy(const TickEvent& event) {
        // Simple mean reversion: buy when price drops, sell when price rises
        static std::unordered_map<Symbol, Price> price_history;
        
        Price current_price = event.trade_price;
        
        if (price_history.find(event.symbol) != price_history.end()) {
            Price last_price = price_history[event.symbol];
            double price_change = (static_cast<double>(current_price) - static_cast<double>(last_price)) / last_price;
            
            // Trade on significant price moves
            if (price_change > 0.01) {  // Price up 1%, sell
                execute_trade(event.symbol, -100, current_price, event.timestamp_ns);
            } else if (price_change < -0.01) {  // Price down 1%, buy
                execute_trade(event.symbol, 100, current_price, event.timestamp_ns);
            }
        }
        
        price_history[event.symbol] = current_price;
    }
    
    void execute_trade(Symbol symbol, int64_t quantity, Price price, uint64_t timestamp) {
        // Apply slippage model
        if (config_.enable_slippage_model) {
            double slippage_factor = config_.slippage_bps / 10000.0;
            if (quantity > 0) {  // Buy - pay higher
                price = static_cast<Price>(price * (1.0 + slippage_factor));
            } else {  // Sell - receive lower
                price = static_cast<Price>(price * (1.0 - slippage_factor));
            }
        }
        
        // Calculate trade value
        int64_t trade_value_cents = static_cast<int64_t>(std::abs(quantity)) * price;
        int64_t commission_cents = static_cast<int64_t>(std::abs(quantity) * config_.commission_per_share * 100);
        
        // Check if we have enough capital
        if (quantity > 0 && trade_value_cents + commission_cents > current_equity_cents_) {
            return;  // Insufficient capital
        }
        
        // Execute the trade
        int64_t old_position = positions_[symbol];
        positions_[symbol] += quantity;
        
        // Calculate realized P&L if reducing position
        int64_t realized_pnl_cents = 0;
        if ((old_position > 0 && quantity < 0) || (old_position < 0 && quantity > 0)) {
            // Position reduction - calculate realized P&L
            Price avg_cost = avg_costs_[symbol];
            int64_t reduction_qty = std::min(std::abs(old_position), std::abs(quantity));
            
            if (old_position > 0) {  // Long position, selling
                realized_pnl_cents = static_cast<int64_t>(reduction_qty) * (price - avg_cost);
            } else {  // Short position, buying
                realized_pnl_cents = static_cast<int64_t>(reduction_qty) * (avg_cost - price);
            }
        }
        
        // Update average cost for position increases
        if ((old_position >= 0 && quantity > 0) || (old_position <= 0 && quantity < 0)) {
            Price old_avg = avg_costs_[symbol];
            int64_t old_qty = std::abs(old_position);
            int64_t new_qty = std::abs(quantity);
            
            if (old_qty > 0) {
                avg_costs_[symbol] = static_cast<Price>(
                    (old_avg * old_qty + price * new_qty) / (old_qty + new_qty));
            } else {
                avg_costs_[symbol] = price;
            }
        }
        
        // Record trade
        TradeRecord trade;
        trade.trade_id = trade_log_.size() + 1;
        trade.timestamp_ns = timestamp;
        trade.symbol = symbol;
        trade.side = quantity > 0 ? Side::BUY : Side::SELL;
        trade.price = price;
        trade.qty = std::abs(quantity);
        trade.pnl_cents = realized_pnl_cents - commission_cents;
        trade.strategy_name = config_.strategy_name;
        
        trade_log_.push_back(trade);
        
        // Update equity
        current_equity_cents_ += realized_pnl_cents - commission_cents - (quantity > 0 ? trade_value_cents : -trade_value_cents);
    }
    
    void update_unrealized_pnl() {
        // Calculate unrealized P&L based on current market prices
        for (const auto& [symbol, position] : positions_) {
            if (position == 0) continue;
            
            auto price_it = current_prices_.find(symbol);
            if (price_it == current_prices_.end()) continue;
            
            Price current_price = price_it->second;
            Price avg_cost = avg_costs_[symbol];
            
            // This is a simplified unrealized P&L calculation
            // In practice, you'd want more sophisticated mark-to-market
        }
    }
    
    int64_t get_total_equity() {
        int64_t total_equity = current_equity_cents_;
        
        // Add unrealized P&L
        for (const auto& [symbol, position] : positions_) {
            if (position == 0) continue;
            
            auto price_it = current_prices_.find(symbol);
            if (price_it == current_prices_.end()) continue;
            
            Price current_price = price_it->second;
            Price avg_cost = avg_costs_[symbol];
            
            int64_t unrealized_pnl = static_cast<int64_t>(position) * (current_price - avg_cost);
            total_equity += unrealized_pnl;
        }
        
        return total_equity;
    }
    
    void calculate_backtest_results(BacktestResults& results) {
        results.total_trades = trade_log_.size();
        results.total_pnl_cents = get_total_equity() - config_.initial_capital_cents;
        results.total_return_pct = static_cast<double>(results.total_pnl_cents) / config_.initial_capital_cents * 100.0;
        
        // Calculate trade statistics
        if (!trade_log_.empty()) {
            uint64_t winning = 0;
            int64_t total_wins = 0;
            int64_t total_losses = 0;
            
            for (const auto& trade : trade_log_) {
                if (trade.pnl_cents > 0) {
                    winning++;
                    total_wins += trade.pnl_cents;
                } else if (trade.pnl_cents < 0) {
                    total_losses += std::abs(trade.pnl_cents);
                }
            }
            
            results.winning_trades = winning;
            results.losing_trades = trade_log_.size() - winning;
            results.win_rate = static_cast<double>(winning) / trade_log_.size();
            
            if (winning > 0) {
                results.avg_win_cents = static_cast<double>(total_wins) / winning;
            }
            
            if (results.losing_trades > 0) {
                results.avg_loss_cents = static_cast<double>(total_losses) / results.losing_trades;
            }
            
            if (total_losses > 0) {
                results.profit_factor = static_cast<double>(total_wins) / total_losses;
            }
        }
        
        // Calculate maximum drawdown
        results.max_drawdown_pct = calculate_max_drawdown();
        
        // Set equity curve
        results.equity_curve = equity_history_;
    }
    
    double calculate_max_drawdown() {
        if (equity_history_.empty()) return 0.0;
        
        int64_t peak = equity_history_[0].second;
        double max_drawdown = 0.0;
        
        for (const auto& [timestamp, equity] : equity_history_) {
            if (equity > peak) {
                peak = equity;
            } else {
                double drawdown = static_cast<double>(peak - equity) / peak * 100.0;
                max_drawdown = std::max(max_drawdown, drawdown);
            }
        }
        
        return max_drawdown;
    }
    
    void reset_backtest_state() {
        current_equity_cents_ = config_.initial_capital_cents;
        peak_equity_cents_ = config_.initial_capital_cents;
        positions_.clear();
        avg_costs_.clear();
        trade_log_.clear();
        equity_history_.clear();
        current_prices_.clear();
    }
    
    void print_backtest_results(const BacktestResults& results) {
        std::cout << "\n BACKTEST RESULTS" << std::endl;
        std::cout << "==================" << std::endl;
        
        std::cout << "\n💰 P&L PERFORMANCE:" << std::endl;
        std::cout << "  Total P&L: $" << std::fixed << std::setprecision(2) 
                  << results.total_pnl_cents / 100.0 << std::endl;
        std::cout << "  Total Return: " << std::setprecision(2) << results.total_return_pct << "%" << std::endl;
        std::cout << "  Max Drawdown: " << results.max_drawdown_pct << "%" << std::endl;
        
        std::cout << "\n TRADING STATISTICS:" << std::endl;
        std::cout << "  Total Trades: " << results.total_trades << std::endl;
        std::cout << "  Winning Trades: " << results.winning_trades << std::endl;
        std::cout << "  Losing Trades: " << results.losing_trades << std::endl;
        std::cout << "  Win Rate: " << std::setprecision(1) << results.win_rate * 100.0 << "%" << std::endl;
        std::cout << "  Profit Factor: " << std::setprecision(2) << results.profit_factor << std::endl;
        
        if (results.avg_win_cents > 0) {
            std::cout << "  Avg Win: $" << results.avg_win_cents / 100.0 << std::endl;
        }
        if (results.avg_loss_cents > 0) {
            std::cout << "  Avg Loss: $" << results.avg_loss_cents / 100.0 << std::endl;
        }
    }
    
    void save_backtest_results(const BacktestResults& results) {
        std::ofstream file(config_.output_file);
        if (!file.is_open()) {
            std::cerr << "Failed to save backtest results" << std::endl;
            return;
        }
        
        file << "{\n";
        file << "  \"strategy\": \"" << config_.strategy_name << "\",\n";
        file << "  \"total_pnl_cents\": " << results.total_pnl_cents << ",\n";
        file << "  \"total_return_pct\": " << results.total_return_pct << ",\n";
        file << "  \"max_drawdown_pct\": " << results.max_drawdown_pct << ",\n";
        file << "  \"total_trades\": " << results.total_trades << ",\n";
        file << "  \"win_rate\": " << results.win_rate << ",\n";
        file << "  \"profit_factor\": " << results.profit_factor << "\n";
        file << "}\n";
        
        file.close();
        std::cout << "💾 Backtest results saved to " << config_.output_file << std::endl;
    }
};

} // namespace hft