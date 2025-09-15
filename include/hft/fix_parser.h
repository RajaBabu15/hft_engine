#pragma once

#include "hft/order.h"
#include "hft/lockfree_queue.h"
#include "hft/latency_metrics.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <string_view>
#include <sstream>

namespace hft {

// FIX 4.4 Message Types
enum class FixMessageType : char {
    HEARTBEAT = '0',
    TEST_REQUEST = '1', 
    RESEND_REQUEST = '2',
    REJECT = '3',
    SEQUENCE_RESET = '4',
    LOGOUT = '5',
    LOGON = 'A',
    NEW_ORDER_SINGLE = 'D',
    ORDER_CANCEL_REQUEST = 'F',
    ORDER_CANCEL_REPLACE_REQUEST = 'G',
    EXECUTION_REPORT = '8',
    ORDER_CANCEL_REJECT = '9',
    MARKET_DATA_REQUEST = 'V',
    MARKET_DATA_SNAPSHOT = 'W',
    MARKET_DATA_INCREMENTAL_REFRESH = 'X'
};

// FIX Field Tags (FIX 4.4)
enum class FixTag : int {
    BEGIN_STRING = 8,
    BODY_LENGTH = 9,
    MSG_TYPE = 35,
    SENDER_COMP_ID = 49,
    TARGET_COMP_ID = 56,
    MSG_SEQ_NUM = 34,
    SENDING_TIME = 52,
    CHECKSUM = 10,
    // Order fields
    CL_ORD_ID = 11,
    SYMBOL = 55,
    SIDE = 54,
    ORDER_QTY = 38,
    ORD_TYPE = 40,
    PRICE = 44,
    TIME_IN_FORCE = 59,
    TRANSACT_TIME = 60,
    // Execution Report fields
    ORDER_ID = 37,
    EXEC_ID = 17,
    EXEC_TYPE = 150,
    ORD_STATUS = 39,
    LEAVES_QTY = 151,
    CUM_QTY = 14,
    AVG_PX = 6,
    LAST_SHARES = 32,
    LAST_PX = 31
};

// Memory pool for FIX field allocation
class FixFieldPool {
private:
    static constexpr size_t POOL_SIZE = 10000;
    static constexpr size_t FIELD_SIZE = 128;
    
    struct alignas(64) FieldNode {
        char data[FIELD_SIZE];
        FieldNode* next;
    };
    
    alignas(64) std::atomic<FieldNode*> free_list_{nullptr};
    std::unique_ptr<FieldNode[]> pool_;
    
public:
    FixFieldPool() : pool_(std::make_unique<FieldNode[]>(POOL_SIZE)) {
        // Initialize free list
        for (size_t i = 0; i < POOL_SIZE - 1; ++i) {
            pool_[i].next = &pool_[i + 1];
        }
        pool_[POOL_SIZE - 1].next = nullptr;
        free_list_.store(&pool_[0], std::memory_order_relaxed);
    }
    
    char* acquire() noexcept {
        FieldNode* node = free_list_.load(std::memory_order_relaxed);
        while (node != nullptr) {
            if (free_list_.compare_exchange_weak(node, node->next, std::memory_order_relaxed)) {
                return node->data;
            }
        }
        // Fallback to heap allocation if pool exhausted
        return new char[FIELD_SIZE];
    }
    
    void release(char* ptr) noexcept {
        if (ptr >= reinterpret_cast<char*>(pool_.get()) && 
            ptr < reinterpret_cast<char*>(pool_.get() + POOL_SIZE)) {
            // Pool allocation - return to free list
            FieldNode* node = reinterpret_cast<FieldNode*>(ptr - offsetof(FieldNode, data));
            FieldNode* head = free_list_.load(std::memory_order_relaxed);
            do {
                node->next = head;
            } while (!free_list_.compare_exchange_weak(head, node, std::memory_order_relaxed));
        } else {
            // Heap allocation
            delete[] ptr;
        }
    }
};

// FIX message structure for high-performance parsing
struct ParsedFixMessage {
    FixMessageType msg_type;
    std::unordered_map<int, std::string_view> fields;
    uint64_t timestamp;
    uint32_t msg_seq_num;
    bool valid;
    
    ParsedFixMessage() : msg_type(FixMessageType::HEARTBEAT), timestamp(0), msg_seq_num(0), valid(false) {}
    
    std::string_view get_field(FixTag tag) const {
        auto it = fields.find(static_cast<int>(tag));
        return it != fields.end() ? it->second : std::string_view{};
    }
    
