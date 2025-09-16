#pragma once

#include "hft/types.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace hft {

// Real-time market data tick
struct MarketTick {
    Symbol symbol;
    uint64_t timestamp_ns;
    Price bid;
    Price ask;
    Quantity bid_size;
    Quantity ask_size;
    Price last_price;
    Quantity last_size;
    uint64_t volume;
    
    MarketTick() = default;
    MarketTick(Symbol sym, uint64_t ts, Price b, Price a, Quantity bs, Quantity as, 
               Price lp = 0, Quantity ls = 0, uint64_t vol = 0)
        : symbol(sym), timestamp_ns(ts), bid(b), ask(a), bid_size(bs), ask_size(as),
          last_price(lp), last_size(ls), volume(vol) {}
};

// Trade tick from real market data
struct TradeTick {
    Symbol symbol;
    uint64_t timestamp_ns;
    Price price;
    Quantity size;
    char conditions[8];  // Trade conditions
    
    TradeTick() = default;
    TradeTick(Symbol sym, uint64_t ts, Price p, Quantity s, const char* cond = "")
        : symbol(sym), timestamp_ns(ts), price(p), size(s) {
        strncpy(conditions, cond, sizeof(conditions) - 1);
        conditions[sizeof(conditions) - 1] = '\0';
    }
};

// Market data provider interface
class MarketDataProvider {
public:
    virtual ~MarketDataProvider() = default;
    
    // Callback types
    using TickCallback = std::function<void(const MarketTick&)>;
    using TradeCallback = std::function<void(const TradeTick&)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    
    // Configuration
    virtual bool initialize(const std::string& config) = 0;
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    
    // Subscriptions
    virtual bool subscribe_quotes(const std::vector<std::string>& symbols) = 0;
    virtual bool subscribe_trades(const std::vector<std::string>& symbols) = 0;
    virtual void unsubscribe_all() = 0;
    
    // Callbacks
    virtual void set_tick_callback(TickCallback callback) = 0;
    virtual void set_trade_callback(TradeCallback callback) = 0;
    virtual void set_error_callback(ErrorCallback callback) = 0;
    
    // Status
    virtual bool is_connected() const = 0;
    virtual std::string get_status() const = 0;
    
    // Historical data
    virtual std::vector<MarketTick> get_historical_quotes(
        const std::string& symbol, 
        const std::string& start_date,
        const std::string& end_date) = 0;
        
    virtual std::vector<TradeTick> get_historical_trades(
        const std::string& symbol,
        const std::string& start_date, 
        const std::string& end_date) = 0;
};

// HTTP client utility for REST APIs
class HttpClient {
private:
    CURL* curl_;
    struct curl_slist* headers_;
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
public:
    HttpClient() : curl_(curl_easy_init()), headers_(nullptr) {
        if (curl_) {
            curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        }
    }
    
    ~HttpClient() {
        if (headers_) {
            curl_slist_free_all(headers_);
        }
        if (curl_) {
            curl_easy_cleanup(curl_);
        }
    }
    
    void add_header(const std::string& header) {
        headers_ = curl_slist_append(headers_, header.c_str());
        if (curl_) {
            curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers_);
        }
    }
    
    std::string get(const std::string& url) {
        std::string response;
        if (curl_) {
            curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
            
            CURLcode res = curl_easy_perform(curl_);
            if (res != CURLE_OK) {
                throw std::runtime_error("HTTP GET failed: " + std::string(curl_easy_strerror(res)));
            }
        }
        return response;
    }
};

// Alpha Vantage provider implementation
class AlphaVantageProvider : public MarketDataProvider {
private:
    std::string api_key_;
    std::unique_ptr<HttpClient> http_client_;
    std::atomic<bool> connected_{false};
    
    TickCallback tick_callback_;
    TradeCallback trade_callback_;
    ErrorCallback error_callback_;
    
    std::thread polling_thread_;
    std::atomic<bool> running_{false};
    std::vector<std::string> subscribed_symbols_;
    
public:
    AlphaVantageProvider() : http_client_(std::make_unique<HttpClient>()) {}
    
    ~AlphaVantageProvider() {
        disconnect();
    }
    
