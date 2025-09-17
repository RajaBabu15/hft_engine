#include "hft/order/order.hpp"
#include "hft/order/order_book.hpp"
#include "hft/matching/matching_engine.hpp"
#include "hft/core/types.hpp"
#include <iostream>

int main() {
    hft::matching::MatchingEngine engine;
    
    hft::order::Order buy_order;
    buy_order.id = 1;
    buy_order.symbol = "AAPL";
    buy_order.side = hft::core::Side::BUY;
    buy_order.type = hft::core::OrderType::LIMIT;
    buy_order.price = 150.00;
    buy_order.quantity = 100;
    
    hft::order::Order sell_order;
    sell_order.id = 2;
    sell_order.symbol = "AAPL";
    sell_order.side = hft::core::Side::SELL;
    sell_order.type = hft::core::OrderType::LIMIT;
    sell_order.price = 151.00;
    sell_order.quantity = 100;
    
    engine.process_order(buy_order);
    engine.process_order(sell_order);
    
    hft::order::Order market_order;
    market_order.id = 3;
    market_order.symbol = "AAPL";
    market_order.side = hft::core::Side::BUY;
    market_order.type = hft::core::OrderType::MARKET;
    market_order.quantity = 50;
    
    auto trades = engine.process_order(market_order);
    
    if (!trades.empty()) {
        std::cout << "Trade: " << trades[0].quantity << "@" << trades[0].price << std::endl;
    }
    
    return 0;
}
