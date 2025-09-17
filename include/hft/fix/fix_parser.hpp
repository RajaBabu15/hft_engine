#pragma once

#include "hft/core/types.hpp"
#include "hft/core/lock_free_queue.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <functional>

namespace hft {
namespace fix {

// ============================================================================
// FIX Protocol Constants
// ============================================================================

constexpr char FIX_SOH = '\001';  // Start of header
constexpr char FIX_EQUALS = '=';
constexpr size_t MAX_FIX_MESSAGE_SIZE = 8192;

// Common FIX tags
namespace Tags {
    constexpr uint32_t BEGIN_STRING = 8;
    constexpr uint32_t BODY_LENGTH = 9;
    constexpr uint32_t CHECK_SUM = 10;
    constexpr uint32_t MSG_SEQ_NUM = 34;
    constexpr uint32_t MSG_TYPE = 35;
    constexpr uint32_t SENDER_COMP_ID = 49;
    constexpr uint32_t SENDING_TIME = 52;
    constexpr uint32_t TARGET_COMP_ID = 56;
    
    // Order related tags
    constexpr uint32_t CL_ORD_ID = 11;
    constexpr uint32_t ORDER_ID = 37;
    constexpr uint32_t EXEC_ID = 17;
    constexpr uint32_t EXEC_TYPE = 150;
    constexpr uint32_t ORD_STATUS = 39;
    constexpr uint32_t SYMBOL = 55;
    constexpr uint32_t SIDE = 54;
    constexpr uint32_t ORDER_QTY = 38;
    constexpr uint32_t PRICE = 44;
    constexpr uint32_t TIME_IN_FORCE = 59;
    constexpr uint32_t ORD_TYPE = 40;
    constexpr uint32_t LAST_QTY = 32;
    constexpr uint32_t LAST_PX = 31;
    constexpr uint32_t LEAVES_QTY = 151;
    constexpr uint32_t CUM_QTY = 14;
    constexpr uint32_t AVG_PX = 6;
}

// ============================================================================
// FIX Message Structure
// ============================================================================

struct FixField {
    uint32_t tag;
    std::string value;
    
    FixField() = default;
    FixField(uint32_t t, const std::string& v) : tag(t), value(v) {}
    FixField(uint32_t t, std::string&& v) : tag(t), value(std::move(v)) {}
};

struct FixMessage {
    std::string begin_string;
    uint32_t body_length;
    std::string msg_type;
    uint32_t msg_seq_num;
    std::string sender_comp_id;
    std::string target_comp_id;
    std::string sending_time;
    uint8_t checksum;
    
    std::unordered_map<uint32_t, std::string> fields;
    std::vector<FixField> ordered_fields;  // Maintains order for checksum calculation
    
    // Utility methods
    bool has_field(uint32_t tag) const;
    const std::string& get_field(uint32_t tag) const;
    void set_field(uint32_t tag, const std::string& value);
    void set_field(uint32_t tag, std::string&& value);
    
    // Type-safe getters
    double get_price(uint32_t tag) const;
    uint64_t get_quantity(uint32_t tag) const;
    int get_int(uint32_t tag) const;
    
    // Message validation
    bool is_valid() const;
    uint8_t calculate_checksum() const;
    
    void clear();
};

// ============================================================================
// FIX Parser Statistics
// ============================================================================

struct FixParserStats {
    std::atomic<uint64_t> messages_parsed{0};
    std::atomic<uint64_t> parse_errors{0};
    std::atomic<uint64_t> checksum_errors{0};
    std::atomic<uint64_t> invalid_messages{0};
    std::atomic<uint64_t> bytes_processed{0};
    std::atomic<double> avg_parse_time_ns{0.0};
    
    void reset() {
        messages_parsed = 0;
        parse_errors = 0;
        checksum_errors = 0;
        invalid_messages = 0;
        bytes_processed = 0;
        avg_parse_time_ns = 0.0;
    }
};

// ============================================================================
// Multithreaded FIX Parser
// ============================================================================

class FixParser {
public:
    using MessageCallback = std::function<void(const FixMessage&)>;
    using ErrorCallback = std::function<void(const std::string&, const std::string&)>;
    
private:
    static constexpr size_t PARSER_QUEUE_SIZE = 4096;
    static constexpr size_t MAX_WORKER_THREADS = 8;
    
    // Parser state
    std::atomic<bool> running_{false};
    size_t num_worker_threads_;
    std::vector<std::thread> worker_threads_;
    