    bool initialize(const std::string& config) override {
        try {
            auto json_config = nlohmann::json::parse(config);
            api_key_ = json_config["api_key"];
            return !api_key_.empty();
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("Failed to parse Alpha Vantage config: " + std::string(e.what()));
            }
            return false;
        }
    }
    
    bool connect() override {
        if (api_key_.empty()) return false;
        
        // Test connection with a simple API call
        try {
            std::string test_url = "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=AAPL&apikey=" + api_key_;
            std::string response = http_client_->get(test_url);
            
            auto json_response = nlohmann::json::parse(response);
            if (json_response.contains("Error Message")) {
                if (error_callback_) {
                    error_callback_("Alpha Vantage API error: " + json_response["Error Message"].get<std::string>());
                }
                return false;
            }
            
            connected_.store(true, std::memory_order_release);
            start_polling();
            return true;
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("Alpha Vantage connection failed: " + std::string(e.what()));
            }
            return false;
        }
    }
    
    void disconnect() override {
        running_.store(false, std::memory_order_release);
        if (polling_thread_.joinable()) {
            polling_thread_.join();
        }
        connected_.store(false, std::memory_order_release);
    }
    
    bool subscribe_quotes(const std::vector<std::string>& symbols) override {
        subscribed_symbols_ = symbols;
        return true;
    }
    
    bool subscribe_trades(const std::vector<std::string>& symbols) override {
        // Alpha Vantage doesn't provide real-time trades via free API
        return false;
    }
    
    void unsubscribe_all() override {
        subscribed_symbols_.clear();
    }
    
    void set_tick_callback(TickCallback callback) override {
        tick_callback_ = callback;
    }
    
    void set_trade_callback(TradeCallback callback) override {
        trade_callback_ = callback;
    }
    
    void set_error_callback(ErrorCallback callback) override {
        error_callback_ = callback;
    }
    
    bool is_connected() const override {
        return connected_.load(std::memory_order_acquire);
    }
    
    std::string get_status() const override {
        return is_connected() ? "Connected to Alpha Vantage" : "Disconnected";
    }
    
    std::vector<MarketTick> get_historical_quotes(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date) override {
        
        std::vector<MarketTick> ticks;
        
        try {
            // Use DAILY function for historical data
            std::string url = "https://www.alphavantage.co/query?function=TIME_SERIES_DAILY&symbol=" +
                             symbol + "&apikey=" + api_key_ + "&outputsize=full";
            
            std::string response = http_client_->get(url);
            auto json_data = nlohmann::json::parse(response);
            
            if (json_data.contains("Time Series (Daily)")) {
                auto time_series = json_data["Time Series (Daily)"];
                
                for (auto& [date, data] : time_series.items()) {
                    // Convert date to timestamp (simplified)
                    uint64_t timestamp = parse_date_to_ns(date);
                    
                    // Extract OHLC data and create tick
                    Price open = static_cast<Price>(std::stod(data["1. open"].get<std::string>()) * 10000);
                    Price high = static_cast<Price>(std::stod(data["2. high"].get<std::string>()) * 10000);
                    Price low = static_cast<Price>(std::stod(data["3. low"].get<std::string>()) * 10000);
                    Price close = static_cast<Price>(std::stod(data["4. close"].get<std::string>()) * 10000);
                    Quantity volume = static_cast<Quantity>(std::stoull(data["5. volume"].get<std::string>()));
                    
                    // Create multiple ticks for OHLC
                    ticks.emplace_back(string_to_symbol(symbol), timestamp, close - 1, close + 1, 100, 100, close, 0, volume);
                    
                    // Filter by date range if needed
                    if (date < start_date || date > end_date) continue;
                }
            }
            
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("Failed to fetch historical data: " + std::string(e.what()));
            }
        }
        
        return ticks;
    }
    
    std::vector<TradeTick> get_historical_trades(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date) override {
        
        // Alpha Vantage free tier doesn't provide tick-level trade data
        return {};
    }
    
