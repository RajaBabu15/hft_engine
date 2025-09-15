#pragma once

#include "hft/types.h"
#include "hft/lockfree_queue.h"
#include "hft/market_simulator.h"
#include <string>
#include <vector>
#include <fstream>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <functional>
#include <iomanip>

namespace hft {

// Tick data record structure
struct TickRecord {
    uint64_t timestamp_ns;
    Symbol symbol_id;
    Price bid_price;
    Price ask_price;
    Quantity bid_size;
    Quantity ask_size;
    Price last_trade_price;
    Quantity last_trade_size;
    uint32_t trade_count;
    
    TickRecord() : timestamp_ns(0), symbol_id(0), bid_price(0), ask_price(0),
                   bid_size(0), ask_size(0), last_trade_price(0), 
                   last_trade_size(0), trade_count(0) {}
};

// Replay modes
enum class ReplayMode : uint8_t {
    REAL_TIME,      // Replay at original speed
    ACCELERATED,    // Replay faster than real-time
    STEP_BY_STEP,   // Manual step-through
    BATCH          // Process all data as fast as possible
};

// Data source interface
class ITickDataSource {
public:
    virtual ~ITickDataSource() = default;
    virtual bool load_data(const std::string& source) = 0;
    virtual bool get_next_tick(TickRecord& tick) = 0;
    virtual void reset() = 0;
    virtual size_t get_total_ticks() const = 0;
    virtual size_t get_current_position() const = 0;
    virtual bool seek_to_time(uint64_t timestamp_ns) = 0;
};

// CSV file data source
class CsvTickDataSource : public ITickDataSource {
private:
    std::vector<TickRecord> ticks_;
    size_t current_position_;
    
    uint64_t parse_timestamp(const std::string& timestamp_str) {
        // Parse timestamp in format "YYYY-MM-DD HH:MM:SS.sss"
        // This is a simplified parser - in practice would use a proper datetime library
        std::tm tm = {};
        std::istringstream ss(timestamp_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        
        auto time_point = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            time_point.time_since_epoch()).count();
    }
    
    Price parse_price(const std::string& price_str) {
        double price_d = std::stod(price_str);
        return static_cast<Price>(price_d * 10000); // Assume 4 decimal places
    }
    
    Quantity parse_quantity(const std::string& qty_str) {
        return static_cast<Quantity>(std::stoll(qty_str));
    }

public:
    CsvTickDataSource() : current_position_(0) {}
    
    bool load_data(const std::string& filename) override {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        ticks_.clear();
        std::string line;
        
        // Skip header if present
        if (std::getline(file, line)) {
            if (line.find("timestamp") != std::string::npos ||
                line.find("time") != std::string::npos) {
                // Header line, skip it
            } else {
                // First line is data, process it
                file.seekg(0, std::ios::beg);
            }
        }
        
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            std::istringstream iss(line);
            std::string field;
            std::vector<std::string> fields;
            
            while (std::getline(iss, field, ',')) {
                fields.push_back(field);
            }
            
            if (fields.size() >= 8) {
                TickRecord tick;
                try {
                    tick.timestamp_ns = parse_timestamp(fields[0]);
                    tick.symbol_id = std::stoull(fields[1]);
                    tick.bid_price = parse_price(fields[2]);
                    tick.ask_price = parse_price(fields[3]);
                    tick.bid_size = parse_quantity(fields[4]);
                    tick.ask_size = parse_quantity(fields[5]);
                    tick.last_trade_price = fields.size() > 6 ? parse_price(fields[6]) : 0;
                    tick.last_trade_size = fields.size() > 7 ? parse_quantity(fields[7]) : 0;
                    tick.trade_count = fields.size() > 8 ? std::stoul(fields[8]) : 0;
                    
                    ticks_.push_back(tick);
                } catch (const std::exception& e) {
                    // Skip invalid lines
                    continue;
                }
            }
        }
        
        current_position_ = 0;
        return !ticks_.empty();
    }
    
