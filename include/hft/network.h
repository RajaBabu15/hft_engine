#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <functional>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <memory>
#include <chrono>
#include <optional>
#include <iostream>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <queue>

#include "hft/order.h"
#include "hft/platform.h"
#include "hft/order_node.h"
#include "hft/matching_engine_types.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>
    #include <windows.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "Ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <net/if.h>
    #include <errno.h>
    #include <sys/uio.h>
    #if defined(__linux__)
        #include <netpacket/packet.h>
        #include <net/ethernet.h>
    #endif
    #include <sys/mman.h>
    #ifdef __linux__
        #include <sys/epoll.h>
        #include <sys/eventfd.h>
        #include <linux/if_packet.h>
        #include <linux/filter.h>
        #include <netinet/udp.h>
        #include <sched.h>
    #elif defined(__APPLE__)
        #include <sys/event.h>
        #include <mach/mach_time.h>
    #endif
#endif

namespace hft {
namespace net {
class TimeUtils {
public:
    static inline uint64_t rdtsc() noexcept {
#ifdef _WIN32
        return __rdtsc();
#elif defined(__x86_64__)
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
#else
        return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
    }

    static inline uint64_t now_ns() noexcept {
        using namespace std::chrono;
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    }

    static inline void spin_delay(int cycles) noexcept {
#ifdef _WIN32
        for (int i = 0; i < cycles; ++i) _mm_pause();
#else
        for (int i = 0; i < cycles; ++i) {
            // Portable fallback
            std::this_thread::yield();
        }
#endif
    }
};

// CPU affinity and priority settings
class CpuUtils {
public:
    static bool set_thread_affinity(std::thread& t, int cpu_id) {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        return pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset) == 0;
#elif defined(_WIN32)
        DWORD_PTR mask = 1ULL << cpu_id;
        return SetThreadAffinityMask(t.native_handle(), mask) != 0;
#else
        return true; // macOS doesn't support direct affinity
#endif
    }

    static bool set_realtime_priority(std::thread& t) {
#ifdef __linux__
        struct sched_param param;
        param.sched_priority = 99;
        return pthread_setschedparam(t.native_handle(), SCHED_FIFO, &param) == 0;
#elif defined(_WIN32)
        return SetThreadPriority(t.native_handle(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
#else
        return true;
#endif
    }
};

// -----------------------------------------------------------------------------
// Memory Management & Cache Optimization
// -----------------------------------------------------------------------------

template<typename T>
class alignas(CACHE_LINE_SIZE) CacheAlignedAtomic : public std::atomic<T> {
public:
    using std::atomic<T>::atomic;
};

// High-performance memory pool for network buffers
class BufferPool {
public:
    static constexpr size_t BUFFER_SIZE = 2048;
    static constexpr size_t POOL_SIZE = 4096;

    BufferPool() {
        for (size_t i = 0; i < POOL_SIZE; ++i) {
            free_buffers_.push(buffers_[i].data());
        }
    }

    uint8_t* acquire() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_buffers_.empty()) return nullptr;
        uint8_t* buf = free_buffers_.front();
        free_buffers_.pop();
        return buf;
    }

    void release(uint8_t* buf) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        free_buffers_.push(buf);
    }

private:
    std::array<std::array<uint8_t, BUFFER_SIZE>, POOL_SIZE> buffers_;
    std::queue<uint8_t*> free_buffers_;
    std::mutex mutex_;
};

// -----------------------------------------------------------------------------
// Lock-Free Data Structures (Enhanced)
// -----------------------------------------------------------------------------

// SPSC Ring with padding for cache line alignment
template <typename T, size_t Capacity>
class alignas(CACHE_LINE_SIZE) SpscRingOptimized {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");
public:
    SpscRingOptimized() noexcept : head_(0), tail_(0) {}

    bool push(const T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t next_head = current_head + 1;
        if ((next_head - tail_.load(std::memory_order_acquire)) > Capacity) {
            return false; // Queue full
        }
        buffer_[current_head & mask()] = item;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        if (current_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue empty
        }
        item = buffer_[current_tail & mask()];
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

private:
    static constexpr size_t mask() noexcept { return Capacity - 1; }
    
    T buffer_[Capacity];
    alignas(CACHE_LINE_SIZE) CacheAlignedAtomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) CacheAlignedAtomic<size_t> tail_;
};

// Batched MPMC queue for higher throughput
template <typename T, size_t Capacity>
class MpmcBatchQueue {
public:
    MpmcBatchQueue() : head_(0), tail_(0) {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool enqueue_batch(const T* items, size_t count) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i) {
            Slot* slot = &slots_[(head + i) & mask()];
            size_t seq = slot->sequence.load(std::memory_order_acquire);
            if (seq != (head + i)) return false;
        }
        