    // Message queues for multithreaded processing
    std::unique_ptr<core::LockFreeQueue<std::string, PARSER_QUEUE_SIZE>> raw_message_queue_;
    std::unique_ptr<core::LockFreeQueue<FixMessage, PARSER_QUEUE_SIZE>> parsed_message_queue_;
    
    // Callbacks
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    
    // Statistics
    FixParserStats stats_;
    
    // Message parsing
    std::string message_buffer_;
    size_t buffer_position_;
    
public:
    explicit FixParser(size_t num_workers = 4);
    ~FixParser();
    
    // Non-copyable, non-movable
    FixParser(const FixParser&) = delete;
    FixParser& operator=(const FixParser&) = delete;
    FixParser(FixParser&&) = delete;
    FixParser& operator=(FixParser&&) = delete;
    
    // Configuration
    void set_message_callback(MessageCallback callback);
    void set_error_callback(ErrorCallback callback);
    
    // Parser control
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Message processing
    void feed_data(const char* data, size_t length);
    void feed_data(const std::string& data);
    
    // Single message parsing (for testing/sync use)
    bool parse_message(const std::string& raw_message, FixMessage& parsed_message);
    
    // Statistics
    const FixParserStats& get_stats() const { return stats_; }
    void reset_stats() { stats_.reset(); }
    
    // Performance tuning
    void set_worker_threads(size_t count);
    size_t get_worker_threads() const { return num_worker_threads_; }

private:
    // Worker thread functions
    void parsing_worker();
    void processing_worker();
    
    // Message parsing helpers
    bool extract_next_message(std::string& message);
    bool parse_message_internal(const std::string& raw_message, FixMessage& message);
    bool parse_field(const std::string& field_str, uint32_t& tag, std::string& value);
    
    // Validation helpers
    bool validate_message_structure(const FixMessage& message);
    uint8_t calculate_checksum(const std::string& message_without_checksum);
    
    // Statistics helpers
    void update_parse_time(double parse_time_ns);
};

// ============================================================================
// FIX Message Builder
// ============================================================================

class FixMessageBuilder {
private:
    FixMessage message_;
    std::string sender_comp_id_;
    std::string target_comp_id_;
    uint32_t next_seq_num_;
    
public:
    explicit FixMessageBuilder(const std::string& sender_comp_id, const std::string& target_comp_id);
    
    // Header methods
    FixMessageBuilder& begin_string(const std::string& version = "FIX.4.4");
    FixMessageBuilder& msg_type(const std::string& type);
    FixMessageBuilder& msg_seq_num(uint32_t seq_num);
    FixMessageBuilder& sending_time();  // Use current time
    FixMessageBuilder& sending_time(const std::string& time);
    
    // Field methods
    FixMessageBuilder& field(uint32_t tag, const std::string& value);
    FixMessageBuilder& field(uint32_t tag, double value, int precision = 6);
    FixMessageBuilder& field(uint32_t tag, int64_t value);
    FixMessageBuilder& field(uint32_t tag, uint64_t value);
    
    // Common order fields
    FixMessageBuilder& cl_ord_id(const std::string& client_order_id);
    FixMessageBuilder& order_id(const std::string& order_id);
    FixMessageBuilder& symbol(const std::string& symbol);
    FixMessageBuilder& side(char side);  // '1' = Buy, '2' = Sell
    FixMessageBuilder& order_qty(uint64_t quantity);
    FixMessageBuilder& price(double price, int precision = 6);
    FixMessageBuilder& ord_type(char type);  // '1' = Market, '2' = Limit, etc.
    FixMessageBuilder& time_in_force(char tif);  // '0' = Day, '1' = GTC, etc.
    
    // Build and serialize
    FixMessage build();
    std::string to_string();
    
    // Reset for reuse
    void reset();
    
private:
    std::string current_timestamp();
    void calculate_and_set_body_length();
    void calculate_and_set_checksum();
};

// ============================================================================
// Utility Functions
// ============================================================================

// Convert FIX message to/from wire format
std::string serialize_fix_message(const FixMessage& message);
bool deserialize_fix_message(const std::string& raw_message, FixMessage& message);

// FIX protocol utilities
std::string generate_fix_timestamp();
std::string generate_fix_timestamp(const core::TimePoint& time);
core::TimePoint parse_fix_timestamp(const std::string& timestamp);

// Message type helpers
bool is_admin_message(const std::string& msg_type);
bool is_application_message(const std::string& msg_type);
bool is_order_message(const std::string& msg_type);
bool is_execution_report(const std::string& msg_type);

} // namespace fix
} // namespace hft