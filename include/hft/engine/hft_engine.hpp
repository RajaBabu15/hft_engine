#pragma once

#include "hft/core/types.hpp"
#include "hft/core/clock.hpp"
#include "hft/core/lock_free_queue.hpp"
#include "hft/core/admission_control.hpp"
#include "hft/matching/matching_engine.hpp"
#include "hft/fix/fix_parser.hpp"
#include "hft/strategies/market_making.hpp"
#include "hft/backtesting/tick_replay.hpp"
#include "hft/testing/stress_test.hpp"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace hft {
namespace engine {

// ============================================================================
// HFT Engine Configuration
// ============================================================================

struct HftEngineConfig {
    // Core engine settings
    size_t num_worker_threads{8};
    size_t num_matching_threads{4};
    size_t num_market_data_threads{2};
    size_t num_fix_workers{4};
    
    // Queue configurations
    size_t order_queue_size{8192};
    size_t market_data_queue_size{16384};
    size_t fix_message_queue_size{4096};
    size_t execution_report_queue_size{4096};
    
    // Performance settings  
    bool enable_admission_control{true};
    double target_p99_latency_us{100.0};
    double max_throughput_ops_per_sec{100000.0};
    bool enable_redis_caching{true};
    bool enable_latency_monitoring{true};
    
    // Risk management
    bool enable_risk_checks{true};
    double max_position_limit{1000000.0};
    double max_order_value{100000.0};
    double max_daily_loss{50000.0};
    
    // Market data
    std::vector<core::Symbol> symbols{"AAPL", "MSFT", "GOOGL", "TSLA", "AMZN", "META", "NVDA", "NFLX"};
    bool enable_market_data_simulation{true};
    double market_data_frequency_hz{1000.0};
    double base_volatility{0.02};
    
    // FIX protocol
    std::string fix_sender_comp_id{"HFT_ENGINE"};
    std::string fix_target_comp_id{"EXCHANGE"};
    std::string fix_version{"FIX.4.4"};
    
    // Strategies
    bool enable_market_making{true};
    double market_making_spread_bps{5.0};
    core::Quantity default_quote_size{100};
    
    // Logging and monitoring
    bool enable_detailed_logging{false};
    std::string log_file_path{"hft_engine.log"};
    bool enable_performance_monitoring{true};
    uint64_t monitoring_interval_ms{1000};
    
    HftEngineConfig() = default;
};

// ============================================================================
// Performance Monitoring
// ============================================================================

struct EnginePerformanceMetrics {
    // Throughput metrics
    std::atomic<uint64_t> orders_processed{0};
    std::atomic<uint64_t> market_data_processed{0};
    std::atomic<uint64_t> fix_messages_processed{0};
    std::atomic<uint64_t> executions_generated{0};
    
    // Latency metrics (microseconds)
    std::atomic<double> avg_order_latency_us{0.0};
    std::atomic<double> p99_order_latency_us{0.0};
    std::atomic<double> avg_market_data_latency_us{0.0};
    std::atomic<double> p99_market_data_latency_us{0.0};
    std::atomic<double> avg_fix_parse_latency_us{0.0};
    
    // System resource metrics
    std::atomic<double> cpu_usage_percent{0.0};
    std::atomic<uint64_t> memory_usage_mb{0};
    std::atomic<size_t> active_thread_count{0};
    
    // Trading metrics
    std::atomic<double> total_pnl{0.0};
    std::atomic<double> realized_pnl{0.0};
    std::atomic<double> unrealized_pnl{0.0};
    std::atomic<uint64_t> total_trades{0};
    std::atomic<double> total_volume{0.0};
    
    // Queue metrics
    std::atomic<size_t> order_queue_depth{0};
    std::atomic<size_t> market_data_queue_depth{0};
    std::atomic<size_t> fix_queue_depth{0};
    
    // Error metrics
    std::atomic<uint64_t> order_rejections{0};
    std::atomic<uint64_t> parsing_errors{0};
    std::atomic<uint64_t> risk_violations{0};
    std::atomic<uint64_t> system_errors{0};
    