        if (!head_.compare_exchange_weak(head, head + count, std::memory_order_relaxed)) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            Slot* slot = &slots_[(head + i) & mask()];
            slot->data = items[i];
            slot->sequence.store(head + i + 1, std::memory_order_release);
        }
        return true;
    }

    size_t dequeue_batch(T* items, size_t max_count) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t count = 0;
        
        for (size_t i = 0; i < max_count; ++i) {
            Slot* slot = &slots_[(tail + i) & mask()];
            size_t seq = slot->sequence.load(std::memory_order_acquire);
            if (seq != (tail + i + 1)) break;
            count++;
        }
        
        if (count == 0) return 0;
        if (!tail_.compare_exchange_weak(tail, tail + count, std::memory_order_relaxed)) {
            return 0;
        }

        for (size_t i = 0; i < count; ++i) {
            Slot* slot = &slots_[(tail + i) & mask()];
            items[i] = slot->data;
            slot->sequence.store(tail + i + Capacity, std::memory_order_release);
        }
        return count;
    }

private:
    static constexpr size_t mask() noexcept { return Capacity - 1; }
    
    struct Slot {
        CacheAlignedAtomic<size_t> sequence;
        T data;
    };
    
    Slot slots_[Capacity];
    alignas(CACHE_LINE_SIZE) CacheAlignedAtomic<size_t> head_;
    alignas(CACHE_LINE_SIZE) CacheAlignedAtomic<size_t> tail_;
};

// -----------------------------------------------------------------------------
// Market Data Types & Structures
// -----------------------------------------------------------------------------

enum class FeedType : uint8_t {
    FIX_42,
    FIX_44,
    FAST,
    ITCH_50,
    BINARY_PROPRIETARY,
    OUCH,
    SBE
};

enum class MessageType : uint8_t {
    NEW_ORDER = 1,
    ORDER_CANCEL,
    ORDER_REPLACE,
    EXECUTION_REPORT,
    MARKET_DATA_SNAPSHOT,
    MARKET_DATA_INCREMENT,
    TRADE_REPORT,
    QUOTE_UPDATE,
    HEARTBEAT
};

struct alignas(64) MarketDataMessage {
    uint64_t timestamp_ns;
    uint64_t sequence_number;
    FeedType feed_type;
    MessageType msg_type;
    uint32_t symbol_id;
    uint32_t data_length;
    uint8_t data[1920]; // Remaining space in cache line aligned struct
};

struct alignas(64) L2Update {
    uint64_t timestamp_ns;
    uint32_t symbol_id;
    uint32_t level_count;
    struct Level {
        Price price;
        Quantity qty;
        uint16_t orders;
        Side side;
    } levels[32];
};

// Order book integration structure
struct OrderBookUpdate {
    uint32_t symbol_id;
    Price price;
    Quantity qty;
    Side side;
    uint8_t action; // 0=add, 1=modify, 2=delete
    uint64_t order_id;
    uint64_t timestamp_ns;
};

// -----------------------------------------------------------------------------
// Protocol Parsers (Optimized)
// -----------------------------------------------------------------------------

// Enhanced FIX parser with template specialization
class FixParser {
public:
    using FieldCallback = std::function<void(int tag, std::string_view value)>;
    
    struct ParseResult {
        bool success = false;
        MessageType msg_type = MessageType::HEARTBEAT;
        size_t bytes_consumed = 0;
    };

    // Fast FIX parsing with SOH/pipe support and checksum validation
    static ParseResult parse_message(const uint8_t* data, size_t len, FieldCallback cb) noexcept {
        ParseResult result;
        const char* p = reinterpret_cast<const char*>(data);
        const char* end = p + len;
        
        // Fast scan for complete message (SOH terminated)
        const char* msg_end = static_cast<const char*>(memchr(p, '\x01', len));
        if (!msg_end) return result; // Incomplete message
        
        uint32_t computed_checksum = 0;
        bool found_checksum = false;
        uint32_t msg_checksum = 0;
        
        while (p < msg_end) {
            const char* eq = static_cast<const char*>(memchr(p, '=', msg_end - p));
            if (!eq) break;
            
            const char* delim = static_cast<const char*>(memchr(eq + 1, '\x01', msg_end - (eq + 1)));
            if (!delim) delim = msg_end;
            
            // Parse tag number (optimized)
            int tag = 0;
            for (const char* t = p; t < eq; ++t) {
                char c = *t;
                if (c < '0' || c > '9') { tag = -1; break; }
                tag = tag * 10 + (c - '0');
            }
            
            if (tag >= 0) {
                std::string_view value(eq + 1, delim - (eq + 1));
                
                // Handle checksum field specially
                if (tag == 10) {
                    found_checksum = true;
                    msg_checksum = parse_int(value);
                } else {
                    // Update running checksum
                    for (const char* ch = p; ch <= delim; ++ch) {
                        computed_checksum += static_cast<uint8_t>(*ch);
                    }
                    cb(tag, value);
                    
                    // Extract message type
                    if (tag == 35) {
                        result.msg_type = map_fix_msg_type(value);
                    }
                }
            }
            
            p = delim + 1;
        }
        
        // Validate checksum if present
        result.success = !found_checksum || (computed_checksum % 256) == (msg_checksum % 256);
        result.bytes_consumed = (msg_end - reinterpret_cast<const char*>(data)) + 1;
        return result;
    }

private:
    static uint32_t parse_int(std::string_view s) noexcept {
        uint32_t result = 0;
        for (char c : s) {
            if (c < '0' || c > '9') break;
            result = result * 10 + (c - '0');
        }
        return result;
    }
    
