#pragma once

#include "hft/order.h"
#include "hft/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>

namespace hft {

// FIX message types
enum class FIXMsgType {
    HEARTBEAT = 0,
    TEST_REQUEST = 1,
    RESEND_REQUEST = 2,
    REJECT = 3,
    SEQUENCE_RESET = 4,
    LOGOUT = 5,
    LOGON = 'A',
    NEW_ORDER_SINGLE = 'D',
    ORDER_CANCEL_REQUEST = 'F',
    ORDER_CANCEL_REPLACE_REQUEST = 'G',
    EXECUTION_REPORT = '8',
    ORDER_CANCEL_REJECT = '9',
    MARKET_DATA_REQUEST = 'V',
    MARKET_DATA_SNAPSHOT = 'W',
    MARKET_DATA_INCREMENTAL_REFRESH = 'X',
    SECURITY_DEFINITION_REQUEST = 'c',
    SECURITY_DEFINITION = 'd',
    BUSINESS_MESSAGE_REJECT = 'j'
};

// FIX session state
enum class FIXSessionState {
    DISCONNECTED,
    LOGGING_ON,
    LOGGED_ON,
    LOGGING_OUT,
    ERROR_STATE
};

// FIX message structure
struct FIXMessage {
    std::string raw_message;
    std::unordered_map<int, std::string> fields;
    FIXMsgType msg_type = FIXMsgType::HEARTBEAT;
    uint64_t timestamp_ns = 0;
    uint32_t msg_seq_num = 0;
    std::string sender_comp_id;
    std::string target_comp_id;
    bool is_valid = false;
    std::string error_reason;
    
    FIXMessage() = default;
    FIXMessage(const std::string& raw) : raw_message(raw), timestamp_ns(now_ns()) {}
    
    // Field accessors
    std::string get_field(int tag) const {
        auto it = fields.find(tag);
        return it != fields.end() ? it->second : "";
    }
    
    void set_field(int tag, const std::string& value) {
        fields[tag] = value;
    }
    
    bool has_field(int tag) const {
        return fields.find(tag) != fields.end();
    }
};

// FIX session management
class FIXSession {
private:
    std::string sender_comp_id_;
    std::string target_comp_id_;
    FIXSessionState state_ = FIXSessionState::DISCONNECTED;
    std::atomic<uint32_t> outgoing_seq_num_{1};
    std::atomic<uint32_t> incoming_seq_num_{1};
    std::atomic<uint64_t> last_heartbeat_time_{0};
    std::atomic<uint64_t> heartbeat_interval_ms_{30000}; // 30 seconds
    
    mutable std::mutex session_mutex_;
    
public:
    FIXSession(const std::string& sender, const std::string& target)
        : sender_comp_id_(sender), target_comp_id_(target) {
        last_heartbeat_time_.store(now_ns() / 1000000, std::memory_order_relaxed);
    }
    
    uint32_t get_next_outgoing_seq_num() {
        return outgoing_seq_num_.fetch_add(1, std::memory_order_acq_rel);
    }
    
    bool validate_incoming_seq_num(uint32_t seq_num) {
        uint32_t expected = incoming_seq_num_.load(std::memory_order_acquire);
        if (seq_num == expected) {
            incoming_seq_num_.store(expected + 1, std::memory_order_release);
            return true;
        }
        return false;
    }
    
    FIXSessionState get_state() const {
        std::lock_guard<std::mutex> lock(session_mutex_);
        return state_;
    }
    
    void set_state(FIXSessionState state) {
        std::lock_guard<std::mutex> lock(session_mutex_);
        state_ = state;
    }
    
    bool needs_heartbeat() const {
        uint64_t now = now_ns() / 1000000;
        uint64_t last = last_heartbeat_time_.load(std::memory_order_relaxed);
        return (now - last) > heartbeat_interval_ms_.load(std::memory_order_relaxed);
    }
    
    void update_heartbeat_time() {
        last_heartbeat_time_.store(now_ns() / 1000000, std::memory_order_relaxed);
    }
    
    const std::string& get_sender_comp_id() const { return sender_comp_id_; }
    const std::string& get_target_comp_id() const { return target_comp_id_; }
};

// High-performance FIX parser with multithreading support
class FIXParser {
public:
    using MessageHandler = std::function<void(const FIXMessage&)>;
    using ErrorHandler = std::function<void(const std::string&, const std::string&)>;
    