    void reset() {
        orders_processed = 0;
        market_data_processed = 0;
        fix_messages_processed = 0;
        executions_generated = 0;
        avg_order_latency_us = 0.0;
        p99_order_latency_us = 0.0;
        avg_market_data_latency_us = 0.0;
        p99_market_data_latency_us = 0.0;
        avg_fix_parse_latency_us = 0.0;
        cpu_usage_percent = 0.0;
        memory_usage_mb = 0;
        active_thread_count = 0;
        total_pnl = 0.0;
        realized_pnl = 0.0;
        unrealized_pnl = 0.0;
        total_trades = 0;
        total_volume = 0.0;
        order_queue_depth = 0;
        market_data_queue_depth = 0;
        fix_queue_depth = 0;
        order_rejections = 0;
        parsing_errors = 0;
        risk_violations = 0;
        system_errors = 0;
    }
    
    // Calculate derived metrics
    double get_orders_per_second() const {
        static auto start_time = core::HighResolutionClock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            core::HighResolutionClock::now() - start_time).count();
        return elapsed > 0 ? static_cast<double>(orders_processed.load()) / elapsed : 0.0;
    }
    
    double get_market_data_per_second() const {
        static auto start_time = core::HighResolutionClock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            core::HighResolutionClock::now() - start_time).count();
        return elapsed > 0 ? static_cast<double>(market_data_processed.load()) / elapsed : 0.0;
    }
    
    double get_error_rate() const {
        uint64_t total_processed = orders_processed.load() + market_data_processed.load();
        uint64_t total_errors = order_rejections.load() + parsing_errors.load() + 
                               risk_violations.load() + system_errors.load();
        return total_processed > 0 ? static_cast<double>(total_errors) / total_processed : 0.0;
    }
};

// ============================================================================
// Main HFT Engine Class
// ============================================================================

class HftEngine {
private:
    // Configuration
    HftEngineConfig config_;
    
    // Core components
    std::unique_ptr<matching::MatchingEngine> matching_engine_;
    std::unique_ptr<fix::FixParser> fix_parser_;
    std::unique_ptr<core::AdaptiveAdmissionControl> admission_control_;
    
    // Market data simulation
    std::unique_ptr<backtesting::MarketDataSimulator> market_data_simulator_;
    std::unique_ptr<backtesting::TickReplayEngine> tick_replay_engine_;
    
    // Strategies
    std::vector<std::unique_ptr<strategies::MarketMakingStrategy>> strategies_;
    
    // Threading
    std::atomic<bool> running_{false};
    std::vector<std::thread> worker_threads_;
    std::thread monitoring_thread_;
    std::thread market_data_thread_;
    
    // Message queues
    std::unique_ptr<core::LockFreeQueue<std::string, 4096>> fix_message_queue_;
    std::unique_ptr<core::LockFreeQueue<core::MarketDataTick, 16384>> market_data_queue_;
    std::unique_ptr<core::LockFreeQueue<matching::ExecutionReport, 4096>> execution_queue_;
    
    // Performance monitoring
    EnginePerformanceMetrics metrics_;
    std::unique_ptr<core::PerformanceCounter> latency_counter_;
    std::atomic<core::TimePoint> last_monitoring_time_;
    
    // State management
    std::unordered_map<core::Symbol, core::MarketDataTick> latest_market_data_;
    std::unordered_map<core::Symbol, strategies::Position> consolidated_positions_;
    std::mutex state_mutex_;
    
public:
    explicit HftEngine(const HftEngineConfig& config = HftEngineConfig{});
    ~HftEngine();
    
    // Non-copyable, non-movable
    HftEngine(const HftEngine&) = delete;
    HftEngine& operator=(const HftEngine&) = delete;
    HftEngine(HftEngine&&) = delete;
    HftEngine& operator=(HftEngine&&) = delete;
    
    // Engine lifecycle
    void start();
    void stop();
    void restart();
    bool is_running() const { return running_.load(); }
    
    // Configuration
    void update_config(const HftEngineConfig& config);
    const HftEngineConfig& get_config() const { return config_; }
    