private:
    void start_polling() {
        running_.store(true, std::memory_order_release);
        polling_thread_ = std::thread([this]() { polling_loop(); });
    }
    
    void polling_loop() {
        while (running_.load(std::memory_order_relaxed)) {
            try {
                for (const auto& symbol : subscribed_symbols_) {
                    if (!running_.load(std::memory_order_relaxed)) break;
                    
                    fetch_real_time_quote(symbol);
                    
                    // Rate limiting - Alpha Vantage allows 5 API requests per minute
                    std::this_thread::sleep_for(std::chrono::seconds(15));
                }
            } catch (const std::exception& e) {
                if (error_callback_) {
                    error_callback_("Polling error: " + std::string(e.what()));
                }
            }
            
            // Wait before next polling cycle
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    }
    
    void fetch_real_time_quote(const std::string& symbol) {
        try {
            std::string url = "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=" +
                             symbol + "&apikey=" + api_key_;
            
            std::string response = http_client_->get(url);
            auto json_data = nlohmann::json::parse(response);
            
            if (json_data.contains("Global Quote")) {
                auto quote = json_data["Global Quote"];
                
                Price price = static_cast<Price>(std::stod(quote["05. price"].get<std::string>()) * 10000);
                uint64_t timestamp = now_ns();
                
                // Simulate bid/ask spread (1 cent)
                Price bid = price - 5;
                Price ask = price + 5;
                
                MarketTick tick(string_to_symbol(symbol), timestamp, bid, ask, 100, 100, price, 0, 0);
                
                if (tick_callback_) {
                    tick_callback_(tick);
                }
            }
            
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("Failed to fetch quote for " + symbol + ": " + std::string(e.what()));
            }
        }
    }
    
    Symbol string_to_symbol(const std::string& symbol_str) const {
        // Simple hash function to convert string to Symbol (uint32_t)
        std::hash<std::string> hasher;
        return static_cast<Symbol>(hasher(symbol_str) % 100000) + 1;
    }
    
    uint64_t parse_date_to_ns(const std::string& date_str) const {
        // Simplified date parsing - in production use proper date/time library
        // Format: "YYYY-MM-DD"
        std::tm tm = {};
        std::istringstream ss(date_str);
        ss >> std::get_time(&tm, "%Y-%m-%d");
        
        auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
        return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
    }
};

// IEX Cloud provider (similar implementation)
class IEXProvider : public MarketDataProvider {
private:
    std::string api_token_;
    std::unique_ptr<HttpClient> http_client_;
    std::atomic<bool> connected_{false};
    
    TickCallback tick_callback_;
    TradeCallback trade_callback_;
    ErrorCallback error_callback_;
    
    std::vector<std::string> subscribed_symbols_;
    
public:
    IEXProvider() : http_client_(std::make_unique<HttpClient>()) {}
    
    bool initialize(const std::string& config) override {
        try {
            auto json_config = nlohmann::json::parse(config);
            api_token_ = json_config["api_token"];
            return !api_token_.empty();
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("Failed to parse IEX config: " + std::string(e.what()));
            }
            return false;
        }
    }
    
    bool connect() override {
        connected_.store(true, std::memory_order_release);
        return true;
    }
    
    void disconnect() override {
        connected_.store(false, std::memory_order_release);
    }
    
    bool subscribe_quotes(const std::vector<std::string>& symbols) override {
        subscribed_symbols_ = symbols;
        return true;
    }
    
    bool subscribe_trades(const std::vector<std::string>& symbols) override {
        return true;  // IEX supports trade data
    }
    
    void unsubscribe_all() override {
        subscribed_symbols_.clear();
    }
    
    void set_tick_callback(TickCallback callback) override { tick_callback_ = callback; }
    void set_trade_callback(TradeCallback callback) override { trade_callback_ = callback; }
    void set_error_callback(ErrorCallback callback) override { error_callback_ = callback; }
    
    bool is_connected() const override {
        return connected_.load(std::memory_order_acquire);
    }
    
    std::string get_status() const override {
        return is_connected() ? "Connected to IEX Cloud" : "Disconnected";
    }
    
    std::vector<MarketTick> get_historical_quotes(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date) override {
        
        std::vector<MarketTick> ticks;
        
        try {
            // IEX Cloud historical data endpoint
            std::string url = "https://cloud.iexapis.com/stable/stock/" + symbol + 
                             "/chart/date/" + start_date + "?token=" + api_token_;
            
            std::string response = http_client_->get(url);
            auto json_data = nlohmann::json::parse(response);
            
            for (const auto& bar : json_data) {
                uint64_t timestamp = parse_iex_timestamp(bar["date"].get<std::string>(), 
                                                        bar["minute"].get<std::string>());
                
                Price open = static_cast<Price>(bar["open"].get<double>() * 10000);
                Price high = static_cast<Price>(bar["high"].get<double>() * 10000);
                Price low = static_cast<Price>(bar["low"].get<double>() * 10000);
                Price close = static_cast<Price>(bar["close"].get<double>() * 10000);
                Quantity volume = static_cast<Quantity>(bar["volume"].get<uint64_t>());
                
                // Create tick with bid/ask spread
                ticks.emplace_back(string_to_symbol(symbol), timestamp, close - 2, close + 2, 
                                  100, 100, close, 0, volume);
            }
            
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("Failed to fetch IEX historical data: " + std::string(e.what()));
            }
        }
        
        return ticks;
    }
    
    std::vector<TradeTick> get_historical_trades(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date) override {
        
        std::vector<TradeTick> trades;
        
        try {
            // IEX Cloud trades endpoint
            std::string url = "https://cloud.iexapis.com/stable/stock/" + symbol + 
                             "/trades?token=" + api_token_;
            
            std::string response = http_client_->get(url);
            auto json_data = nlohmann::json::parse(response);
            
            for (const auto& trade : json_data) {
                uint64_t timestamp = trade["timestamp"].get<uint64_t>() * 1000000;  // Convert to ns
                Price price = static_cast<Price>(trade["price"].get<double>() * 10000);
                Quantity size = static_cast<Quantity>(trade["size"].get<uint64_t>());
                
                trades.emplace_back(string_to_symbol(symbol), timestamp, price, size);
            }
            
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("Failed to fetch IEX trade data: " + std::string(e.what()));
            }
        }
        
        return trades;
    }
    