    static MessageType map_fix_msg_type(std::string_view type) noexcept {
        if (type == "D") return MessageType::NEW_ORDER;
        if (type == "F") return MessageType::ORDER_CANCEL;
        if (type == "G") return MessageType::ORDER_REPLACE;
        if (type == "8") return MessageType::EXECUTION_REPORT;
        return MessageType::HEARTBEAT;
    }
};

// FAST (FIX Adapted for Streaming) decoder skeleton
class FastDecoder {
public:
    struct Template {
        uint32_t id;
        std::vector<uint8_t> field_ops; // Encoded field operations
    };
    
    bool decode_message(const uint8_t* data, size_t len, MarketDataMessage& output) {
        // FAST decoding implementation would go here
        // This is a complex binary protocol requiring template management
        return false; // Placeholder
    }
};

// NASDAQ ITCH 5.0 decoder (binary market data)
class ItchDecoder {
public:
    bool decode_message(const uint8_t* data, size_t len, L2Update& output) {
        if (len < 2) return false;
        
        uint16_t msg_len = (data[0] << 8) | data[1];
        if (len < msg_len) return false;
        
        char msg_type = data[2];
        
        switch (msg_type) {
            case 'A': // Add Order
                return decode_add_order(data + 3, msg_len - 3, output);
            case 'U': // Replace Order
                return decode_replace_order(data + 3, msg_len - 3, output);
            case 'D': // Delete Order
                return decode_delete_order(data + 3, msg_len - 3, output);
            case 'E': // Order Executed
                return decode_execution(data + 3, msg_len - 3, output);
            default:
                return false;
        }
    }

private:
    bool decode_add_order(const uint8_t* data, size_t len, L2Update& output) {
        // ITCH Add Order message parsing
        if (len < 35) return false; // Minimum message size
        
        // Extract timestamp (6 bytes, big-endian)
        uint64_t timestamp = 0;
        for (int i = 0; i < 6; ++i) {
            timestamp = (timestamp << 8) | data[i];
        }
        output.timestamp_ns = timestamp;
        
        // Order reference (8 bytes)
        uint64_t order_ref = 0;
        for (int i = 6; i < 14; ++i) {
            order_ref = (order_ref << 8) | data[i];
        }
        
        // Side (1 byte: 'B' or 'S')
        output.levels[0].side = (data[14] == 'B') ? Side::BUY : Side::SELL;
        
        // Shares (4 bytes, big-endian)
        uint32_t shares = (data[15] << 24) | (data[16] << 16) | (data[17] << 8) | data[18];
        output.levels[0].qty = shares;
        
        // Stock symbol (8 bytes, right-padded with spaces)
        // Convert to symbol_id using hash or lookup table
        output.symbol_id = hash_symbol(reinterpret_cast<const char*>(data + 19), 8);
        
        // Price (4 bytes, big-endian, in 1/10000 increments)
        uint32_t price_raw = (data[27] << 24) | (data[28] << 16) | (data[29] << 8) | data[30];
        output.levels[0].price = price_raw; // Store as-is, convert later if needed
        
        output.level_count = 1;
        return true;
    }
    
    bool decode_replace_order(const uint8_t* data, size_t len, L2Update& output) {
        // Implementation for order replace messages
        return false;
    }
    
    bool decode_delete_order(const uint8_t* data, size_t len, L2Update& output) {
        // Implementation for order delete messages
        return false;
    }
    
    bool decode_execution(const uint8_t* data, size_t len, L2Update& output) {
        // Implementation for execution messages
        return false;
    }
    
    uint32_t hash_symbol(const char* symbol, size_t len) {
        // Simple hash function for symbol -> ID mapping
        uint32_t hash = 5381;
        for (size_t i = 0; i < len && symbol[i] != ' '; ++i) {
            hash = ((hash << 5) + hash) + symbol[i];
        }
        return hash;
    }
};

// -----------------------------------------------------------------------------
// Network Transport Layer (Enhanced)
// -----------------------------------------------------------------------------

// Raw socket with kernel bypass support
class RawSocket {
public:
    RawSocket() : fd_(-1) {}
    ~RawSocket() { close(); }
    
    bool create_udp_raw(const std::string& interface = "") {
#ifdef __linux__
        fd_ = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
        if (fd_ < 0) return false;
        
        if (!interface.empty()) {
            struct sockaddr_ll addr{};
            addr.sll_family = AF_PACKET;
            addr.sll_protocol = htons(ETH_P_IP);
            addr.sll_ifindex = if_nametoindex(interface.c_str());
            
            if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
                ::close(fd_);
                fd_ = -1;
                return false;
            }
        }
        