    struct ParserConfig {
        uint32_t num_parser_threads = 4;
        uint32_t message_buffer_size = 10000;
        bool enable_message_validation = true;
        bool enable_checksum_validation = true;
        bool enable_sequence_validation = true;
        std::string fix_version = "FIX.4.4";
        uint32_t max_message_length = 8192;
    };
    
private:
    ParserConfig config_;
    std::vector<std::thread> parser_threads_;
    std::atomic<bool> running_{false};
    
    // Message queues for each parser thread
    std::vector<std::queue<std::string>> message_queues_;
    std::vector<std::mutex> queue_mutexes_;
    std::vector<std::condition_variable> queue_conditions_;
    
    // Handler functions
    std::unordered_map<FIXMsgType, MessageHandler> message_handlers_;
    ErrorHandler error_handler_;
    
    // Session management
    std::unordered_map<std::string, std::unique_ptr<FIXSession>> sessions_;
    std::mutex sessions_mutex_;
    
    // Performance statistics
    std::atomic<uint64_t> messages_parsed_{0};
    std::atomic<uint64_t> parse_errors_{0};
    std::atomic<uint64_t> validation_errors_{0};
    
    // FIX field definitions
    static const std::unordered_map<int, std::string> field_names_;
    
public:
    explicit FIXParser(const ParserConfig& config = ParserConfig{}) 
        : config_(config),
          message_queues_(config.num_parser_threads),
          queue_mutexes_(config.num_parser_threads),
          queue_conditions_(config.num_parser_threads) {
        
        // Initialize default message handlers
        setup_default_handlers();
    }
    
    ~FIXParser() {
        stop();
    }
    
    bool start() {
        if (running_.load(std::memory_order_acquire)) {
            return false; // Already running
        }
        
        running_.store(true, std::memory_order_release);
        
        // Start parser threads
        parser_threads_.reserve(config_.num_parser_threads);
        for (uint32_t i = 0; i < config_.num_parser_threads; ++i) {
            parser_threads_.emplace_back([this, i]() {
                parser_thread_main(i);
            });
        }
        
        std::cout << "🔧 FIX Parser started with " << config_.num_parser_threads 
                  << " threads" << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_.load(std::memory_order_acquire)) {
            return; // Not running
        }
        
        running_.store(false, std::memory_order_release);
        
        // Wake up all parser threads
        for (auto& condition : queue_conditions_) {
            condition.notify_all();
        }
        
        // Wait for threads to finish
        for (auto& thread : parser_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        parser_threads_.clear();
        std::cout << "🔧 FIX Parser stopped" << std::endl;
    }
    
    // Submit message for parsing (thread-safe)
    bool submit_message(const std::string& raw_message) {
        if (!running_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Load balance across parser threads using simple round-robin
        static std::atomic<uint32_t> round_robin_counter{0};
        uint32_t thread_id = round_robin_counter.fetch_add(1, std::memory_order_relaxed) 
                            % config_.num_parser_threads;
        
        std::unique_lock<std::mutex> lock(queue_mutexes_[thread_id]);
        if (message_queues_[thread_id].size() >= config_.message_buffer_size) {
            return false; // Queue full
        }
        
        message_queues_[thread_id].push(raw_message);
        lock.unlock();
        
        queue_conditions_[thread_id].notify_one();
        return true;
    }
    
    // Register message handler
    void set_message_handler(FIXMsgType msg_type, MessageHandler handler) {
        message_handlers_[msg_type] = handler;
    }
    
    void set_error_handler(ErrorHandler handler) {
        error_handler_ = handler;
    }
    
    // Session management
    std::shared_ptr<FIXSession> create_session(const std::string& sender_comp_id,
                                               const std::string& target_comp_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        std::string session_key = sender_comp_id + "-" + target_comp_id;
        
        auto session = std::make_unique<FIXSession>(sender_comp_id, target_comp_id);
        auto session_ptr = std::shared_ptr<FIXSession>(session.get());
        sessions_[session_key] = std::move(session);
        
        return session_ptr;
    }
    
    std::shared_ptr<FIXSession> get_session(const std::string& sender_comp_id,
                                            const std::string& target_comp_id) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        std::string session_key = sender_comp_id + "-" + target_comp_id;
        
        auto it = sessions_.find(session_key);
        if (it != sessions_.end()) {
            return std::shared_ptr<FIXSession>(it->second.get());
        }
        return nullptr;
    }
    
