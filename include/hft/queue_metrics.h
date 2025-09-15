#pragma once

#include "hft/types.h"
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <iomanip>

namespace hft {

struct QueueOperation {
    uint64_t timestamp_ns;
    uint32_t queue_id;
    uint32_t operation_type; // 0=enqueue, 1=dequeue, 2=peek
    uint64_t latency_ns;
    uint32_t queue_depth_before;
    uint32_t queue_depth_after;
    bool was_blocked;
};

class QueueMetrics {
private:
    struct PerQueueStats {
        std::atomic<uint64_t> enqueue_count{0};
        std::atomic<uint64_t> dequeue_count{0};
        std::atomic<uint64_t> blocked_operations{0};
        std::atomic<uint64_t> total_latency_ns{0};
        std::atomic<uint64_t> max_latency_ns{0};
        std::atomic<uint64_t> min_latency_ns{UINT64_MAX};
        std::atomic<uint64_t> max_depth{0};
        std::atomic<uint64_t> current_depth{0};
    };

    static constexpr uint32_t MAX_QUEUES = 64;
    static constexpr uint32_t HISTORY_SIZE = 10000;
    
    std::array<PerQueueStats, MAX_QUEUES> queue_stats_;
    std::vector<QueueOperation> operation_history_;
    std::mutex history_mutex_;
    std::atomic<uint64_t> total_operations_{0};
    std::atomic<uint64_t> start_time_ns_{0};
    
    uint64_t now_ns() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

public:
    QueueMetrics() {
        start_time_ns_.store(now_ns(), std::memory_order_relaxed);
        operation_history_.reserve(HISTORY_SIZE);
    }

    void record_enqueue(uint32_t queue_id, uint64_t latency_ns, 
                       uint32_t depth_before, uint32_t depth_after, 
                       bool was_blocked = false) {
        if (queue_id >= MAX_QUEUES) return;
        
        auto& stats = queue_stats_[queue_id];
        stats.enqueue_count.fetch_add(1, std::memory_order_relaxed);
        stats.total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
        stats.current_depth.store(depth_after, std::memory_order_relaxed);
        
        // Update max/min latency
        uint64_t current_max = stats.max_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns > current_max && 
               !stats.max_latency_ns.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed));
        