    bool has_field(FixTag tag) const {
        return fields.find(static_cast<int>(tag)) != fields.end();
    }
};

// Enhanced FIX 4.4 Parser with multithreading support
class FIXParser {
private:
    static constexpr char SOH = '\x01'; // Start of Header delimiter
    
    std::unique_ptr<FixFieldPool> field_pool_;
    LatencyTracker* latency_tracker_;
    
    // Thread pool for parsing
    std::vector<std::thread> parser_threads_;
    std::atomic<bool> running_{false};
    FixMessageQueue input_queue_;
    LockFreeQueue<ParsedFixMessage, 8192> output_queue_;
    
    // Parse a single FIX field (tag=value)
    bool parse_field(std::string_view data, size_t& pos, int& tag, std::string_view& value) const noexcept {
        if (pos >= data.size()) return false;
        
        // Parse tag
        size_t eq_pos = data.find('=', pos);
        if (eq_pos == std::string_view::npos) return false;
        
        std::string_view tag_str = data.substr(pos, eq_pos - pos);
        tag = 0;
        for (char c : tag_str) {
            if (c < '0' || c > '9') return false;
            tag = tag * 10 + (c - '0');
        }
        
        // Parse value
        size_t soh_pos = data.find(SOH, eq_pos + 1);
        if (soh_pos == std::string_view::npos) {
            soh_pos = data.size();
        }
        
        value = data.substr(eq_pos + 1, soh_pos - eq_pos - 1);
        pos = soh_pos + 1;
        
        return true;
    }
    
    // Validate FIX message checksum
    bool validate_checksum(std::string_view message) const noexcept {
        if (message.size() < 7) return false; // Minimum: "10=xxx\x01"
        
        // Find checksum field (last field)
        size_t checksum_pos = message.rfind("10=");
        if (checksum_pos == std::string_view::npos) return false;
        
        // Calculate checksum of message up to checksum field
        uint32_t calculated_checksum = 0;
        for (size_t i = 0; i < checksum_pos; ++i) {
            calculated_checksum += static_cast<uint8_t>(message[i]);
        }
        calculated_checksum %= 256;
        
        // Parse expected checksum
        size_t soh_pos = message.find(SOH, checksum_pos);
        if (soh_pos == std::string_view::npos) return false;
        
        std::string_view checksum_str = message.substr(checksum_pos + 3, soh_pos - checksum_pos - 3);
        if (checksum_str.size() != 3) return false;
        
        uint32_t expected_checksum = 0;
        for (char c : checksum_str) {
            if (c < '0' || c > '9') return false;
            expected_checksum = expected_checksum * 10 + (c - '0');
        }
        
        return calculated_checksum == expected_checksum;
    }
    
    // Parser thread function
    void parser_thread_func(int thread_id) {
        FixMessage<> raw_msg;
        ParsedFixMessage parsed_msg;
        
        while (running_.load(std::memory_order_relaxed)) {
            if (input_queue_.try_dequeue(raw_msg)) {
                auto latency_measure = latency_tracker_ ? 
                    latency_tracker_->measure(LatencyPoint::ORDER_VALIDATION) :
                    LatencyTracker::LatencyMeasurement(nullptr, LatencyPoint::ORDER_VALIDATION);
                
                parse_message_internal(std::string_view(raw_msg.data, raw_msg.length), parsed_msg);
                
                if (parsed_msg.valid) {
                    // Try to enqueue parsed message
                    while (running_.load(std::memory_order_relaxed) && 
                           !output_queue_.try_enqueue(std::move(parsed_msg))) {
                        std::this_thread::yield();
                    }
                }
            } else {
                std::this_thread::yield();
            }
        }
    }
    
    void parse_message_internal(std::string_view message, ParsedFixMessage& result) const {
        result.valid = false;
        result.fields.clear();
        result.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        if (message.empty() || !validate_checksum(message)) {
            return;
        }
        
        size_t pos = 0;
        int tag;
        std::string_view value;
        
        // Parse all fields
        while (parse_field(message, pos, tag, value)) {
            result.fields[tag] = value;
            
            // Extract key fields
            if (tag == static_cast<int>(FixTag::MSG_TYPE) && !value.empty()) {
                result.msg_type = static_cast<FixMessageType>(value[0]);
            } else if (tag == static_cast<int>(FixTag::MSG_SEQ_NUM)) {
                result.msg_seq_num = 0;
                for (char c : value) {
                    if (c >= '0' && c <= '9') {
                        result.msg_seq_num = result.msg_seq_num * 10 + (c - '0');
                    }
                }
            }
        }
        
        result.valid = !result.fields.empty();
    }

public:
    FIXParser(size_t num_threads = 4, LatencyTracker* tracker = nullptr) 
        : field_pool_(std::make_unique<FixFieldPool>())
        , latency_tracker_(tracker)
    {
        start_parsing_threads(num_threads);
    }
    