    // Market data interface
    void feed_market_data(const core::MarketDataTick& tick);
    void feed_fix_message(const std::string& fix_message);
    void start_market_data_simulation();
    void stop_market_data_simulation();
    
    // Trading interface
    bool submit_order(const order::Order& order);
    bool cancel_order(core::OrderID order_id);
    bool modify_order(core::OrderID order_id, core::Price new_price, core::Quantity new_quantity);
    
    // Strategy management
    void add_strategy(std::unique_ptr<strategies::MarketMakingStrategy> strategy);
    void remove_strategy(const std::string& strategy_name);
    void start_all_strategies();
    void stop_all_strategies();
    
    // Performance monitoring
    const EnginePerformanceMetrics& get_performance_metrics() const { return metrics_; }
    void reset_performance_metrics() { metrics_.reset(); }
    void print_performance_summary() const;
    void export_performance_data(const std::string& filename) const;
    
    // Risk management
    void set_position_limits(const core::Symbol& symbol, double max_position);
    void enable_emergency_stop();
    void disable_emergency_stop();
    bool is_emergency_stopped() const;
    
    // System health
    bool is_healthy() const;
    std::vector<std::string> get_health_warnings() const;
    void run_system_diagnostics() const;
    
    // Backtesting interface
    backtesting::BacktestMetrics run_backtest(
        const core::TimePoint& start_time,
        const core::TimePoint& end_time,
        const std::string& strategy_name = ""
    );
    
    // Stress testing
    testing::PerformanceMetrics run_stress_test(
        const testing::StressTestConfig& test_config = testing::StressTestConfig{}
    );
    
private:
    // Initialization
    void initialize_components();
    void initialize_queues();
    void initialize_strategies();
    void setup_callbacks();
    
    // Worker threads
    void market_data_worker();
    void fix_message_worker();
    void execution_report_worker();
    void monitoring_worker();
    
    // Message processing
    void process_market_data_tick(const core::MarketDataTick& tick);
    void process_fix_message(const std::string& fix_message);
    void process_execution_report(const matching::ExecutionReport& report);
    
    // Strategy coordination
    void distribute_market_data_to_strategies(const core::MarketDataTick& tick);
    void distribute_execution_to_strategies(const matching::ExecutionReport& report);
    
    // Performance monitoring helpers
    void update_latency_metrics(const std::string& metric_name, double latency_us);
    void update_throughput_metrics();
    void update_system_resource_metrics();
    void collect_queue_depth_metrics();
    
    // Risk management
    bool perform_pre_trade_risk_checks(const order::Order& order) const;
    bool perform_position_risk_checks(const core::Symbol& symbol, 
                                    core::Side side, core::Quantity quantity) const;
    void update_consolidated_positions();
    
    // Error handling
    void handle_system_error(const std::string& error_type, const std::string& message);
    void log_performance_warning(const std::string& warning);
    
    // System utilities
    void pin_threads_to_cores();
    void optimize_system_settings();
    double get_current_cpu_usage() const;
    uint64_t get_current_memory_usage() const;
};

// ============================================================================
// HFT Engine Builder
// ============================================================================

class HftEngineBuilder {
private:
    HftEngineConfig config_;
    std::vector<std::unique_ptr<strategies::MarketMakingStrategy>> strategies_;
    std::unique_ptr<backtesting::TickDataSource> tick_data_source_;
    
public:
    HftEngineBuilder() = default;
    
    // Configuration methods
    HftEngineBuilder& with_symbols(const std::vector<core::Symbol>& symbols);
    HftEngineBuilder& with_threading(size_t workers, size_t matching_threads);
    HftEngineBuilder& with_admission_control(double target_latency_us, double max_throughput);
    HftEngineBuilder& with_risk_limits(double max_position, double max_order_value);
    HftEngineBuilder& with_market_data_simulation(double frequency_hz, double volatility);
    HftEngineBuilder& with_redis_caching(bool enabled = true);
    HftEngineBuilder& with_detailed_logging(const std::string& log_file);
    