        uint64_t current_min = stats.min_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns < current_min && 
               !stats.min_latency_ns.compare_exchange_weak(current_min, latency_ns, std::memory_order_relaxed));
        
        // Update max depth
        uint64_t current_max_depth = stats.max_depth.load(std::memory_order_relaxed);
        while (depth_after > current_max_depth && 
               !stats.max_depth.compare_exchange_weak(current_max_depth, depth_after, std::memory_order_relaxed));
        
        if (was_blocked) {
            stats.blocked_operations.fetch_add(1, std::memory_order_relaxed);
        }
        
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        
        // Record in history (with size limit)
        record_operation(queue_id, 0, latency_ns, depth_before, depth_after, was_blocked);
    }

    void record_dequeue(uint32_t queue_id, uint64_t latency_ns, 
                       uint32_t depth_before, uint32_t depth_after,
                       bool was_blocked = false) {
        if (queue_id >= MAX_QUEUES) return;
        
        auto& stats = queue_stats_[queue_id];
        stats.dequeue_count.fetch_add(1, std::memory_order_relaxed);
        stats.total_latency_ns.fetch_add(latency_ns, std::memory_order_relaxed);
        stats.current_depth.store(depth_after, std::memory_order_relaxed);
        
        // Update latency stats
        uint64_t current_max = stats.max_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns > current_max && 
               !stats.max_latency_ns.compare_exchange_weak(current_max, latency_ns, std::memory_order_relaxed));
        
        uint64_t current_min = stats.min_latency_ns.load(std::memory_order_relaxed);
        while (latency_ns < current_min && 
               !stats.min_latency_ns.compare_exchange_weak(current_min, latency_ns, std::memory_order_relaxed));
        
        if (was_blocked) {
            stats.blocked_operations.fetch_add(1, std::memory_order_relaxed);
        }
        
        total_operations_.fetch_add(1, std::memory_order_relaxed);
        
        record_operation(queue_id, 1, latency_ns, depth_before, depth_after, was_blocked);
    }

    struct QueueStats {
        uint32_t queue_id;
        uint64_t enqueues;
        uint64_t dequeues;
        uint64_t blocked_ops;
        double avg_latency_ns;
        uint64_t min_latency_ns;
        uint64_t max_latency_ns;
        uint64_t max_depth;
        uint64_t current_depth;
        double throughput_ops_per_sec;
        double block_rate_pct;
    };

    QueueStats get_queue_stats(uint32_t queue_id) const {
        if (queue_id >= MAX_QUEUES) {
            return {queue_id, 0, 0, 0, 0.0, 0, 0, 0, 0, 0.0, 0.0};
        }
        
        const auto& stats = queue_stats_[queue_id];
        uint64_t enqueues = stats.enqueue_count.load(std::memory_order_relaxed);
        uint64_t dequeues = stats.dequeue_count.load(std::memory_order_relaxed);
        uint64_t blocked = stats.blocked_operations.load(std::memory_order_relaxed);
        uint64_t total_latency = stats.total_latency_ns.load(std::memory_order_relaxed);
        uint64_t total_ops = enqueues + dequeues;
        
        double avg_latency = total_ops > 0 ? static_cast<double>(total_latency) / total_ops : 0.0;
        
        uint64_t elapsed_ns = now_ns() - start_time_ns_.load(std::memory_order_relaxed);
        double elapsed_sec = static_cast<double>(elapsed_ns) / 1e9;
        double throughput = elapsed_sec > 0 ? total_ops / elapsed_sec : 0.0;
        
        double block_rate = total_ops > 0 ? static_cast<double>(blocked) / total_ops * 100.0 : 0.0;
        
        return {
            queue_id,
            enqueues,
            dequeues,
            blocked,
            avg_latency,
            stats.min_latency_ns.load(std::memory_order_relaxed),
            stats.max_latency_ns.load(std::memory_order_relaxed),
            stats.max_depth.load(std::memory_order_relaxed),
            stats.current_depth.load(std::memory_order_relaxed),
            throughput,
            block_rate
        };
    }

    struct SystemStats {
        uint64_t total_operations;
        double system_throughput_ops_per_sec;
        uint64_t total_blocked_operations;
        double system_block_rate_pct;
        uint32_t active_queues;
        uint64_t uptime_ms;
    };

    SystemStats get_system_stats() const {
        uint64_t total_ops = total_operations_.load(std::memory_order_relaxed);
        uint64_t total_blocked = 0;
        uint32_t active_queues = 0;
        
        for (uint32_t i = 0; i < MAX_QUEUES; ++i) {
            const auto& stats = queue_stats_[i];
            uint64_t queue_ops = stats.enqueue_count.load(std::memory_order_relaxed) + 
                                stats.dequeue_count.load(std::memory_order_relaxed);
            if (queue_ops > 0) {
                active_queues++;
                total_blocked += stats.blocked_operations.load(std::memory_order_relaxed);
            }
        }
        
        uint64_t elapsed_ns = now_ns() - start_time_ns_.load(std::memory_order_relaxed);
        double elapsed_sec = static_cast<double>(elapsed_ns) / 1e9;
        double throughput = elapsed_sec > 0 ? total_ops / elapsed_sec : 0.0;
        double block_rate = total_ops > 0 ? static_cast<double>(total_blocked) / total_ops * 100.0 : 0.0;
        
        return {
            total_ops,
            throughput,
            total_blocked,
            block_rate,
            active_queues,
            elapsed_ns / 1000000  // Convert to ms
        };
    }

    std::vector<QueueOperation> get_recent_operations(uint32_t limit = 100) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(history_mutex_));
        
        if (operation_history_.size() <= limit) {
            return operation_history_;
        }
        
        return std::vector<QueueOperation>(
            operation_history_.end() - limit, 
            operation_history_.end()
        );
    }

    void print_detailed_report() const {
        std::cout << "\n📊 COMPREHENSIVE QUEUE METRICS REPORT" << std::endl;
        std::cout << "======================================" << std::endl;
        
        auto sys_stats = get_system_stats();
        std::cout << "🔧 System Overview:" << std::endl;
        std::cout << "   Total Operations: " << sys_stats.total_operations << std::endl;
        std::cout << "   System Throughput: " << std::fixed << std::setprecision(0) 
                  << sys_stats.system_throughput_ops_per_sec << " ops/sec" << std::endl;
        std::cout << "   Active Queues: " << sys_stats.active_queues << std::endl;
        std::cout << "   System Block Rate: " << std::fixed << std::setprecision(2) 
                  << sys_stats.system_block_rate_pct << "%" << std::endl;
        std::cout << "   Uptime: " << sys_stats.uptime_ms << "ms" << std::endl;
        
        std::cout << "\n📈 Per-Queue Statistics:" << std::endl;
        std::cout << std::setw(5) << "QID" 
                  << std::setw(12) << "Enqueues"
                  << std::setw(12) << "Dequeues" 
                  << std::setw(10) << "Blocked"
                  << std::setw(12) << "Avg Lat(ns)"
                  << std::setw(12) << "Max Lat(ns)"
                  << std::setw(10) << "Max Depth"
                  << std::setw(10) << "Cur Depth"
                  << std::setw(12) << "Thru(ops/s)"
                  << std::setw(10) << "Block%" << std::endl;
        std::cout << std::string(115, '-') << std::endl;
        
        for (uint32_t i = 0; i < MAX_QUEUES; ++i) {
            auto stats = get_queue_stats(i);
            if (stats.enqueues + stats.dequeues > 0) {
                std::cout << std::setw(5) << i
                          << std::setw(12) << stats.enqueues
                          << std::setw(12) << stats.dequeues
                          << std::setw(10) << stats.blocked_ops
                          << std::setw(12) << std::fixed << std::setprecision(0) << stats.avg_latency_ns
                          << std::setw(12) << stats.max_latency_ns
                          << std::setw(10) << stats.max_depth
                          << std::setw(10) << stats.current_depth
                          << std::setw(12) << std::fixed << std::setprecision(0) << stats.throughput_ops_per_sec
                          << std::setw(10) << std::fixed << std::setprecision(1) << stats.block_rate_pct
                          << std::endl;
            }
        }
        
        // Latency percentiles for active queues
        std::cout << "\n⚡ Latency Analysis (Active Queues):" << std::endl;
        for (uint32_t i = 0; i < MAX_QUEUES; ++i) {
            auto stats = get_queue_stats(i);
            if (stats.enqueues + stats.dequeues > 0) {
                std::cout << "   Queue " << i << ": "
                          << "Min=" << stats.min_latency_ns << "ns, "
                          << "Avg=" << std::fixed << std::setprecision(0) << stats.avg_latency_ns << "ns, "
                          << "Max=" << stats.max_latency_ns << "ns" << std::endl;
            }
        }
    }

    void reset_stats() {
        for (auto& stats : queue_stats_) {
            stats.enqueue_count.store(0, std::memory_order_relaxed);
            stats.dequeue_count.store(0, std::memory_order_relaxed);
            stats.blocked_operations.store(0, std::memory_order_relaxed);
            stats.total_latency_ns.store(0, std::memory_order_relaxed);
            stats.max_latency_ns.store(0, std::memory_order_relaxed);
            stats.min_latency_ns.store(UINT64_MAX, std::memory_order_relaxed);
            stats.max_depth.store(0, std::memory_order_relaxed);
            stats.current_depth.store(0, std::memory_order_relaxed);
        }
        
        total_operations_.store(0, std::memory_order_relaxed);
        start_time_ns_.store(now_ns(), std::memory_order_relaxed);
        
        std::lock_guard<std::mutex> lock(history_mutex_);
        operation_history_.clear();
    }

