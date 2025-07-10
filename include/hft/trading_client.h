#pragma once
#include "types.h"
#include "auth_manager.h"
#include <vector>
#include <unordered_map>
#include <functional>
#include <future>
#include <memory>

namespace hft {
    class TradingClient {
    public:
        using OrderCallback = std::function<void(const Order&)>;
        using TradeCallback = std::function<void(const Trade&)>;
        using ErrorCallback = std::function<void(const std::string&)>;

        explicit TradingClient(std::shared_ptr<AuthManager> auth_manager);
        ~TradingClient();

        // Connection management
        bool connect();
        void disconnect();
        bool is_connected() const;

        // Order management
        std::future<OrderId> place_order(const Symbol& symbol, Side side, OrderType type, 
                                       Price price, Quantity quantity, TimeInForce tif = TimeInForce::GTC);
        std::future<bool> cancel_order(const OrderId& order_id);
        std::future<bool> cancel_all_orders(const Symbol& symbol = "");
        
        // Account information
        std::future<std::vector<Order>> get_open_orders(const Symbol& symbol = "");
        std::future<std::vector<Trade>> get_trade_history(const Symbol& symbol = "", int limit = 100);
        std::future<double> get_account_balance(const std::string& asset = "USDT");
        
        // Order status queries
        std::future<Order> get_order_status(const OrderId& order_id);
        
        // Callbacks for real-time updates
        void set_order_callback(OrderCallback callback);
        void set_trade_callback(TradeCallback callback);
        void set_error_callback(ErrorCallback callback);
        
        // Risk management
        void set_max_position_size(const Symbol& symbol, Quantity max_size);
        void set_max_order_value(double max_value);
        bool validate_order(const Symbol& symbol, Side side, Price price, Quantity quantity) const;
        
        // Market data requests
        std::future<double> get_current_price(const Symbol& symbol);
        std::future<std::pair<Price, Price>> get_bid_ask_spread(const Symbol& symbol);

    private:
        std::shared_ptr<AuthManager> auth_manager_;
        bool connected_;
        
        // Risk management settings
        std::unordered_map<Symbol, Quantity> max_position_sizes_;
        double max_order_value_;
        
        // Callbacks
        OrderCallback order_callback_;
        TradeCallback trade_callback_;
        ErrorCallback error_callback_;
        
        // Internal order tracking
        std::unordered_map<OrderId, Order> active_orders_;
        
        // HTTP client for REST API calls
        std::string make_authenticated_request(const std::string& endpoint, const std::string& method, 
                                              const std::string& params = "");
        
        // Helper methods
        OrderId generate_order_id();
        std::string get_current_timestamp();
        bool check_connection();
        void handle_error(const std::string& error_message);
        
        // Order validation
        bool validate_price(Price price) const;
        bool validate_quantity(Quantity quantity) const;
        bool check_risk_limits(const Symbol& symbol, Side side, Price price, Quantity quantity) const;
    };
}
