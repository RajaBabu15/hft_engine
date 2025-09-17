#include "hft/fix/fix_parser.hpp"
#include "hft/core/clock.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
namespace hft {
namespace fix {
bool FixMessage::has_field(uint32_t tag) const {
    return fields.find(tag) != fields.end();
}
const std::string& FixMessage::get_field(uint32_t tag) const {
    static const std::string empty_string;
    auto it = fields.find(tag);
    return (it != fields.end()) ? it->second : empty_string;
}
void FixMessage::set_field(uint32_t tag, const std::string& value) {
    fields[tag] = value;
    auto it = std::find_if(ordered_fields.begin(), ordered_fields.end(),
        [tag](const FixField& field) { return field.tag == tag; });
    if (it != ordered_fields.end()) {
        it->value = value;
    } else {
        ordered_fields.emplace_back(tag, value);
    }
}
void FixMessage::set_field(uint32_t tag, std::string&& value) {
    auto it = std::find_if(ordered_fields.begin(), ordered_fields.end(),
        [tag](const FixField& field) { return field.tag == tag; });
    if (it != ordered_fields.end()) {
        it->value = value;
        fields[tag] = std::move(value);
    } else {
        ordered_fields.emplace_back(tag, value);
        fields[tag] = std::move(value);
    }
}
double FixMessage::get_price(uint32_t tag) const {
    const auto& value = get_field(tag);
    return value.empty() ? 0.0 : std::stod(value);
}
uint64_t FixMessage::get_quantity(uint32_t tag) const {
    const auto& value = get_field(tag);
    return value.empty() ? 0 : std::stoull(value);
}
int FixMessage::get_int(uint32_t tag) const {
    const auto& value = get_field(tag);
    return value.empty() ? 0 : std::stoi(value);
}
bool FixMessage::is_valid() const {
    return !begin_string.empty() &&
           !msg_type.empty() &&
           !sender_comp_id.empty() &&
           !target_comp_id.empty() &&
           body_length > 0;
}
uint8_t FixMessage::calculate_checksum() const {
    uint32_t sum = 0;
    for (const auto& field : ordered_fields) {
        if (field.tag != Tags::CHECK_SUM) {
            std::string field_str = std::to_string(field.tag) + "=" + field.value + FIX_SOH;
            for (char c : field_str) {
                sum += static_cast<uint8_t>(c);
            }
        }
    }
    return static_cast<uint8_t>(sum % 256);
}
void FixMessage::clear() {
    begin_string.clear();
    body_length = 0;
    msg_type.clear();
    msg_seq_num = 0;
    sender_comp_id.clear();
    target_comp_id.clear();
    sending_time.clear();
    checksum = 0;
    fields.clear();
    ordered_fields.clear();
}
FixParser::FixParser(size_t num_workers)
    : num_worker_threads_(std::min(num_workers, MAX_WORKER_THREADS)),
      buffer_position_(0) {
    raw_message_queue_ = std::make_unique<core::LockFreeQueue<std::string, PARSER_QUEUE_SIZE>>();
    parsed_message_queue_ = std::make_unique<core::LockFreeQueue<FixMessage, PARSER_QUEUE_SIZE>>();
    message_buffer_.reserve(MAX_FIX_MESSAGE_SIZE * 4);
}
FixParser::~FixParser() {
    stop();
}
void FixParser::set_message_callback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    message_callback_ = std::move(callback);
}
void FixParser::set_error_callback(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    error_callback_ = std::move(callback);
}
void FixParser::start() {
    if (running_.load()) return;
    running_.store(true);
    worker_threads_.reserve(num_worker_threads_);
    size_t parsing_workers = std::max(size_t(1), num_worker_threads_ / 3);
    size_t processing_workers = num_worker_threads_ - parsing_workers;
    for (size_t i = 0; i < parsing_workers; ++i) {
        worker_threads_.emplace_back(&FixParser::parsing_worker, this);
    }
    for (size_t i = 0; i < processing_workers; ++i) {
        worker_threads_.emplace_back(&FixParser::processing_worker, this);
    }
}
void FixParser::stop() {
    if (!running_.load()) return;
    running_.store(false);
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        message_callback_ = nullptr;
        error_callback_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        message_buffer_.clear();
        buffer_position_ = 0;
    }
}
void FixParser::feed_data(const char* data, size_t length) {
    if (!running_.load() || data == nullptr || length == 0) {
        return;
    }
    stats_.bytes_processed.fetch_add(length);
    std::string message;
    int retry_count = 0;
    const int max_retries = 3;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex_);
        message_buffer_.append(data, length);
        while (extract_next_message(message)) {
            if (!running_.load()) break;
            bool enqueued = false;
            retry_count = 0;
            while (!enqueued && retry_count < max_retries && running_.load()) {
                if (raw_message_queue_->enqueue(std::move(message))) {
                    enqueued = true;
                } else {
                    retry_count++;
                    if (retry_count < max_retries) {
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                }
            }
            if (!enqueued) {
                stats_.queue_full_events.fetch_add(1);
                stats_.messages_dropped.fetch_add(1);
                std::lock_guard<std::mutex> cb_lock(callback_mutex_);
                if (error_callback_) {
                    error_callback_("QUEUE_FULL", "Raw message queue is full after retries");
                }
            }
            message.clear();
        }
    }
}
void FixParser::feed_data(const std::string& data) {
    feed_data(data.c_str(), data.size());
}
bool FixParser::parse_message(const std::string& raw_message, FixMessage& parsed_message) {
    auto start_time = std::chrono::high_resolution_clock::now();
    bool success = parse_message_internal(raw_message, parsed_message);
    auto end_time = std::chrono::high_resolution_clock::now();
    if (success) {
        stats_.messages_parsed.fetch_add(1);
    } else {
        stats_.parse_errors.fetch_add(1);
    }
    double parse_time = std::chrono::duration<double, std::nano>(end_time - start_time).count();
    update_parse_time(parse_time);
    return success;
}
void FixParser::set_worker_threads(size_t count) {
    if (running_.load()) {
        throw std::runtime_error("Cannot change worker thread count while parser is running");
    }
    num_worker_threads_ = std::min(count, MAX_WORKER_THREADS);
}
void FixParser::parsing_worker() {
    std::string raw_message;
    FixMessage parsed_message;
    while (running_.load()) {
        try {
            if (raw_message_queue_->dequeue(raw_message)) {
                if (raw_message.empty() || raw_message.size() > MAX_FIX_MESSAGE_SIZE) {
                    raw_message.clear();
                    continue;
                }
                parsed_message.clear();
                if (parse_message_internal(raw_message, parsed_message)) {
                    bool enqueued = false;
                    int retry_count = 0;
                    const int max_retries = 5;
                    while (!enqueued && retry_count < max_retries && running_.load()) {
                        if (parsed_message_queue_->enqueue(std::move(parsed_message))) {
                            enqueued = true;
                        } else {
                            retry_count++;
                            if (retry_count < max_retries) {
                                std::this_thread::sleep_for(std::chrono::microseconds(5));
                            }
                        }
                    }
                    if (!enqueued) {
                        stats_.queue_full_events.fetch_add(1);
                        stats_.messages_dropped.fetch_add(1);
                        std::lock_guard<std::mutex> lock(callback_mutex_);
                        if (error_callback_ && running_.load()) {
                            try {
                                error_callback_("PARSED_QUEUE_FULL", "Parsed message queue is full after retries");
                            } catch (...) {
                                stats_.callback_errors.fetch_add(1);
                            }
                        }
                    }
                } else {
                    stats_.parse_errors.fetch_add(1);
                }
                raw_message.clear();
            } else {
                std::this_thread::yield();
            }
        } catch (const std::exception& e) {
            stats_.parse_errors.fetch_add(1);
            raw_message.clear();
            std::this_thread::yield();
        } catch (...) {
            stats_.parse_errors.fetch_add(1);
            raw_message.clear();
            std::this_thread::yield();
        }
    }
}
void FixParser::processing_worker() {
    FixMessage parsed_message;
    static int processing_count = 0;
    while (running_.load()) {
        if (parsed_message_queue_->dequeue(parsed_message)) {
            MessageCallback msg_callback;
            ErrorCallback err_callback;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                msg_callback = message_callback_;
                err_callback = error_callback_;
            }
            if (msg_callback && running_.load()) {
                try {
                    msg_callback(parsed_message);
                } catch (const std::exception& e) {
                    stats_.callback_errors.fetch_add(1);
                    if (err_callback && running_.load()) {
                        try {
                            err_callback("CALLBACK_ERROR", e.what());
                        } catch (...) {
                            stats_.callback_errors.fetch_add(1);
                        }
                    }
                } catch (...) {
                    stats_.callback_errors.fetch_add(1);
                }
            }
        } else {
            std::this_thread::yield();
        }
    }
}
bool FixParser::extract_next_message(std::string& message) {
    static int extract_count = 0;
    bool should_debug = (++extract_count <= 5);
    if (message_buffer_.size() < 20) {
        if (should_debug) std::cout << "[DEBUG] extract_next_message: buffer too small" << std::endl;
        return false;
    }
    size_t start_pos = message_buffer_.find("8=FIX", buffer_position_);
    if (start_pos == std::string::npos) {
        buffer_position_ = 0;
        return false;
    }
    size_t checksum_pos = message_buffer_.find("10=", start_pos);
    if (checksum_pos == std::string::npos) {
        return false;
    }
    size_t end_search_start = checksum_pos + 3;
    size_t message_end = message_buffer_.find(FIX_SOH, end_search_start);
    if (message_end == std::string::npos) {
        return false;
    }
    message_end += 1;
    size_t message_length = message_end - start_pos;
    if (message_length > MAX_FIX_MESSAGE_SIZE) {
        buffer_position_ = start_pos + 1;
        return false;
    }
    message = message_buffer_.substr(start_pos, message_length);
    buffer_position_ = message_end;
    if (buffer_position_ > message_buffer_.size() / 2 && buffer_position_ > 1024) {
        message_buffer_.erase(0, buffer_position_);
        buffer_position_ = 0;
    }
    return true;
}
bool FixParser::parse_message_internal(const std::string& raw_message, FixMessage& message) {
    if (raw_message.empty()) return false;
    message.clear();
    std::vector<std::string> fields;
    std::stringstream ss(raw_message);
    std::string field;
    size_t pos = 0;
    while (pos < raw_message.length()) {
        size_t next_soh = raw_message.find(FIX_SOH, pos);
        if (next_soh == std::string::npos) {
            next_soh = raw_message.length();
        }
        std::string field_str = raw_message.substr(pos, next_soh - pos);
        if (!field_str.empty()) {
            fields.push_back(field_str);
        }
        pos = next_soh + 1;
    }
    if (fields.empty()) return false;
    for (const auto& field_str : fields) {
        uint32_t tag;
        std::string value;
        if (!parse_field(field_str, tag, value)) {
            continue;
        }
        message.set_field(tag, std::move(value));
        switch (tag) {
            case Tags::BEGIN_STRING:
                message.begin_string = message.get_field(tag);
                break;
            case Tags::BODY_LENGTH:
                try {
                    message.body_length = std::stoul(message.get_field(tag));
                } catch (...) {
                    return false;
                }
                break;
            case Tags::MSG_TYPE:
                message.msg_type = message.get_field(tag);
                break;
            case Tags::MSG_SEQ_NUM:
                try {
                    message.msg_seq_num = std::stoul(message.get_field(tag));
                } catch (...) {
                    message.msg_seq_num = 0;
                }
                break;
            case Tags::SENDER_COMP_ID:
                message.sender_comp_id = message.get_field(tag);
                break;
            case Tags::TARGET_COMP_ID:
                message.target_comp_id = message.get_field(tag);
                break;
            case Tags::SENDING_TIME:
                message.sending_time = message.get_field(tag);
                break;
            case Tags::CHECK_SUM:
                try {
                    message.checksum = std::stoul(message.get_field(tag));
                } catch (...) {
                    message.checksum = 0;
                }
                break;
        }
    }
    if (!validate_message_structure(message)) {
        stats_.invalid_messages.fetch_add(1);
        return false;
    }
    uint8_t calculated_checksum = message.calculate_checksum();
    if (calculated_checksum != message.checksum) {
        stats_.checksum_errors.fetch_add(1);
        return false;
    }
    return true;
}
bool FixParser::parse_field(const std::string& field_str, uint32_t& tag, std::string& value) {
    size_t equals_pos = field_str.find(FIX_EQUALS);
    if (equals_pos == std::string::npos || equals_pos == 0) {
        return false;
    }
    try {
        tag = std::stoul(field_str.substr(0, equals_pos));
        value = field_str.substr(equals_pos + 1);
        return true;
    } catch (...) {
        return false;
    }
}
bool FixParser::validate_message_structure(const FixMessage& message) {
    return !message.begin_string.empty() &&
           !message.msg_type.empty() &&
           !message.sender_comp_id.empty() &&
           !message.target_comp_id.empty() &&
           message.body_length > 0;
}
uint8_t FixParser::calculate_checksum(const std::string& message_without_checksum) {
    uint32_t sum = 0;
    for (char c : message_without_checksum) {
        sum += static_cast<uint8_t>(c);
    }
    return static_cast<uint8_t>(sum % 256);
}
void FixParser::update_parse_time(double parse_time_ns) {
    double current_avg = stats_.avg_parse_time_ns.load();
    double alpha = 0.1;
    double new_avg = alpha * parse_time_ns + (1.0 - alpha) * current_avg;
    stats_.avg_parse_time_ns.store(new_avg);
}
FixMessageBuilder::FixMessageBuilder(const std::string& sender_comp_id, const std::string& target_comp_id)
    : sender_comp_id_(sender_comp_id), target_comp_id_(target_comp_id), next_seq_num_(1) {
    reset();
}
FixMessageBuilder& FixMessageBuilder::begin_string(const std::string& version) {
    message_.set_field(Tags::BEGIN_STRING, version);
    message_.begin_string = version;
    return *this;
}
FixMessageBuilder& FixMessageBuilder::msg_type(const std::string& type) {
    message_.set_field(Tags::MSG_TYPE, type);
    message_.msg_type = type;
    return *this;
}
FixMessageBuilder& FixMessageBuilder::msg_seq_num(uint32_t seq_num) {
    message_.set_field(Tags::MSG_SEQ_NUM, std::to_string(seq_num));
    message_.msg_seq_num = seq_num;
    next_seq_num_ = seq_num + 1;
    return *this;
}
FixMessageBuilder& FixMessageBuilder::sending_time() {
    std::string timestamp = current_timestamp();
    message_.set_field(Tags::SENDING_TIME, timestamp);
    message_.sending_time = timestamp;
    return *this;
}
FixMessageBuilder& FixMessageBuilder::sending_time(const std::string& time) {
    message_.set_field(Tags::SENDING_TIME, time);
    message_.sending_time = time;
    return *this;
}
FixMessageBuilder& FixMessageBuilder::field(uint32_t tag, const std::string& value) {
    message_.set_field(tag, value);
    return *this;
}
FixMessageBuilder& FixMessageBuilder::field(uint32_t tag, double value, int precision) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << value;
    message_.set_field(tag, ss.str());
    return *this;
}
FixMessageBuilder& FixMessageBuilder::field(uint32_t tag, int64_t value) {
    message_.set_field(tag, std::to_string(value));
    return *this;
}
FixMessageBuilder& FixMessageBuilder::field(uint32_t tag, uint64_t value) {
    message_.set_field(tag, std::to_string(value));
    return *this;
}
FixMessageBuilder& FixMessageBuilder::cl_ord_id(const std::string& client_order_id) {
    message_.set_field(Tags::CL_ORD_ID, client_order_id);
    return *this;
}
FixMessageBuilder& FixMessageBuilder::order_id(const std::string& order_id) {
    message_.set_field(Tags::ORDER_ID, order_id);
    return *this;
}
FixMessageBuilder& FixMessageBuilder::symbol(const std::string& symbol) {
    message_.set_field(Tags::SYMBOL, symbol);
    return *this;
}
FixMessageBuilder& FixMessageBuilder::side(char side) {
    message_.set_field(Tags::SIDE, std::string(1, side));
    return *this;
}
FixMessageBuilder& FixMessageBuilder::order_qty(uint64_t quantity) {
    message_.set_field(Tags::ORDER_QTY, std::to_string(quantity));
    return *this;
}
FixMessageBuilder& FixMessageBuilder::price(double price, int precision) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(precision) << price;
    message_.set_field(Tags::PRICE, ss.str());
    return *this;
}
FixMessageBuilder& FixMessageBuilder::ord_type(char type) {
    message_.set_field(Tags::ORD_TYPE, std::string(1, type));
    return *this;
}
FixMessageBuilder& FixMessageBuilder::time_in_force(char tif) {
    message_.set_field(Tags::TIME_IN_FORCE, std::string(1, tif));
    return *this;
}
FixMessage FixMessageBuilder::build() {
    if (message_.begin_string.empty()) {
        begin_string();
    }
    if (message_.sender_comp_id.empty()) {
        message_.set_field(Tags::SENDER_COMP_ID, sender_comp_id_);
        message_.sender_comp_id = sender_comp_id_;
    }
    if (message_.target_comp_id.empty()) {
        message_.set_field(Tags::TARGET_COMP_ID, target_comp_id_);
        message_.target_comp_id = target_comp_id_;
    }
    if (message_.msg_seq_num == 0) {
        msg_seq_num(next_seq_num_);
    }
    if (message_.sending_time.empty()) {
        sending_time();
    }
    calculate_and_set_body_length();
    calculate_and_set_checksum();
    return message_;
}
std::string FixMessageBuilder::to_string() {
    FixMessage msg = build();
    return serialize_fix_message(msg);
}
void FixMessageBuilder::reset() {
    message_.clear();
    message_.msg_seq_num = 0;
}
std::string FixMessageBuilder::current_timestamp() {
    return generate_fix_timestamp();
}
void FixMessageBuilder::calculate_and_set_body_length() {
    uint32_t body_length = 0;
    for (const auto& field : message_.ordered_fields) {
        if (field.tag != Tags::BEGIN_STRING &&
            field.tag != Tags::BODY_LENGTH &&
            field.tag != Tags::CHECK_SUM) {
            std::string field_str = std::to_string(field.tag) + "=" + field.value + FIX_SOH;
            body_length += field_str.length();
        }
    }
    message_.set_field(Tags::BODY_LENGTH, std::to_string(body_length));
    message_.body_length = body_length;
}
void FixMessageBuilder::calculate_and_set_checksum() {
    uint8_t checksum = message_.calculate_checksum();
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(3) << static_cast<int>(checksum);
    message_.set_field(Tags::CHECK_SUM, ss.str());
    message_.checksum = checksum;
}
std::string generate_fix_timestamp() {
    return generate_fix_timestamp(core::HighResolutionClock::now());
}
std::string generate_fix_timestamp(const core::TimePoint& time) {
    auto system_time = std::chrono::system_clock::now();
    auto time_t_val = std::chrono::system_clock::to_time_t(system_time);
    std::ostringstream ss;
    ss << std::put_time(std::gmtime(&time_t_val), "%Y%m%d-%H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        system_time.time_since_epoch()) % 1000;
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}
std::string serialize_fix_message(const FixMessage& message) {
    std::ostringstream ss;
    for (const auto& field : message.ordered_fields) {
        ss << field.tag << "=" << field.value << FIX_SOH;
    }
    return ss.str();
}
bool is_admin_message(const std::string& msg_type) {
    return msg_type == "0" || msg_type == "1" || msg_type == "2" ||
           msg_type == "3" || msg_type == "4" || msg_type == "5";
}
bool is_application_message(const std::string& msg_type) {
    return !is_admin_message(msg_type);
}
bool is_order_message(const std::string& msg_type) {
    return msg_type == "D" || msg_type == "F" || msg_type == "G";
}
bool is_execution_report(const std::string& msg_type) {
    return msg_type == "8";
}
}
}
