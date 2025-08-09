#include <gtest/gtest.h>
#include "hft/trading_client.h"
#include "hft/auth_manager.h"
#include <memory>
#include <chrono>
#include <thread>

class TradingClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        auth_manager = std::make_shared<hft::AuthManager>();
        auth_manager->set_credentials(
            "testApiKey12345678901234567890123456789012345678901234567890",
            "testSecretKey123456789012345678901234567890123456789012345"
        );
        
        trading_client = std::make_unique<hft::TradingClient>(auth_manager);
        
        // Set up callbacks for testing
        order_callback_called = false;
        trade_callback_called = false;
        error_callback_called = false;
        last_error_message = "";
        
        trading_client->set_order_callback([this](const hft::Order& order) {
            order_callback_called = true;
            last_order = order;
        });
        
        trading_client->set_trade_callback([this](const hft::Trade& trade) {
            trade_callback_called = true;
            last_trade = trade;
        });
        
        trading_client->set_error_callback([this](const std::string& error) {
            error_callback_called = true;
            last_error_message = error;
        });
    }

    void TearDown() override {
        if (trading_client && trading_client->is_connected()) {
            trading_client->disconnect();
        }
    }

    std::shared_ptr<hft::AuthManager> auth_manager;
    std::unique_ptr<hft::TradingClient> trading_client;
    
    // Callback tracking
    bool order_callback_called;
    bool trade_callback_called;
    bool error_callback_called;
    std::string last_error_message;
    hft::Order last_order;
    hft::Trade last_trade;
};

TEST_F(TradingClientTest, InitialState) {
    EXPECT_FALSE(trading_client->is_connected());
}

TEST_F(TradingClientTest, ConnectWithValidCredentials) {
    EXPECT_TRUE(trading_client->connect());
    EXPECT_TRUE(trading_client->is_connected());
}

TEST_F(TradingClientTest, ConnectWithoutCredentials) {
    auto no_auth_manager = std::make_shared<hft::AuthManager>();
    auto no_auth_client = std::make_unique<hft::TradingClient>(no_auth_manager);
    
    EXPECT_FALSE(no_auth_client->connect());
    EXPECT_FALSE(no_auth_client->is_connected());
}

TEST_F(TradingClientTest, DisconnectAndReconnect) {
    EXPECT_TRUE(trading_client->connect());
    EXPECT_TRUE(trading_client->is_connected());
    
    trading_client->disconnect();
    EXPECT_FALSE(trading_client->is_connected());
    
    EXPECT_TRUE(trading_client->connect());
    EXPECT_TRUE(trading_client->is_connected());
}

TEST_F(TradingClientTest, PlaceValidOrder) {
    EXPECT_TRUE(trading_client->connect());
    
    auto future = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 0.001);
    
    // Wait for the async operation to complete
    auto order_id = future.get();
    
    EXPECT_FALSE(order_id.empty());
    EXPECT_TRUE(order_callback_called);
    EXPECT_EQ(last_order.symbol, "BTCUSDT");
    EXPECT_EQ(last_order.side, hft::Side::BUY);
    EXPECT_EQ(last_order.type, hft::OrderType::LIMIT);
    EXPECT_EQ(last_order.price, 45000.0);
    EXPECT_EQ(last_order.quantity, 0.001);
}

TEST_F(TradingClientTest, PlaceOrderWithoutConnection) {
    auto future = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 0.001);
    
    auto order_id = future.get();
    
    EXPECT_TRUE(order_id.empty());
    EXPECT_TRUE(error_callback_called);
    EXPECT_FALSE(order_callback_called);
}

TEST_F(TradingClientTest, PlaceInvalidOrder) {
    EXPECT_TRUE(trading_client->connect());
    
    // Invalid price (negative)
    auto future = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, -45000.0, 0.001);
    
    auto order_id = future.get();
    
    EXPECT_TRUE(order_id.empty());
    EXPECT_TRUE(error_callback_called);
    EXPECT_FALSE(order_callback_called);
}

TEST_F(TradingClientTest, CancelValidOrder) {
    EXPECT_TRUE(trading_client->connect());
    
    // Place an order first
    auto place_future = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 0.001);
    auto order_id = place_future.get();
    EXPECT_FALSE(order_id.empty());
    
    // Reset callback flag
    order_callback_called = false;
    
    // Cancel the order
    auto cancel_future = trading_client->cancel_order(order_id);
    bool success = cancel_future.get();
    
    EXPECT_TRUE(success);
    EXPECT_TRUE(order_callback_called);
    EXPECT_EQ(last_order.status, hft::OrderStatus::CANCELED);
}

TEST_F(TradingClientTest, CancelNonexistentOrder) {
    EXPECT_TRUE(trading_client->connect());
    
    auto future = trading_client->cancel_order("NONEXISTENT_ORDER");
    bool success = future.get();
    
    EXPECT_FALSE(success);
    EXPECT_TRUE(error_callback_called);
}

TEST_F(TradingClientTest, CancelOrderWithoutConnection) {
    auto future = trading_client->cancel_order("ORDER_123456");
    bool success = future.get();
    
    EXPECT_FALSE(success);
    EXPECT_TRUE(error_callback_called);
}

TEST_F(TradingClientTest, GetOpenOrders) {
    EXPECT_TRUE(trading_client->connect());
    
    // Place some orders
    auto future1 = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 0.001);
    auto future2 = trading_client->place_order("BTCUSDT", hft::Side::SELL, hft::OrderType::LIMIT, 46000.0, 0.001);
    
    future1.get();
    future2.get();
    
    // Get open orders
    auto orders_future = trading_client->get_open_orders("BTCUSDT");
    auto orders = orders_future.get();
    
    EXPECT_EQ(orders.size(), 2);
}