private:
    Symbol string_to_symbol(const std::string& symbol_str) const {
        std::hash<std::string> hasher;
        return static_cast<Symbol>(hasher(symbol_str) % 100000) + 1;
    }
    
    uint64_t parse_iex_timestamp(const std::string& date, const std::string& minute) const {
        // Simplified IEX timestamp parsing
        return now_ns();  // Placeholder
    }
};

// Market data manager that coordinates multiple providers
class MarketDataManager {
private:
    std::vector<std::unique_ptr<MarketDataProvider>> providers_;
    TickCallback tick_callback_;
    TradeCallback trade_callback_;
    ErrorCallback error_callback_;
    
public:
    MarketDataManager() = default;
    
    void add_provider(std::unique_ptr<MarketDataProvider> provider) {
        // Set up callbacks to forward to our callbacks
        provider->set_tick_callback([this](const MarketTick& tick) {
            if (tick_callback_) tick_callback_(tick);
        });
        
        provider->set_trade_callback([this](const TradeTick& trade) {
            if (trade_callback_) trade_callback_(trade);
        });
        
        provider->set_error_callback([this](const std::string& error) {
            if (error_callback_) error_callback_(error);
        });
        
        providers_.push_back(std::move(provider));
    }
    
    bool initialize_all() {
        bool success = true;
        for (auto& provider : providers_) {
            if (!provider->connect()) {
                success = false;
            }
        }
        return success;
    }
    
    void subscribe_quotes(const std::vector<std::string>& symbols) {
        for (auto& provider : providers_) {
            provider->subscribe_quotes(symbols);
        }
    }
    
    void subscribe_trades(const std::vector<std::string>& symbols) {
        for (auto& provider : providers_) {
            provider->subscribe_trades(symbols);
        }
    }
    
    void set_tick_callback(TickCallback callback) { tick_callback_ = callback; }
    void set_trade_callback(TradeCallback callback) { trade_callback_ = callback; }
    void set_error_callback(ErrorCallback callback) { error_callback_ = callback; }
    
    void disconnect_all() {
        for (auto& provider : providers_) {
            provider->disconnect();
        }
    }
    
    // Aggregate historical data from all providers
    std::vector<MarketTick> get_historical_quotes_aggregated(
        const std::string& symbol,
        const std::string& start_date,
        const std::string& end_date) {
        
        std::vector<MarketTick> all_ticks;
        
        for (auto& provider : providers_) {
            auto provider_ticks = provider->get_historical_quotes(symbol, start_date, end_date);
            all_ticks.insert(all_ticks.end(), provider_ticks.begin(), provider_ticks.end());
        }
        
        // Sort by timestamp
        std::sort(all_ticks.begin(), all_ticks.end(), 
                  [](const MarketTick& a, const MarketTick& b) {
                      return a.timestamp_ns < b.timestamp_ns;
                  });
        
        return all_ticks;
    }
};

} // namespace hft