    ~FIXParser() {
        stop_parsing_threads();
    }
    
    void start_parsing_threads(size_t num_threads) {
        if (running_.load(std::memory_order_relaxed)) return;
        
        running_.store(true, std::memory_order_relaxed);
        parser_threads_.reserve(num_threads);
        
        for (size_t i = 0; i < num_threads; ++i) {
            parser_threads_.emplace_back(&FIXParser::parser_thread_func, this, static_cast<int>(i));
        }
    }
    
    void stop_parsing_threads() {
        running_.store(false, std::memory_order_relaxed);
        
        for (auto& thread : parser_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        parser_threads_.clear();
    }
    
    // Submit raw FIX message for parsing
    bool submit_for_parsing(const char* data, size_t length) {
        FixMessage<> msg;
        if (length >= sizeof(msg.data)) return false;
        
        std::memcpy(msg.data, data, length);
        msg.length = length;
        msg.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        
        return input_queue_.try_enqueue(std::move(msg));
    }
    
    // Get parsed FIX message (non-blocking)
    bool get_parsed_message(ParsedFixMessage& message) {
        return output_queue_.try_dequeue(message);
    }
    
    // Synchronous parsing (for backward compatibility)
    Order parse_order(const std::string& fix_msg) {
        ParsedFixMessage msg;
        parse_message_internal(fix_msg, msg);
        return convert_to_order(msg);
    }
    
    // Convert FIX message to Order
    Order convert_to_order(const ParsedFixMessage& fix_msg) const {
        Order order;
        
        if (!fix_msg.valid || fix_msg.msg_type != FixMessageType::NEW_ORDER_SINGLE) {
            return order; // Invalid order
        }
        
        // Extract order fields
        auto symbol_field = fix_msg.get_field(FixTag::SYMBOL);
        auto side_field = fix_msg.get_field(FixTag::SIDE);
        auto qty_field = fix_msg.get_field(FixTag::ORDER_QTY);
        auto price_field = fix_msg.get_field(FixTag::PRICE);
        auto ord_type_field = fix_msg.get_field(FixTag::ORD_TYPE);
        auto cl_ord_id_field = fix_msg.get_field(FixTag::CL_ORD_ID);
        
        // Parse symbol (hash for fast lookup)
        Symbol symbol_id = 1; // Default
        if (!symbol_field.empty()) {
            symbol_id = std::hash<std::string_view>{}(symbol_field);
        }
        
        // Parse side
        Side side = Side::BUY;
        if (!side_field.empty()) {
            side = (side_field[0] == '1') ? Side::BUY : Side::SELL;
        }
        
        // Parse quantity
        Quantity qty = 0;
        for (char c : qty_field) {
            if (c >= '0' && c <= '9') {
                qty = qty * 10 + (c - '0');
            }
        }
        
        // Parse price (assuming integer representation with implicit decimals)
        Price price = 0;
        bool decimal_found = false;
        int decimal_places = 0;
        for (char c : price_field) {
            if (c == '.') {
                decimal_found = true;
            } else if (c >= '0' && c <= '9') {
                price = price * 10 + (c - '0');
                if (decimal_found) decimal_places++;
            }
        }
        // Normalize to fixed precision (e.g., 4 decimal places)
        while (decimal_places < 4) {
            price *= 10;
            decimal_places++;
        }
        
        // Parse order type
        OrderType ord_type = OrderType::LIMIT;
        if (!ord_type_field.empty()) {
            if (ord_type_field[0] == '1') ord_type = OrderType::MARKET;
            else if (ord_type_field[0] == '2') ord_type = OrderType::LIMIT;
        }
        
        // Parse client order ID
        OrderId order_id = 0;
        for (char c : cl_ord_id_field) {
            if (c >= '0' && c <= '9') {
                order_id = order_id * 10 + (c - '0');
            }
        }
        
        order.init(order_id, symbol_id, side, ord_type, price, qty);
        return order;
    }
    
    // Get parsing statistics
    struct ParsingStats {
        size_t messages_pending = 0;
        size_t messages_parsed = 0;
        bool threads_running = false;
    };
    
    ParsingStats get_stats() const {
        ParsingStats stats;
        stats.messages_pending = input_queue_.size();
        stats.messages_parsed = output_queue_.size();
        stats.threads_running = running_.load(std::memory_order_relaxed);
        return stats;
    }
};

} // namespace hft 