private:
    void record_operation(uint32_t queue_id, uint32_t op_type, uint64_t latency_ns,
                         uint32_t depth_before, uint32_t depth_after, bool was_blocked) {
        std::lock_guard<std::mutex> lock(history_mutex_);
        
        // Circular buffer behavior
        if (operation_history_.size() >= HISTORY_SIZE) {
            operation_history_.erase(operation_history_.begin(), 
                                   operation_history_.begin() + HISTORY_SIZE/4);
        }
        
        operation_history_.push_back({
            now_ns(),
            queue_id,
            op_type,
            latency_ns,
            depth_before,
            depth_after,
            was_blocked
        });
    }
};

// Global queue metrics instance declaration (define in your main file)
// extern QueueMetrics g_queue_metrics;

// RAII helper for measuring queue operation latency
class QueueLatencyMeasurer {
private:
    uint64_t start_time_;
    uint32_t queue_id_;
    uint32_t operation_type_;
    uint32_t depth_before_;
    QueueMetrics* metrics_;
    
public:
    QueueLatencyMeasurer(uint32_t queue_id, uint32_t op_type, uint32_t depth_before, QueueMetrics* metrics = nullptr)
        : queue_id_(queue_id), operation_type_(op_type), depth_before_(depth_before), metrics_(metrics) {
        start_time_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    ~QueueLatencyMeasurer() {
        if (metrics_) {
            uint64_t end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            uint64_t latency = end_time - start_time_;
            
            // Record with default parameters
            if (operation_type_ == 0) {
                metrics_->record_enqueue(queue_id_, latency, depth_before_, depth_before_ + 1, false);
            } else {
                metrics_->record_dequeue(queue_id_, latency, depth_before_, depth_before_ - 1, false);
            }
        }
    }
    
    void finish(uint32_t depth_after, bool was_blocked = false) {
        if (metrics_) {
            uint64_t end_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            uint64_t latency = end_time - start_time_;
            
            if (operation_type_ == 0) {
                metrics_->record_enqueue(queue_id_, latency, depth_before_, depth_after, was_blocked);
            } else {
                metrics_->record_dequeue(queue_id_, latency, depth_before_, depth_after, was_blocked);
            }
        }
    }
};

} // namespace hft