TEST_F(TradingClientTest, GetOpenOrdersEmptySymbol) {
    EXPECT_TRUE(trading_client->connect());
    
    auto orders_future = trading_client->get_open_orders("");
    auto orders = orders_future.get();
    
    EXPECT_TRUE(orders.empty());
}

TEST_F(TradingClientTest, GetAccountBalance) {
    EXPECT_TRUE(trading_client->connect());
    
    auto balance_future = trading_client->get_account_balance("USDT");
    double balance = balance_future.get();
    
    EXPECT_GT(balance, 0.0);
}

TEST_F(TradingClientTest, GetCurrentPrice) {
    EXPECT_TRUE(trading_client->connect());
    
    auto price_future = trading_client->get_current_price("BTCUSDT");
    double price = price_future.get();
    
    EXPECT_GT(price, 0.0);
}

TEST_F(TradingClientTest, GetBidAskSpread) {
    EXPECT_TRUE(trading_client->connect());
    
    auto spread_future = trading_client->get_bid_ask_spread("BTCUSDT");
    auto spread = spread_future.get();
    
    EXPECT_GT(spread.first, 0.0);  // bid
    EXPECT_GT(spread.second, 0.0); // ask
    EXPECT_LT(spread.first, spread.second); // bid < ask
}

TEST_F(TradingClientTest, RiskManagementMaxOrderValue) {
    EXPECT_TRUE(trading_client->connect());
    
    trading_client->set_max_order_value(1000.0);
    
    // Try to place order with value > max_order_value
    auto future = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 1.0); // 45000 > 1000
    
    auto order_id = future.get();
    
    EXPECT_TRUE(order_id.empty());
    EXPECT_TRUE(error_callback_called);
}

TEST_F(TradingClientTest, RiskManagementMaxPositionSize) {
    EXPECT_TRUE(trading_client->connect());
    
    trading_client->set_max_position_size("BTCUSDT", 0.01);
    
    // Try to place order with quantity > max_position_size
    auto future = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 0.1);
    
    auto order_id = future.get();
    
    EXPECT_TRUE(order_id.empty());
    EXPECT_TRUE(error_callback_called);
}

TEST_F(TradingClientTest, OrderValidation) {
    EXPECT_TRUE(trading_client->connect());
    
    // Valid order
    EXPECT_TRUE(trading_client->validate_order("BTCUSDT", hft::Side::BUY, 45000.0, 0.001));
    
    // Invalid price
    EXPECT_FALSE(trading_client->validate_order("BTCUSDT", hft::Side::BUY, -45000.0, 0.001));
    EXPECT_FALSE(trading_client->validate_order("BTCUSDT", hft::Side::BUY, 0.0, 0.001));
    
    // Invalid quantity
    EXPECT_FALSE(trading_client->validate_order("BTCUSDT", hft::Side::BUY, 45000.0, 0.0));
    EXPECT_FALSE(trading_client->validate_order("BTCUSDT", hft::Side::BUY, 45000.0, -0.001));
}

TEST_F(TradingClientTest, CancelAllOrders) {
    EXPECT_TRUE(trading_client->connect());
    
    // Place multiple orders
    auto future1 = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 0.001);
    auto future2 = trading_client->place_order("BTCUSDT", hft::Side::SELL, hft::OrderType::LIMIT, 46000.0, 0.001);
    auto future3 = trading_client->place_order("ETHUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 3000.0, 0.01);
    
    future1.get();
    future2.get();
    future3.get();
    
    // Cancel all BTCUSDT orders
    auto cancel_future = trading_client->cancel_all_orders("BTCUSDT");
    bool success = cancel_future.get();
    
    EXPECT_TRUE(success);
    
    // Check remaining orders
    auto orders_future = trading_client->get_open_orders("");
    auto orders = orders_future.get();
    
    // Should only have ETHUSDT order remaining
    EXPECT_EQ(orders.size(), 1);
    EXPECT_EQ(orders[0].symbol, "ETHUSDT");
}

TEST_F(TradingClientTest, GetOrderStatus) {
    EXPECT_TRUE(trading_client->connect());
    
    // Place an order
    auto place_future = trading_client->place_order("BTCUSDT", hft::Side::BUY, hft::OrderType::LIMIT, 45000.0, 0.001);
    auto order_id = place_future.get();
    EXPECT_FALSE(order_id.empty());
    
    // Get order status
    auto status_future = trading_client->get_order_status(order_id);
    auto order = status_future.get();
    
    EXPECT_EQ(order.id, order_id);
    EXPECT_EQ(order.symbol, "BTCUSDT");
    EXPECT_EQ(order.side, hft::Side::BUY);
}

TEST_F(TradingClientTest, GetOrderStatusNonexistent) {
    EXPECT_TRUE(trading_client->connect());
    
    auto status_future = trading_client->get_order_status("NONEXISTENT_ORDER");
    auto order = status_future.get();
    
    EXPECT_EQ(order.status, hft::OrderStatus::REJECTED);
}

TEST_F(TradingClientTest, GetTradeHistory) {
    EXPECT_TRUE(trading_client->connect());
    
    auto trades_future = trading_client->get_trade_history("BTCUSDT", 10);
    auto trades = trades_future.get();
    
    // For mock implementation, should return empty vector
    EXPECT_TRUE(trades.empty());
}
