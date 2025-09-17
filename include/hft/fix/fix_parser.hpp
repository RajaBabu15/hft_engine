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
#include <mutex>
namespace hft {
namespace fix {
constexpr char FIX_SOH = '\001';
constexpr char FIX_EQUALS = '=';
constexpr size_t MAX_FIX_MESSAGE_SIZE = 8192;
namespace Tags {
    constexpr uint32_t BEGIN_STRING = 8;
    constexpr uint32_t BODY_LENGTH = 9;
    constexpr uint32_t CHECK_SUM = 10;
    constexpr uint32_t MSG_SEQ_NUM = 34;
    constexpr uint32_t MSG_TYPE = 35;
    constexpr uint32_t SENDER_COMP_ID = 49;
    constexpr uint32_t SENDING_TIME = 52;
    constexpr uint32_t TARGET_COMP_ID = 56;
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
    std::vector<FixField> ordered_fields;
    bool has_field(uint32_t tag) const;
    const std::string& get_field(uint32_t tag) const;
    void set_field(uint32_t tag, const std::string& value);
    void set_field(uint32_t tag, std::string&& value);
    double get_price(uint32_t tag) const;
    uint64_t get_quantity(uint32_t tag) const;
    int get_int(uint32_t tag) const;
    bool is_valid() const;
    uint8_t calculate_checksum() const;
    void clear();
};
struct FixParserStats {
    std::atomic<uint64_t> messages_parsed{0};
    std::atomic<uint64_t> parse_errors{0};
    std::atomic<uint64_t> checksum_errors{0};
    std::atomic<uint64_t> invalid_messages{0};
    std::atomic<uint64_t> bytes_processed{0};
    std::atomic<double> avg_parse_time_ns{0.0};
    std::atomic<uint64_t> queue_full_events{0};
    std::atomic<uint64_t> messages_dropped{0};
    std::atomic<uint64_t> callback_errors{0};
    void reset() {
        messages_parsed = 0;
        parse_errors = 0;
        checksum_errors = 0;
        invalid_messages = 0;
        bytes_processed = 0;
        avg_parse_time_ns = 0.0;
        queue_full_events = 0;
        messages_dropped = 0;
        callback_errors = 0;
    }
};
class FixParser {
public:
    using MessageCallback = std::function<void(const FixMessage&)>;
    using ErrorCallback = std::function<void(const std::string&, const std::string&)>;
private:
    static constexpr size_t PARSER_QUEUE_SIZE = 16384;  // Increased for high throughput
    static constexpr size_t MAX_WORKER_THREADS = 4;    // Conservative limit to prevent resource exhaustion
    std::atomic<bool> running_{false};
    size_t num_worker_threads_;
    std::vector<std::thread> worker_threads_;
    std::unique_ptr<core::LockFreeQueue<std::string, PARSER_QUEUE_SIZE>> raw_message_queue_;
    std::unique_ptr<core::LockFreeQueue<FixMessage, PARSER_QUEUE_SIZE>> parsed_message_queue_;
    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    FixParserStats stats_;
    std::string message_buffer_;
    size_t buffer_position_;
    mutable std::mutex buffer_mutex_;
    mutable std::mutex callback_mutex_;
public:
    explicit FixParser(size_t num_workers = 4);
    ~FixParser();
    FixParser(const FixParser&) = delete;
    FixParser& operator=(const FixParser&) = delete;
    FixParser(FixParser&&) = delete;
    FixParser& operator=(FixParser&&) = delete;
    void set_message_callback(MessageCallback callback);
    void set_error_callback(ErrorCallback callback);
    void start();
    void stop();
    bool is_running() const { return running_.load(); }
    void feed_data(const char* data, size_t length);
    void feed_data(const std::string& data);
    bool parse_message(const std::string& raw_message, FixMessage& parsed_message);
    const FixParserStats& get_stats() const { return stats_; }
    void reset_stats() { stats_.reset(); }
    void set_worker_threads(size_t count);
    size_t get_worker_threads() const { return num_worker_threads_; }
private:
    void parsing_worker();
    void processing_worker();
    bool extract_next_message(std::string& message);
    bool parse_message_internal(const std::string& raw_message, FixMessage& message);
    bool parse_field(const std::string& field_str, uint32_t& tag, std::string& value);
    bool validate_message_structure(const FixMessage& message);
    uint8_t calculate_checksum(const std::string& message_without_checksum);
    void update_parse_time(double parse_time_ns);
};
class FixMessageBuilder {
private:
    FixMessage message_;
    std::string sender_comp_id_;
    std::string target_comp_id_;
    uint32_t next_seq_num_;
public:
    explicit FixMessageBuilder(const std::string& sender_comp_id, const std::string& target_comp_id);
    FixMessageBuilder& begin_string(const std::string& version = "FIX.4.4");
    FixMessageBuilder& msg_type(const std::string& type);
    FixMessageBuilder& msg_seq_num(uint32_t seq_num);
    FixMessageBuilder& sending_time();
    FixMessageBuilder& sending_time(const std::string& time);
    FixMessageBuilder& field(uint32_t tag, const std::string& value);
    FixMessageBuilder& field(uint32_t tag, double value, int precision = 6);
    FixMessageBuilder& field(uint32_t tag, int64_t value);
    FixMessageBuilder& field(uint32_t tag, uint64_t value);
    FixMessageBuilder& cl_ord_id(const std::string& client_order_id);
    FixMessageBuilder& order_id(const std::string& order_id);
    FixMessageBuilder& symbol(const std::string& symbol);
    FixMessageBuilder& side(char side);
    FixMessageBuilder& order_qty(uint64_t quantity);
    FixMessageBuilder& price(double price, int precision = 6);
    FixMessageBuilder& ord_type(char type);
    FixMessageBuilder& time_in_force(char tif);
    FixMessage build();
    std::string to_string();
    void reset();
private:
    std::string current_timestamp();
    void calculate_and_set_body_length();
    void calculate_and_set_checksum();
};
std::string serialize_fix_message(const FixMessage& message);
bool deserialize_fix_message(const std::string& raw_message, FixMessage& message);
std::string generate_fix_timestamp();
std::string generate_fix_timestamp(const core::TimePoint& time);
core::TimePoint parse_fix_timestamp(const std::string& timestamp);
bool is_admin_message(const std::string& msg_type);
bool is_application_message(const std::string& msg_type);
bool is_order_message(const std::string& msg_type);
bool is_execution_report(const std::string& msg_type);
}
}