    // Synchronous parsing for single messages
    FIXMessage parse_message(const std::string& raw_message) {
        FIXMessage msg(raw_message);
        parse_message_internal(msg);
        return msg;
    }
    
    // Convert Order to FIX NewOrderSingle message
    std::string create_new_order_single(const Order& order, 
                                        const std::string& sender_comp_id,
                                        const std::string& target_comp_id) {
        auto session = get_session(sender_comp_id, target_comp_id);
        if (!session) {
            session = create_session(sender_comp_id, target_comp_id);
        }
        
        std::stringstream ss;
        
        // Header fields
        ss << "8=" << config_.fix_version << "\001";  // BeginString
        ss << "35=D\001";                              // MsgType (NewOrderSingle)
        ss << "49=" << sender_comp_id << "\001";       // SenderCompID
        ss << "56=" << target_comp_id << "\001";       // TargetCompID
        ss << "34=" << session->get_next_outgoing_seq_num() << "\001"; // MsgSeqNum
        ss << "52=" << get_utc_timestamp() << "\001";   // SendingTime
        
        // Order fields
        ss << "11=" << order.id << "\001";              // ClOrdID
        ss << "55=" << order.symbol << "\001";          // Symbol
        ss << "54=" << (order.side == Side::BUY ? "1" : "2") << "\001"; // Side
        ss << "38=" << order.qty << "\001";             // OrderQty
        ss << "40=" << (order.type == OrderType::LIMIT ? "2" : "1") << "\001"; // OrdType
        
        if (order.type == OrderType::LIMIT) {
            ss << "44=" << (order.price / 10000.0) << "\001"; // Price
        }
        
        ss << "59=" << (order.tif == TimeInForce::GTC ? "1" : "3") << "\001"; // TimeInForce
        
        std::string body = ss.str();
        
        // Calculate body length and checksum
        std::stringstream final_msg;
        final_msg << "9=" << (body.length() - 2) << "\001"; // BodyLength (exclude 8= and 9= fields)
        final_msg << body;
        
        std::string msg_without_checksum = final_msg.str();
        uint32_t checksum = calculate_checksum(msg_without_checksum);
        final_msg << "10=" << std::setfill('0') << std::setw(3) << (checksum % 256) << "\001";
        
        return final_msg.str();
    }
    
    // Performance statistics
    uint64_t get_messages_parsed() const {
        return messages_parsed_.load(std::memory_order_relaxed);
    }
    
    uint64_t get_parse_errors() const {
        return parse_errors_.load(std::memory_order_relaxed);
    }
    
    uint64_t get_validation_errors() const {
        return validation_errors_.load(std::memory_order_relaxed);
    }
    
    void reset_statistics() {
        messages_parsed_.store(0, std::memory_order_relaxed);
        parse_errors_.store(0, std::memory_order_relaxed);
        validation_errors_.store(0, std::memory_order_relaxed);
    }
    
private:
    void parser_thread_main(uint32_t thread_id) {
        while (running_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lock(queue_mutexes_[thread_id]);
            
            // Wait for messages or shutdown signal
            queue_conditions_[thread_id].wait(lock, [this, thread_id]() {
                return !message_queues_[thread_id].empty() || 
                       !running_.load(std::memory_order_acquire);
            });
            
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            
            // Process all available messages
            while (!message_queues_[thread_id].empty()) {
                std::string raw_message = message_queues_[thread_id].front();
                message_queues_[thread_id].pop();
                lock.unlock();
                
                // Parse and handle message
                FIXMessage msg(raw_message);
                parse_message_internal(msg);
                
                if (msg.is_valid) {
                    handle_parsed_message(msg);
                    messages_parsed_.fetch_add(1, std::memory_order_relaxed);
                } else {
                    parse_errors_.fetch_add(1, std::memory_order_relaxed);
                    if (error_handler_) {
                        error_handler_("Parse Error", msg.error_reason);
                    }
                }
                
                lock.lock();
            }
        }
    }
    