        // Set socket to non-blocking
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        
        return true;
#else
        // On non-Linux platforms, fall back to standard UDP socket
        (void)interface;
        return create_udp_standard("0.0.0.0", 0);
#endif
    }
    
    bool create_udp_standard(const std::string& addr, uint16_t port) {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) return false;
        
        int reuse = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        
        // Optimize socket buffer sizes
        int buf_size = 16 * 1024 * 1024; // 16MB
        setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
        
        struct sockaddr_in bind_addr{};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(port);
        bind_addr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(fd_, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        
        // Join multicast group if address is multicast
        join_multicast(addr);
        
        // Set non-blocking
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        
        return true;
    }
    
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    
    int fd() const { return fd_; }

private:
    int fd_;
    
    void join_multicast(const std::string& addr) {
        struct in_addr mcast_addr;
        if (inet_pton(AF_INET, addr.c_str(), &mcast_addr) == 1) {
            uint8_t first_octet = ntohl(mcast_addr.s_addr) >> 24;
            if (first_octet >= 224 && first_octet <= 239) {
                struct ip_mreq mreq{};
                mreq.imr_multiaddr = mcast_addr;
                mreq.imr_interface.s_addr = INADDR_ANY;
                setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
            }
        }
    }
};

// High-performance event loop using epoll/kqueue/IOCP
class EventLoop {
public:
    using EventCallback = std::function<void(int fd, uint32_t events)>;
    
    EventLoop() {
#ifdef __linux__
        epfd_ = epoll_create1(EPOLL_CLOEXEC);
#elif defined(__APPLE__)
        kq_ = kqueue();
#elif defined(_WIN32)
        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
#endif
    }
    
    ~EventLoop() {
#ifdef __linux__
        if (epfd_ >= 0) ::close(epfd_);
#elif defined(__APPLE__)
        if (kq_ >= 0) ::close(kq_);
#elif defined(_WIN32)
        if (iocp_) CloseHandle(iocp_);
#endif
    }
    
    bool add_socket(int fd, EventCallback cb) {
        callbacks_[fd] = std::move(cb);
        
#ifdef __linux__
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET; // Edge-triggered for performance
        ev.data.fd = fd;
        return epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == 0;
#elif defined(__APPLE__)
        struct kevent kev;
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
        return kevent(kq_, &kev, 1, nullptr, 0, nullptr) == 0;
#elif defined(_WIN32)
        HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
        return CreateIoCompletionPort(h, iocp_, fd, 0) != nullptr;
#endif
    }
    
    void run(std::atomic<bool>& running) {
#ifdef __linux__
        const int MAX_EVENTS = 128;
        struct epoll_event events[MAX_EVENTS];
        
        while (running.load(std::memory_order_relaxed)) {
            int nfds = epoll_wait(epfd_, events, MAX_EVENTS, 1); // 1ms timeout
            if (nfds < 0 && errno != EINTR) break;
            
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                auto it = callbacks_.find(fd);
                if (it != callbacks_.end()) {
                    it->second(fd, events[i].events);
                }
            }
        }
#elif defined(__APPLE__)
        const int MAX_EVENTS = 128;
        struct kevent events[MAX_EVENTS];
        struct timespec timeout = {0, 1000000}; // 1ms
        
        while (running.load(std::memory_order_relaxed)) {
            int nfds = kevent(kq_, nullptr, 0, events, MAX_EVENTS, &timeout);
            if (nfds < 0 && errno != EINTR) break;
            
            for (int i = 0; i < nfds; ++i) {
                int fd = static_cast<int>(events[i].ident);
                auto it = callbacks_.find(fd);
                if (it != callbacks_.end()) {
                    it->second(fd, 0);
                }
            }
        }
#elif defined(_WIN32)
        DWORD bytes_transferred;
        ULONG_PTR completion_key;
        OVERLAPPED* overlapped;
        
        while (running.load(std::memory_order_relaxed)) {
            BOOL result = GetQueuedCompletionStatus(iocp_, &bytes_transferred, 
                                                   &completion_key, &overlapped, 1);
            if (result || overlapped) {
                int fd = static_cast<int>(completion_key);
                auto it = callbacks_.find(fd);
                if (it != callbacks_.end()) {
                    it->second(fd, 0);
                }
            }
        }
#endif
    }

private:
#ifdef __linux__
    int epfd_ = -1;
#elif defined(__APPLE__)
    int kq_ = -1;
#elif defined(_WIN32)
    HANDLE iocp_ = nullptr;
#endif
    std::unordered_map<int, EventCallback> callbacks_;
};

// -----------------------------------------------------------------------------
// Market Data Feed Manager
// -----------------------------------------------------------------------------

class MarketDataFeed {
public:
    using L2UpdateCallback = std::function<void(const L2Update&)>;
    using OrderCallback = std::function<void(const hft::Order&)>;
    using OrderBookUpdateCallback = std::function<void(const OrderBookUpdate&)>;
    
    struct FeedConfig {
        std::string name;
        FeedType type;
        std::string multicast_addr;
        uint16_t port;
        std::string interface;
        bool use_raw_socket = false;
        int cpu_affinity = -1;
        bool realtime_priority = false;
    };
    
    MarketDataFeed() : running_(false), buffer_pool_(std::make_unique<BufferPool>()) {}
    
    ~MarketDataFeed() {
        stop();
    }
    