    bool get_next_tick(TickRecord& tick) override {
        if (current_position_ >= ticks_.size()) {
            return false;
        }
        
        tick = ticks_[current_position_++];
        return true;
    }
    
    void reset() override {
        current_position_ = 0;
    }
    
    size_t get_total_ticks() const override {
        return ticks_.size();
    }
    
    size_t get_current_position() const override {
        return current_position_;
    }
    
    bool seek_to_time(uint64_t timestamp_ns) override {
        for (size_t i = 0; i < ticks_.size(); ++i) {
            if (ticks_[i].timestamp_ns >= timestamp_ns) {
                current_position_ = i;
                return true;
            }
        }
        return false;
    }
};

// Binary file data source for high-performance replay
class BinaryTickDataSource : public ITickDataSource {
private:
    std::vector<TickRecord> ticks_;
    size_t current_position_;

public:
    BinaryTickDataSource() : current_position_(0) {}
    
    bool load_data(const std::string& filename) override {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Read header (number of records)
        uint64_t record_count;
        file.read(reinterpret_cast<char*>(&record_count), sizeof(record_count));
        
        if (file.fail()) {
            return false;
        }
        
        ticks_.resize(record_count);
        file.read(reinterpret_cast<char*>(ticks_.data()), 
                 record_count * sizeof(TickRecord));
        
        current_position_ = 0;
        return !file.fail();
    }
    
    bool get_next_tick(TickRecord& tick) override {
        if (current_position_ >= ticks_.size()) {
            return false;
        }
        
        tick = ticks_[current_position_++];
        return true;
    }
    
    void reset() override {
        current_position_ = 0;
    }
    
    size_t get_total_ticks() const override {
        return ticks_.size();
    }
    
    size_t get_current_position() const override {
        return current_position_;
    }
    
    bool seek_to_time(uint64_t timestamp_ns) override {
        // Binary search for timestamp
        size_t left = 0, right = ticks_.size();
        while (left < right) {
            size_t mid = left + (right - left) / 2;
            if (ticks_[mid].timestamp_ns < timestamp_ns) {
                left = mid + 1;
            } else {
                right = mid;
            }
        }
        
        current_position_ = left;
        return left < ticks_.size();
    }
    
    // Save data in binary format for faster loading
    bool save_data(const std::string& filename, const std::vector<TickRecord>& ticks) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        uint64_t record_count = ticks.size();
        file.write(reinterpret_cast<const char*>(&record_count), sizeof(record_count));
        file.write(reinterpret_cast<const char*>(ticks.data()), 
                  record_count * sizeof(TickRecord));
        
        return !file.fail();
    }
};

// Tick data replay harness
class TickDataReplayHarness {
private:
    std::unique_ptr<ITickDataSource> data_source_;
    ReplayMode replay_mode_;
    double acceleration_factor_;
    
    // Replay thread
    std::thread replay_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> step_requested_{false};
    
    // Output queue
    LockFreeQueue<MarketDataUpdate, 32768> output_queue_;
    
    // Timing control
    uint64_t replay_start_time_ns_;
    uint64_t data_start_time_ns_;
    std::atomic<uint64_t> current_data_time_ns_{0};
    
    // Statistics
    std::atomic<uint64_t> ticks_processed_{0};
    std::atomic<uint64_t> ticks_skipped_{0};
    