    void parse_message_internal(FIXMessage& msg) {
        try {
            // Basic format validation
            if (msg.raw_message.length() < 20 || msg.raw_message.length() > config_.max_message_length) {
                msg.error_reason = "Invalid message length";
                return;
            }
            
            // Parse fields
            if (!parse_fields(msg)) {
                return;
            }
            
            // Validate required header fields
            if (!validate_header(msg)) {
                return;
            }
            
            // Validate checksum if enabled
            if (config_.enable_checksum_validation && !validate_checksum(msg)) {
                msg.error_reason = "Invalid checksum";
                return;
            }
            
            // Determine message type
            std::string msg_type_str = msg.get_field(35); // MsgType
            if (msg_type_str.empty()) {
                msg.error_reason = "Missing MsgType field";
                return;
            }
            
            msg.msg_type = string_to_msg_type(msg_type_str);
            msg.sender_comp_id = msg.get_field(49);
            msg.target_comp_id = msg.get_field(56);
            
            std::string seq_num_str = msg.get_field(34);
            if (!seq_num_str.empty()) {
                msg.msg_seq_num = std::stoul(seq_num_str);
            }
            
            msg.is_valid = true;
            
        } catch (const std::exception& e) {
            msg.error_reason = "Parse exception: " + std::string(e.what());
        }
    }
    
    bool parse_fields(FIXMessage& msg) {
        const std::string& raw = msg.raw_message;
        size_t pos = 0;
        
        while (pos < raw.length()) {
            // Find tag
            size_t eq_pos = raw.find('=', pos);
            if (eq_pos == std::string::npos) {
                msg.error_reason = "Invalid field format: missing '='";
                return false;
            }
            
            // Extract tag
            std::string tag_str = raw.substr(pos, eq_pos - pos);
            int tag;
            try {
                tag = std::stoi(tag_str);
            } catch (const std::exception&) {
                msg.error_reason = "Invalid tag: " + tag_str;
                return false;
            }
            
            // Find value
            pos = eq_pos + 1;
            size_t soh_pos = raw.find('\001', pos); // SOH delimiter
            if (soh_pos == std::string::npos) {
                soh_pos = raw.length(); // Last field
            }
            
            std::string value = raw.substr(pos, soh_pos - pos);
            msg.set_field(tag, value);
            
            pos = soh_pos + 1;
        }
        
        return true;
    }
    
    bool validate_header(const FIXMessage& msg) {
        // Check required header fields
        if (!msg.has_field(8) ||   // BeginString
            !msg.has_field(9) ||   // BodyLength
            !msg.has_field(35) ||  // MsgType
            !msg.has_field(49) ||  // SenderCompID
            !msg.has_field(56) ||  // TargetCompID
            !msg.has_field(34) ||  // MsgSeqNum
            !msg.has_field(52)) {  // SendingTime
            return false;
        }
        
        // Validate FIX version
        if (msg.get_field(8) != config_.fix_version) {
            return false;
        }
        
        return true;
    }
    
    bool validate_checksum(const FIXMessage& msg) {
        if (!msg.has_field(10)) {
            return false;
        }
        
        // Find checksum field position
        size_t checksum_pos = msg.raw_message.rfind("10=");
        if (checksum_pos == std::string::npos) {
            return false;
        }
        
        std::string msg_without_checksum = msg.raw_message.substr(0, checksum_pos);
        uint32_t calculated = calculate_checksum(msg_without_checksum);
        uint32_t received = std::stoul(msg.get_field(10));
        
        return (calculated % 256) == received;
    }
    
    uint32_t calculate_checksum(const std::string& message) {
        uint32_t sum = 0;
        for (char c : message) {
            sum += static_cast<uint8_t>(c);
        }
        return sum;
    }
    