    bool start(const FeedConfig& config) {
        config_ = config;
        
        // Create appropriate socket
        if (config.use_raw_socket) {
            if (!raw_socket_.create_udp_raw(config.interface)) {
                return false;
            }
        } else {
            if (!raw_socket_.create_udp_standard(config.multicast_addr, config.port)) {
                return false;
            }
        }
        
        // Setup event loop
        event_loop_ = std::make_unique<EventLoop>();
        if (!event_loop_->add_socket(raw_socket_.fd(), 
            [this](int fd, uint32_t events) { this->handle_data(fd, events); })) {
            return false;
        }
        
        // Start processing thread
        running_.store(true, std::memory_order_release);
        processing_thread_ = std::thread([this]() { this->process_loop(); });
        
        // Set CPU affinity and priority if specified
        if (config.cpu_affinity >= 0) {
            CpuUtils::set_thread_affinity(processing_thread_, config.cpu_affinity);
        }
        if (config.realtime_priority) {
            CpuUtils::set_realtime_priority(processing_thread_);
        }
        
        return true;
    }
    
    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }
        
        raw_socket_.close();
    }
    
    void set_l2_callback(L2UpdateCallback cb) { l2_callback_ = std::move(cb); }
    void set_order_callback(OrderCallback cb) { order_callback_ = std::move(cb); }
    void set_orderbook_callback(OrderBookUpdateCallback cb) { orderbook_callback_ = std::move(cb); }
    
    // Attach lock-free queues for cross-thread communication
    template<size_t N>
    void attach_l2_queue(MpmcBatchQueue<L2Update, N>* queue) { l2_queue_ = reinterpret_cast<void*>(queue); l2_deq_ = [](void* q, L2Update* out, size_t maxc){ return static_cast<MpmcBatchQueue<L2Update, N>*>(q)->dequeue_batch(out, maxc); }; }
    template<size_t N>
    void attach_order_queue(MpmcBatchQueue<hft::Order, N>* queue) { order_queue_ = reinterpret_cast<void*>(queue); order_deq_ = [](void* q, hft::Order* out, size_t maxc){ return static_cast<MpmcBatchQueue<hft::Order, N>*>(q)->dequeue_batch(out, maxc); }; }

private:
    FeedConfig config_;
    RawSocket raw_socket_;
    std::unique_ptr<EventLoop> event_loop_;
    std::unique_ptr<BufferPool> buffer_pool_;
    std::thread processing_thread_;
    std::atomic<bool> running_;
    
    // Protocol parsers
    FixParser fix_parser_;
    FastDecoder fast_decoder_;
    ItchDecoder itch_decoder_;
    
    // Callbacks
    L2UpdateCallback l2_callback_;
    OrderCallback order_callback_;
    OrderBookUpdateCallback orderbook_callback_;
    
    // Lock-free queues (use a fixed capacity flavor for demo)
    MpmcBatchQueue<L2Update, 8192>* l2_queue_ = nullptr;
    MpmcBatchQueue<hft::Order, 8192>* order_queue_ = nullptr;
    
    // Statistics
    std::atomic<uint64_t> packets_received_{0};
    std::atomic<uint64_t> messages_parsed_{0};
    std::atomic<uint64_t> parse_errors_{0};
    
    void process_loop() {
        event_loop_->run(running_);
    }
    
    void handle_data(int fd, uint32_t events) {
#ifdef __linux__
        // Use recvmmsg for batch processing on Linux
        handle_data_batch_linux(fd);
#else
        // Single packet processing for other platforms
        handle_data_single(fd);
#endif
    }
    