    // Strategy configuration
    HftEngineBuilder& add_simple_market_maker(double spread_bps = 5.0);
    HftEngineBuilder& add_advanced_market_maker(const strategies::MarketMakingStrategy::Parameters& params);
    HftEngineBuilder& add_statistical_arbitrage(const std::vector<core::Symbol>& universe);
    HftEngineBuilder& add_custom_strategy(std::unique_ptr<strategies::MarketMakingStrategy> strategy);
    
    // Data sources
    HftEngineBuilder& with_csv_data_source(const std::string& filename);
    HftEngineBuilder& with_simulated_data_source(core::TimePoint start_time, 
                                                 std::chrono::seconds duration);
    
    // Build engine
    std::unique_ptr<HftEngine> build();
    
    // Pre-configured engines
    static std::unique_ptr<HftEngine> build_development_engine();
    static std::unique_ptr<HftEngine> build_production_engine();
    static std::unique_ptr<HftEngine> build_backtesting_engine();
    static std::unique_ptr<HftEngine> build_stress_testing_engine();
};

// ============================================================================
// Integration Pipeline Runner
// ============================================================================

class IntegratedPipeline {
private:
    std::unique_ptr<HftEngine> engine_;
    std::unique_ptr<testing::StressTestEngine> stress_tester_;
    std::unique_ptr<strategies::StrategyBacktester> backtester_;
    
    // Pipeline configuration
    struct PipelineConfig {
        bool run_unit_tests{true};
        bool run_integration_tests{true};
        bool run_stress_tests{true};
        bool run_backtests{true};
        bool generate_reports{true};
        std::string output_directory{"./pipeline_results"};
    };
    
    PipelineConfig pipeline_config_;
    
public:
    explicit IntegratedPipeline(std::unique_ptr<HftEngine> engine);
    
    // Pipeline execution
    struct PipelineResults {
        bool unit_tests_passed;
        bool integration_tests_passed;
        bool stress_tests_passed;
        bool backtests_passed;
        
        testing::PerformanceMetrics stress_test_results;
        backtesting::BacktestMetrics backtest_results;
        EnginePerformanceMetrics engine_metrics;
        
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
        
        bool overall_success() const {
            return unit_tests_passed && integration_tests_passed && 
                   stress_tests_passed && backtests_passed;
        }
    };
    
    PipelineResults run_full_pipeline();
    PipelineResults run_validation_suite();
    PipelineResults run_performance_suite();
    
    // Individual test phases
    bool run_unit_tests();
    bool run_integration_tests();
    testing::PerformanceMetrics run_stress_tests();
    backtesting::BacktestMetrics run_backtests();
    
    // Configuration
    void set_pipeline_config(const PipelineConfig& config);
    void set_output_directory(const std::string& directory);
    
    // Reporting
    void generate_comprehensive_report(const PipelineResults& results, 
                                     const std::string& filename) const;
    void print_pipeline_summary(const PipelineResults& results) const;
    
private:
    void setup_output_directory();
    void run_system_validation();
    void validate_performance_targets(const testing::PerformanceMetrics& metrics);
    void validate_backtest_results(const backtesting::BacktestMetrics& metrics);
};

// ============================================================================
// Utility Functions
// ============================================================================

// Factory functions for common configurations
std::unique_ptr<HftEngine> create_hft_engine_for_symbol(const core::Symbol& symbol);
std::unique_ptr<HftEngine> create_multi_asset_hft_engine(const std::vector<core::Symbol>& symbols);
std::unique_ptr<HftEngine> create_backtesting_hft_engine(
    const std::string& data_file,
    const core::TimePoint& start_time,
    const core::TimePoint& end_time
);

// Configuration validation
bool validate_hft_config(const HftEngineConfig& config);
std::vector<std::string> get_config_warnings(const HftEngineConfig& config);

// System optimization
void optimize_system_for_hft_trading();
void set_thread_affinity(std::thread& thread, int core_id);
void configure_memory_settings();

// Performance utilities
void benchmark_engine_performance(HftEngine& engine, std::chrono::seconds duration);
void profile_engine_latency(HftEngine& engine, const std::string& output_file);
void measure_throughput_capacity(HftEngine& engine, uint64_t target_ops_per_second);

} // namespace engine
} // namespace hft