    FIXMsgType string_to_msg_type(const std::string& msg_type_str) {
        if (msg_type_str.length() == 1) {
            return static_cast<FIXMsgType>(msg_type_str[0]);
        } else if (msg_type_str == "0") {
            return FIXMsgType::HEARTBEAT;
        } else if (msg_type_str == "1") {
            return FIXMsgType::TEST_REQUEST;
        } else if (msg_type_str == "2") {
            return FIXMsgType::RESEND_REQUEST;
        } else if (msg_type_str == "3") {
            return FIXMsgType::REJECT;
        } else if (msg_type_str == "4") {
            return FIXMsgType::SEQUENCE_RESET;
        } else if (msg_type_str == "5") {
            return FIXMsgType::LOGOUT;
        }
        
        return FIXMsgType::HEARTBEAT; // Default
    }
    
    void handle_parsed_message(const FIXMessage& msg) {
        auto handler_it = message_handlers_.find(msg.msg_type);
        if (handler_it != message_handlers_.end()) {
            handler_it->second(msg);
        } else {
            // Default handler - convert to appropriate type
            handle_default_message(msg);
        }
    }
    
    void handle_default_message(const FIXMessage& msg) {
        switch (msg.msg_type) {
            case FIXMsgType::NEW_ORDER_SINGLE:
                handle_new_order_single(msg);
                break;
            case FIXMsgType::ORDER_CANCEL_REQUEST:
                handle_order_cancel_request(msg);
                break;
            case FIXMsgType::EXECUTION_REPORT:
                handle_execution_report(msg);
                break;
            case FIXMsgType::HEARTBEAT:
                handle_heartbeat(msg);
                break;
            default:
                // Unknown message type - log or ignore
                break;
        }
    }
    
    void handle_new_order_single(const FIXMessage& msg) {
        try {
            Order order;
            order.id = std::stoull(msg.get_field(11));        // ClOrdID
            order.symbol = std::stoul(msg.get_field(55));     // Symbol
            
            std::string side = msg.get_field(54);             // Side
            order.side = (side == "1") ? Side::BUY : Side::SELL;
            
            order.qty = std::stoull(msg.get_field(38));       // OrderQty
            
            std::string ord_type = msg.get_field(40);         // OrdType
            order.type = (ord_type == "2") ? OrderType::LIMIT : OrderType::MARKET;
            
            if (order.type == OrderType::LIMIT && msg.has_field(44)) {
                order.price = static_cast<Price>(std::stod(msg.get_field(44)) * 10000);
            }
            
            std::string tif = msg.get_field(59);              // TimeInForce
            order.tif = (tif == "1") ? TimeInForce::GTC : TimeInForce::IOC;
            
            order.status = OrderStatus::NEW;
            order.user_id = 1; // Default user
            
            // Notify order handler if registered
            auto order_handler = message_handlers_.find(FIXMsgType::NEW_ORDER_SINGLE);
            if (order_handler != message_handlers_.end()) {
                order_handler->second(msg);
            }
            
        } catch (const std::exception& e) {
            if (error_handler_) {
                error_handler_("Order Parse Error", e.what());
            }
        }
    }
    
    void handle_order_cancel_request(const FIXMessage& msg) {
        // Implementation for cancel request handling
    }
    
    void handle_execution_report(const FIXMessage& msg) {
        // Implementation for execution report handling
    }
    
    void handle_heartbeat(const FIXMessage& msg) {
        // Update session heartbeat timestamp
        std::string sender = msg.get_field(49);
        std::string target = msg.get_field(56);
        
        auto session = get_session(sender, target);
        if (session) {
            session->update_heartbeat_time();
        }
    }
    
    void setup_default_handlers() {
        // Set up default message handlers for common message types
        // Applications can override these by calling set_message_handler
    }
    
    std::string get_utc_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        
        return ss.str();
    }
};

// FIX field name definitions
const std::unordered_map<int, std::string> FIXParser::field_names_ = {
    {8, "BeginString"},
    {9, "BodyLength"},
    {10, "CheckSum"},
    {11, "ClOrdID"},
    {34, "MsgSeqNum"},
    {35, "MsgType"},
    {38, "OrderQty"},
    {40, "OrdType"},
    {44, "Price"},
    {49, "SenderCompID"},
    {52, "SendingTime"},
    {54, "Side"},
    {55, "Symbol"},
    {56, "TargetCompID"},
    {59, "TimeInForce"},
    {150, "ExecType"},
    {151, "LeavesQty"},
    {269, "MDEntryType"},
    {270, "MDEntryPx"},
    {271, "MDEntrySize"}
};

}