#ifdef __linux__
    void handle_data_batch_linux(int fd) {
        constexpr int BATCH_SIZE = 32;
        struct mmsghdr msgs[BATCH_SIZE];
        struct iovec iovs[BATCH_SIZE];
        uint8_t* buffers[BATCH_SIZE];
        
        // Acquire buffers from pool
        for (int i = 0; i < BATCH_SIZE; ++i) {
            buffers[i] = buffer_pool_->acquire();
            if (!buffers[i]) break;
            
            iovs[i].iov_base = buffers[i];
            iovs[i].iov_len = BufferPool::BUFFER_SIZE;
            msgs[i].msg_hdr.msg_iov = &iovs[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
            msgs[i].msg_hdr.msg_name = nullptr;
            msgs[i].msg_hdr.msg_namelen = 0;
            msgs[i].msg_hdr.msg_control = nullptr;
            msgs[i].msg_hdr.msg_controllen = 0;
        }
        
        int received = recvmmsg(fd, msgs, BATCH_SIZE, MSG_DONTWAIT, nullptr);
        if (received <= 0) {
            // Release all buffers
            for (int i = 0; i < BATCH_SIZE; ++i) {
                if (buffers[i]) buffer_pool_->release(buffers[i]);
            }
            return;
        }
        
        packets_received_.fetch_add(received, std::memory_order_relaxed);
        
        // Process each received packet
        for (int i = 0; i < received; ++i) {
            process_packet(buffers[i], msgs[i].msg_len);
            buffer_pool_->release(buffers[i]);
        }
        
        // Release unused buffers
        for (int i = received; i < BATCH_SIZE; ++i) {
            if (buffers[i]) buffer_pool_->release(buffers[i]);
        }
    }
#endif
    
    void handle_data_single(int fd) {
        uint8_t* buffer = buffer_pool_->acquire();
        if (!buffer) return;
        
        ssize_t received = recv(fd, buffer, BufferPool::BUFFER_SIZE, MSG_DONTWAIT);
        if (received > 0) {
            packets_received_.fetch_add(1, std::memory_order_relaxed);
            process_packet(buffer, received);
        }
        
        buffer_pool_->release(buffer);
    }
    
    void process_packet(uint8_t* data, size_t len) {
        uint64_t timestamp = TimeUtils::now_ns();
        
        switch (config_.type) {
            case FeedType::FIX_42:
            case FeedType::FIX_44:
                process_fix_packet(data, len, timestamp);
                break;
            case FeedType::ITCH_50:
                process_itch_packet(data, len, timestamp);
                break;
            case FeedType::FAST:
                process_fast_packet(data, len, timestamp);
                break;
            case FeedType::BINARY_PROPRIETARY:
                process_binary_packet(data, len, timestamp);
                break;
            default:
                parse_errors_.fetch_add(1, std::memory_order_relaxed);
                break;
        }
    }
    
    void process_fix_packet(uint8_t* data, size_t len, uint64_t timestamp) {
        auto result = fix_parser_.parse_message(data, len, 
            [this, timestamp](int tag, std::string_view value) {
                this->handle_fix_field(tag, value, timestamp);
            });
        
        if (result.success) {
            messages_parsed_.fetch_add(1, std::memory_order_relaxed);
        } else {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void process_itch_packet(uint8_t* data, size_t len, uint64_t timestamp) {
        L2Update update;
        if (itch_decoder_.decode_message(data, len, update)) {
            messages_parsed_.fetch_add(1, std::memory_order_relaxed);
            
            // Dispatch to callback or queue
            if (l2_callback_) {
                l2_callback_(update);
            }
        } else {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void process_fast_packet(uint8_t* data, size_t len, uint64_t timestamp) {
        MarketDataMessage msg;
        if (fast_decoder_.decode_message(data, len, msg)) {
            messages_parsed_.fetch_add(1, std::memory_order_relaxed);
            // Convert to L2Update and dispatch
        } else {
            parse_errors_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    void process_binary_packet(uint8_t* data, size_t len, uint64_t timestamp) {
        // Custom binary protocol processing
        // This would be exchange-specific
        messages_parsed_.fetch_add(1, std::memory_order_relaxed);
    }
    
    void handle_fix_field(int tag, std::string_view value, uint64_t timestamp) {
        // Convert FIX fields to orders or market data updates
        static thread_local hft::Order current_order;
        static thread_local bool order_complete = false;
        
        switch (tag) {
            case 35: // MsgType
                if (value == "D") { // New Order Single
                    current_order = hft::Order{};
                    current_order.type = OrderType::LIMIT;
                    current_order.tif = TimeInForce::GTC;
                    order_complete = false;
                }
                break;
            case 55: // Symbol
                current_order.symbol = static_cast<hft::Symbol>(
                    std::hash<std::string_view>{}(value));
                break;
            case 54: // Side
                current_order.side = (value == "1") ? Side::BUY : Side::SELL;
                break;
            case 44: // Price
                current_order.price = static_cast<Price>(
                    std::stoll(std::string(value)));
                break;
            case 38: // OrderQty
                current_order.qty = static_cast<Quantity>(
                    std::stoll(std::string(value)));
                order_complete = true;
                break;
        }
        
        if (order_complete) {
            if (order_callback_) {
                order_callback_(current_order);
            }
            order_complete = false;
        }
    }
};

// -----------------------------------------------------------------------------
// Order Entry Gateway (Ultra-Low Latency TCP)
// -----------------------------------------------------------------------------

class OrderEntryGateway {
public:
    struct GatewayConfig {
        std::string host;
        uint16_t port;
        bool use_nagle = false;
        int send_buffer_size = 64 * 1024;
        int recv_buffer_size = 64 * 1024;
        int cpu_affinity = -1;
        bool realtime_priority = false;
    };
    
    using ExecutionCallback = std::function<void(const std::string&)>;
    
    OrderEntryGateway() : connected_(false), sequence_number_(1) {}
    
    ~OrderEntryGateway() {
        disconnect();
    }
    
    bool connect(const GatewayConfig& config) {
        config_ = config;
        
#ifdef _WIN32
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) return false;
#endif
        
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0) return false;
        
        // Optimize socket for low latency
        optimize_socket();
        
        // Connect to server
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.port);
        if (inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr) <= 0) {
            close_socket();
            return false;
        }
        
        if (::connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            close_socket();
            return false;
        }
        
        connected_.store(true, std::memory_order_release);
        
        // Start receiver thread
        receiver_thread_ = std::thread([this]() { this->receive_loop(); });
        
        if (config.cpu_affinity >= 0) {
            CpuUtils::set_thread_affinity(receiver_thread_, config.cpu_affinity);
        }
        if (config.realtime_priority) {
            CpuUtils::set_realtime_priority(receiver_thread_);
        }
        
        return true;
    }
    
    void disconnect() {
        if (!connected_.exchange(false, std::memory_order_acq_rel)) return;
        
        if (receiver_thread_.joinable()) {
            receiver_thread_.join();
        }
        
        close_socket();
    }
    
    // Send FIX order with minimal latency
    bool send_new_order(const hft::Order& order) {
        if (!connected_.load(std::memory_order_acquire)) return false;
        
        // Build FIX message in pre-allocated buffer
        char buffer[512];
        int len = build_new_order_fix(order, buffer, sizeof(buffer));
        
        ssize_t sent = ::send(sock_, buffer, len, MSG_DONTWAIT);
        return sent == len;
    }
    
    // Batch send multiple orders
    bool send_order_batch(const hft::Order* orders, size_t count) {
        if (!connected_.load(std::memory_order_acquire) || count == 0) return false;
        
        constexpr size_t MAX_BATCH_SIZE = 8192;
        char batch_buffer[MAX_BATCH_SIZE];
        size_t total_len = 0;
        
        for (size_t i = 0; i < count; ++i) {
            int msg_len = build_new_order_fix(orders[i], 
                batch_buffer + total_len, MAX_BATCH_SIZE - total_len);
            if (msg_len <= 0 || total_len + msg_len > MAX_BATCH_SIZE) break;
            total_len += msg_len;
        }
        
        if (total_len == 0) return false;
        
        ssize_t sent = ::send(sock_, batch_buffer, total_len, MSG_DONTWAIT);
        return sent == static_cast<ssize_t>(total_len);
    }
    
    void set_execution_callback(ExecutionCallback cb) {
        execution_callback_ = std::move(cb);
    }

private:
    GatewayConfig config_;
    int sock_ = -1;
    std::atomic<bool> connected_;
    std::thread receiver_thread_;
    std::atomic<uint32_t> sequence_number_;
    ExecutionCallback execution_callback_;
    
    void optimize_socket() {
        // Disable Nagle's algorithm for low latency (if available)
        if (!config_.use_nagle) {
            #ifdef TCP_NODELAY
            int flag = 1;
            setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
            #endif
        }
        
        // Set socket buffer sizes
        setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, 
                   &config_.send_buffer_size, sizeof(config_.send_buffer_size));
        setsockopt(sock_, SOL_SOCKET, SO_RCVBUF,
                   &config_.recv_buffer_size, sizeof(config_.recv_buffer_size));
        
#ifdef __linux__
        // Linux-specific optimizations
        int quick_ack = 1;
        setsockopt(sock_, IPPROTO_TCP, TCP_QUICKACK, &quick_ack, sizeof(quick_ack));
        
        int user_timeout = 1000; // 1 second
        setsockopt(sock_, IPPROTO_TCP, TCP_USER_TIMEOUT, &user_timeout, sizeof(user_timeout));
#endif
    }
    
    void close_socket() {
        if (sock_ >= 0) {
#ifdef _WIN32
            closesocket(sock_);
            WSACleanup();
#else
            ::close(sock_);
#endif
            sock_ = -1;
        }
    }
    
    int build_new_order_fix(const hft::Order& order, char* buffer, size_t buffer_size) {
        uint32_t seq = sequence_number_.fetch_add(1, std::memory_order_relaxed);
        
        // Build FIX message manually for maximum performance
        int len = snprintf(buffer, buffer_size,
            "8=FIX.4.4\x01" "9=000\x01" "35=D\x01" "34=%u\x01" "49=TRADER\x01" "56=EXCHANGE\x01"
            "52=%020llu\x01" "11=%u\x01" "55=%u\x01" "54=%c\x01" "38=%llu\x01" "44=%llu\x01"
            "40=2\x01" "59=0\x01" "10=000\x01",
            seq,
            static_cast<unsigned long long>(TimeUtils::now_ns()),
            seq, // ClOrdID = sequence number
            static_cast<uint32_t>(order.symbol),
            (order.side == Side::BUY) ? '1' : '2',
            static_cast<unsigned long long>(order.qty),
            static_cast<unsigned long long>(order.price));
        
        if (len <= 0 || len >= static_cast<int>(buffer_size)) return 0;
        
        // Calculate and update body length
        char* body_start = strchr(buffer, '\x01') + 1; // After 8=FIX.4.4
        char* checksum_start = strstr(buffer, "10=");
        int body_len = checksum_start - body_start;
        
        // Update body length field
        char temp[16];
        snprintf(temp, sizeof(temp), "9=%03d\x01", body_len);
        memcpy(body_start, temp, 6);
        
        // Calculate checksum
        int checksum = 0;
        for (char* p = buffer; p < checksum_start; ++p) {
            checksum += static_cast<unsigned char>(*p);
        }
        checksum %= 256;
        
        // Update checksum
        snprintf(checksum_start, buffer_size - (checksum_start - buffer), 
                 "10=%03d\x01", checksum);
        
        return strlen(buffer);
    }
    
    void receive_loop() {
        char buffer[4096];
        std::string message_buffer;
        
        while (connected_.load(std::memory_order_relaxed)) {
            ssize_t received = ::recv(sock_, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (received <= 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    TimeUtils::spin_delay(100);
                    continue;
                }
                connected_.store(false, std::memory_order_release);
                break;
            }
            
            message_buffer.append(buffer, received);
            process_received_messages(message_buffer);
        }
    }
    
    void process_received_messages(std::string& buffer) {
        size_t pos = 0;
        while (pos < buffer.length()) {
            // Find complete FIX message (ends with \x01)
            size_t msg_end = buffer.find('\x01', pos);
            if (msg_end == std::string::npos) break;
            
            std::string message = buffer.substr(pos, msg_end - pos + 1);
            if (execution_callback_) {
                execution_callback_(message);
            }
            
            pos = msg_end + 1;
        }
        
        // Remove processed messages from buffer
        if (pos > 0) {
            buffer.erase(0, pos);
        }
    }
};

// -----------------------------------------------------------------------------
// Market Data Manager (Orchestrator)
// -----------------------------------------------------------------------------

class MarketDataManager {
public:
    struct ManagerConfig {
        std::vector<MarketDataFeed::FeedConfig> feeds;
        OrderEntryGateway::GatewayConfig gateway;
        bool enable_order_book = true;
        int processing_threads = 2;
    };
    
    using OrderBookCallback = std::function<void(uint32_t symbol_id, const L2Update&)>;
    
    MarketDataManager() : running_(false) {}
    
    ~MarketDataManager() {
        stop();
    }
    
    bool start(const ManagerConfig& config) {
        config_ = config;
        
        // Initialize lock-free queues
        l2_queue_ = std::make_unique<MpmcBatchQueue<L2Update, 8192>>();
        order_queue_ = std::make_unique<MpmcBatchQueue<hft::Order, 8192>>();
        
        // Start market data feeds
        for (const auto& feed_config : config.feeds) {
            auto feed = std::make_unique<MarketDataFeed>();
            feed->attach_l2_queue(l2_queue_.get());
            feed->attach_order_queue(order_queue_.get());
            
            if (!feed->start(feed_config)) {
                std::cerr << "Failed to start feed: " << feed_config.name << std::endl;
                continue;
            }
            
            feeds_.push_back(std::move(feed));
        }
        
        // Connect to order entry gateway
        if (!gateway_.connect(config.gateway)) {
            std::cerr << "Failed to connect to order entry gateway" << std::endl;
            stop();
            return false;
        }
        
        // Start processing threads
        running_.store(true, std::memory_order_release);
        for (int i = 0; i < config.processing_threads; ++i) {
            processing_threads_.emplace_back([this, i]() { 
                this->processing_loop(i); 
            });
        }
        
        return true;
    }
    
    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        
        // Stop all feeds
        feeds_.clear();
        
        // Disconnect gateway
        gateway_.disconnect();
        
        // Stop processing threads
        for (auto& thread : processing_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        processing_threads_.clear();
    }
    
    void set_orderbook_callback(OrderBookCallback cb) {
        orderbook_callback_ = std::move(cb);
    }
    
    // Submit order for execution
    bool submit_order(const hft::Order& order) {
        return gateway_.send_new_order(order);
    }
    
    // Get statistics
    void get_statistics() const {
        // Implementation for gathering and displaying statistics
    }

private:
    ManagerConfig config_;
    std::vector<std::unique_ptr<MarketDataFeed>> feeds_;
    OrderEntryGateway gateway_;
    
    // Lock-free queues for inter-thread communication
    std::unique_ptr<MpmcBatchQueue<L2Update, 8192>> l2_queue_;
    std::unique_ptr<MpmcBatchQueue<hft::Order, 8192>> order_queue_;
    
    // Processing threads
    std::vector<std::thread> processing_threads_;
    std::atomic<bool> running_;
    
    // Callbacks
    OrderBookCallback orderbook_callback_;
    
    void processing_loop(int thread_id) {
        L2Update l2_updates[32];
        hft::Order orders[32];
        
        while (running_.load(std::memory_order_relaxed)) {
            // Process L2 updates
            size_t l2_count = l2_queue_ ? l2_queue_->dequeue_batch(l2_updates, 32) : 0;
            for (size_t i = 0; i < l2_count; ++i) {
                process_l2_update(l2_updates[i]);
            }
            
            // Process orders
            size_t order_count = order_queue_ ? order_queue_->dequeue_batch(orders, 32) : 0;
            for (size_t i = 0; i < order_count; ++i) {
                process_order(orders[i]);
            }
            
            // Yield CPU if no work
            if (l2_count == 0 && order_count == 0) {
                std::this_thread::yield();
            }
        }
    }
    
    void process_l2_update(const L2Update& update) {
        if (orderbook_callback_) {
            orderbook_callback_(update.symbol_id, update);
        }
    }
    
    void process_order(const hft::Order& order) {
        // Process incoming order (from market data feeds)
        // This could be forwarded to a matching engine or order book
    }
};

} // namespace net
} // namespace hft