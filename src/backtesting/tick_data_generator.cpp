#include "hft/backtesting/tick_replay.hpp"
#include <iostream>
#include <fstream>
#include <random>
#include <cmath>
namespace hft {
namespace backtesting {
class TickDataGenerator {
private:
    std::mt19937 gen_;
    std::uniform_real_distribution<> price_walk_dist_{-0.001, 0.001};
    std::uniform_real_distribution<> spread_dist_{0.0001, 0.0005};
    std::uniform_int_distribution<> size_dist_{100, 10000};
    std::uniform_real_distribution<> trade_prob_dist_{0.0, 1.0};
public:
    TickDataGenerator() : gen_(std::random_device{}()) {}
    bool generate_csv_data(const std::string& filename,
                          const std::string& symbol,
                          double initial_price,
                          size_t num_ticks,
                          double trade_probability = 0.1) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to create CSV file: " << filename << std::endl;
            return false;
        }
        file << "timestamp_ns,symbol,bid_price,ask_price,last_price,bid_size,ask_size,last_size\n";
        double current_price = initial_price;
        uint64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        std::cout << "Generating " << num_ticks << " ticks for " << symbol
                  << " starting at $" << initial_price << std::endl;
        for (size_t i = 0; i < num_ticks; ++i) {
            current_price *= (1.0 + price_walk_dist_(gen_));
            current_price = std::max(0.01, current_price);
            double spread = current_price * spread_dist_(gen_);
            double bid_price = current_price - spread/2.0;
            double ask_price = current_price + spread/2.0;
            uint64_t bid_size = size_dist_(gen_);
            uint64_t ask_size = size_dist_(gen_);
            double last_price = 0.0;
            uint64_t last_size = 0;
            if (trade_prob_dist_(gen_) < trade_probability) {
                bool buy_trade = trade_prob_dist_(gen_) < 0.5;
                last_price = buy_trade ? ask_price : bid_price;
                last_size = size_dist_(gen_) / 10;
            }
            file << timestamp_ns << ","
                 << symbol << ","
                 << std::fixed << std::setprecision(4) << bid_price << ","
                 << std::fixed << std::setprecision(4) << ask_price << ","
                 << std::fixed << std::setprecision(4) << last_price << ","
                 << bid_size << ","
                 << ask_size << ","
                 << last_size << "\n";
            timestamp_ns += 1000 + (gen_() % 100000);
            if (i % 10000 == 0) {
                std::cout << "Generated " << i << " ticks..." << std::endl;
            }
        }
        std::cout << "Successfully generated " << num_ticks << " ticks in " << filename << std::endl;
        return true;
    }
    bool generate_multi_symbol_data(const std::string& filename,
                                   const std::vector<std::pair<std::string, double>>& symbols,
                                   size_t ticks_per_symbol) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to create multi-symbol CSV file: " << filename << std::endl;
            return false;
        }
        file << "timestamp_ns,symbol,bid_price,ask_price,last_price,bid_size,ask_size,last_size\n";
        struct SymbolState {
            double current_price;
            uint64_t next_tick_time;
        };
        std::map<std::string, SymbolState> symbol_states;
        uint64_t base_time = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        for (const auto& [symbol, price] : symbols) {
            symbol_states[symbol] = {price, base_time + (gen_() % 1000000)};
        }
        std::vector<std::tuple<uint64_t, std::string, double, double, double, uint64_t, uint64_t, uint64_t>> all_ticks;
        for (const auto& [symbol, initial_price] : symbols) {
            auto& state = symbol_states[symbol];
            for (size_t i = 0; i < ticks_per_symbol; ++i) {
                state.current_price *= (1.0 + price_walk_dist_(gen_));
                state.current_price = std::max(0.01, state.current_price);
                double spread = state.current_price * spread_dist_(gen_);
                double bid_price = state.current_price - spread/2.0;
                double ask_price = state.current_price + spread/2.0;
                uint64_t bid_size = size_dist_(gen_);
                uint64_t ask_size = size_dist_(gen_);
                double last_price = 0.0;
                uint64_t last_size = 0;
                if (trade_prob_dist_(gen_) < 0.1) {
                    bool buy_trade = trade_prob_dist_(gen_) < 0.5;
                    last_price = buy_trade ? ask_price : bid_price;
                    last_size = size_dist_(gen_) / 10;
                }
                all_ticks.emplace_back(state.next_tick_time, symbol, bid_price, ask_price,
                                     last_price, bid_size, ask_size, last_size);
                state.next_tick_time += 1000 + (gen_() % 50000);
            }
        }
        std::sort(all_ticks.begin(), all_ticks.end());
        for (const auto& [timestamp, symbol, bid, ask, last, bid_sz, ask_sz, last_sz] : all_ticks) {
            file << timestamp << "," << symbol << ","
                 << std::fixed << std::setprecision(4) << bid << ","
                 << std::fixed << std::setprecision(4) << ask << ","
                 << std::fixed << std::setprecision(4) << last << ","
                 << bid_sz << "," << ask_sz << "," << last_sz << "\n";
        }
        std::cout << "Generated multi-symbol data with " << all_ticks.size() << " total ticks" << std::endl;
        return true;
    }
};
}
}
int main(int argc, char* argv[]) {
    using namespace hft::backtesting;
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <mode> [options]\n";
        std::cout << "Modes:\n";
        std::cout << "  single <filename> <symbol> <initial_price> <num_ticks>\n";
        std::cout << "  multi <filename> <num_ticks_per_symbol>\n";
        std::cout << "  demo - Generate demo data\n";
        return 1;
    }
    std::string mode = argv[1];
    TickDataGenerator generator;
    if (mode == "single" && argc >= 6) {
        std::string filename = argv[2];
        std::string symbol = argv[3];
        double initial_price = std::stod(argv[4]);
        size_t num_ticks = std::stoull(argv[5]);
        return generator.generate_csv_data(filename, symbol, initial_price, num_ticks) ? 0 : 1;
    } else if (mode == "multi" && argc >= 4) {
        std::string filename = argv[2];
        size_t ticks_per_symbol = std::stoull(argv[3]);
        std::vector<std::pair<std::string, double>> symbols = {
            {"AAPL", 150.00}, {"MSFT", 350.00}, {"GOOGL", 140.00},
            {"AMZN", 130.00}, {"TSLA", 180.00}, {"NVDA", 400.00}
        };
        return generator.generate_multi_symbol_data(filename, symbols, ticks_per_symbol) ? 0 : 1;
    } else if (mode == "demo") {
        std::cout << "Generating demo tick data files...\n";
        if (!generator.generate_csv_data("demo_aapl_ticks.csv", "AAPL", 150.00, 100000, 0.15)) {
            return 1;
        }
        std::vector<std::pair<std::string, double>> demo_symbols = {
            {"AAPL", 150.00}, {"MSFT", 350.00}, {"GOOGL", 140.00}, {"TSLA", 180.00}
        };
        if (!generator.generate_multi_symbol_data("demo_multi_ticks.csv", demo_symbols, 25000)) {
            return 1;
        }
        std::cout << "Demo files generated:\n";
        std::cout << "  - demo_aapl_ticks.csv (100K AAPL ticks)\n";
        std::cout << "  - demo_multi_ticks.csv (100K multi-symbol ticks)\n";
        return 0;
    } else {
        std::cerr << "Invalid arguments. Use --help for usage.\n";
        return 1;
    }
}