    void replay_loop() {
        TickRecord tick;
        uint64_t last_timestamp = 0;
        
        while (running_.load(std::memory_order_relaxed)) {
            // Handle pause
            while (paused_.load(std::memory_order_relaxed) && 
                   running_.load(std::memory_order_relaxed)) {
                if (replay_mode_ == ReplayMode::STEP_BY_STEP && 
                    step_requested_.exchange(false, std::memory_order_relaxed)) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            
            if (!running_.load(std::memory_order_relaxed)) break;
            
            // Get next tick
            if (!data_source_->get_next_tick(tick)) {
                // End of data
                break;
            }
            
            current_data_time_ns_.store(tick.timestamp_ns, std::memory_order_relaxed);
            
            // Handle timing based on replay mode
            if (replay_mode_ == ReplayMode::REAL_TIME || 
                replay_mode_ == ReplayMode::ACCELERATED) {
                
                if (last_timestamp > 0) {
                    uint64_t data_time_diff = tick.timestamp_ns - last_timestamp;
                    uint64_t replay_time_diff = static_cast<uint64_t>(
                        data_time_diff / acceleration_factor_);
                    
                    if (replay_time_diff > 0) {
                        std::this_thread::sleep_for(
                            std::chrono::nanoseconds(replay_time_diff));
                    }
                }
            } else if (replay_mode_ == ReplayMode::STEP_BY_STEP) {
                // Pause after each tick
                paused_.store(true, std::memory_order_relaxed);
            }
            
            // Convert to MarketDataUpdate and enqueue
            process_tick(tick);
            
            last_timestamp = tick.timestamp_ns;
            ticks_processed_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void process_tick(const TickRecord& tick) {
        // Generate bid update
        if (tick.bid_price > 0 && tick.bid_size > 0) {
            MarketDataUpdate bid_update;
            bid_update.symbol_id = tick.symbol_id;
            bid_update.timestamp = tick.timestamp_ns;
            bid_update.price = tick.bid_price;
            bid_update.quantity = tick.bid_size;
            bid_update.side = 0; // Bid
            bid_update.update_type = 1; // Modify
            
            if (!output_queue_.try_enqueue(bid_update)) {
                ticks_skipped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        // Generate ask update
        if (tick.ask_price > 0 && tick.ask_size > 0) {
            MarketDataUpdate ask_update;
            ask_update.symbol_id = tick.symbol_id;
            ask_update.timestamp = tick.timestamp_ns;
            ask_update.price = tick.ask_price;
            ask_update.quantity = tick.ask_size;
            ask_update.side = 1; // Ask
            ask_update.update_type = 1; // Modify
            
            if (!output_queue_.try_enqueue(ask_update)) {
                ticks_skipped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        // Generate trade update if present
        if (tick.last_trade_price > 0 && tick.last_trade_size > 0) {
            MarketDataUpdate trade_update;
            trade_update.symbol_id = tick.symbol_id;
            trade_update.timestamp = tick.timestamp_ns;
            trade_update.price = tick.last_trade_price;
            trade_update.quantity = tick.last_trade_size;
            trade_update.side = 2; // Trade (neutral)
            trade_update.update_type = 3; // Trade
            
            if (!output_queue_.try_enqueue(trade_update)) {
                ticks_skipped_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

public:
    enum class DataFormat {
        CSV,
        BINARY
    };
    
    TickDataReplayHarness(DataFormat format = DataFormat::CSV) 
        : replay_mode_(ReplayMode::REAL_TIME), acceleration_factor_(1.0) {
        
        switch (format) {
            case DataFormat::CSV:
                data_source_ = std::make_unique<CsvTickDataSource>();
                break;
            case DataFormat::BINARY:
                data_source_ = std::make_unique<BinaryTickDataSource>();
                break;
        }
        
        replay_start_time_ns_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    }
    
    ~TickDataReplayHarness() {
        stop_replay();
    }
    
    bool load_data(const std::string& filename) {
        return data_source_->load_data(filename);
    }
    
    void set_replay_mode(ReplayMode mode, double acceleration = 1.0) {
        replay_mode_ = mode;
        acceleration_factor_ = std::max(0.1, acceleration);
    }
    
    bool start_replay() {
        if (running_.load(std::memory_order_relaxed)) return false;
        
        data_source_->reset();
        ticks_processed_.store(0, std::memory_order_relaxed);
        ticks_skipped_.store(0, std::memory_order_relaxed);
        paused_.store(false, std::memory_order_relaxed);
        
        running_.store(true, std::memory_order_relaxed);
        replay_thread_ = std::thread(&TickDataReplayHarness::replay_loop, this);
        
        return true;
    }
    
    void stop_replay() {
        running_.store(false, std::memory_order_relaxed);
        paused_.store(false, std::memory_order_relaxed);
        
        if (replay_thread_.joinable()) {
            replay_thread_.join();
        }
    }
    
    void pause_replay() {
        paused_.store(true, std::memory_order_relaxed);
    }
    
    void resume_replay() {
        paused_.store(false, std::memory_order_relaxed);
    }
    
    void step_forward() {
        if (replay_mode_ == ReplayMode::STEP_BY_STEP) {
            step_requested_.store(true, std::memory_order_relaxed);
        }
    }
    
    bool get_market_update(MarketDataUpdate& update) {
        return output_queue_.try_dequeue(update);
    }
    
    bool seek_to_time(uint64_t timestamp_ns) {
        if (running_.load(std::memory_order_relaxed)) return false;
        return data_source_->seek_to_time(timestamp_ns);
    }
    
    // Get replay statistics
    struct ReplayStats {
        size_t total_ticks;
        size_t current_position;
        uint64_t ticks_processed;
        uint64_t ticks_skipped;
        uint64_t current_data_time_ns;
        bool is_running;
        bool is_paused;
        size_t queue_size;
        double progress_percent;
    };
    
    ReplayStats get_stats() const {
        ReplayStats stats;
        stats.total_ticks = data_source_->get_total_ticks();
        stats.current_position = data_source_->get_current_position();
        stats.ticks_processed = ticks_processed_.load(std::memory_order_relaxed);
        stats.ticks_skipped = ticks_skipped_.load(std::memory_order_relaxed);
        stats.current_data_time_ns = current_data_time_ns_.load(std::memory_order_relaxed);
        stats.is_running = running_.load(std::memory_order_relaxed);
        stats.is_paused = paused_.load(std::memory_order_relaxed);
        stats.queue_size = output_queue_.size();
        
        if (stats.total_ticks > 0) {
            stats.progress_percent = (100.0 * stats.current_position) / stats.total_ticks;
        } else {
            stats.progress_percent = 0.0;
        }
        
        return stats;
    }
    
    // Backtesting integration
    void register_backtest_callback(std::function<void(const MarketDataUpdate&)> callback) {
        // This would be implemented to support backtesting frameworks
        // For now, it's a placeholder for the interface
    }
    
    // Generate sample data for testing
    static bool generate_sample_csv(const std::string& filename, size_t num_ticks = 10000) {
        std::ofstream file(filename);
        if (!file.is_open()) return false;
        
        // Write header
        file << "timestamp,symbol,bid_price,ask_price,bid_size,ask_size,last_price,last_size,trade_count\n";
        
        // Generate sample data
        auto start_time = std::chrono::system_clock::now();
        Price base_price = 50000; // $50.00
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<double> price_dist(0.0, 0.01);
        std::poisson_distribution<int> size_dist(100);
        
        for (size_t i = 0; i < num_ticks; ++i) {
            auto tick_time = start_time + std::chrono::milliseconds(i * 100);
            auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                tick_time.time_since_epoch()).count();
            
            double price_change = price_dist(gen);
            Price current_price = base_price + static_cast<Price>(price_change * 10000);
            Price bid = current_price - 1;
            Price ask = current_price + 1;
            
            Quantity bid_size = std::max(1, size_dist(gen));
            Quantity ask_size = std::max(1, size_dist(gen));
            
            // Format timestamp as readable string
            auto time_t_val = std::chrono::system_clock::to_time_t(tick_time);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                tick_time.time_since_epoch()) % 1000;
            
            std::tm* tm_ptr = std::gmtime(&time_t_val);
            file << std::put_time(tm_ptr, "%Y-%m-%d %H:%M:%S") 
                 << "." << std::setfill('0') << std::setw(3) << ms.count()
                 << ",1," << bid/10000.0 << "," << ask/10000.0 
                 << "," << bid_size << "," << ask_size 
                 << "," << current_price/10000.0 << "," << (bid_size + ask_size)/2 
                 << ",1\n";
        }
        
        return true;
    }
};

} // namespace hft