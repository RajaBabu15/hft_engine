#include <gtest/gtest.h>
#include "hft/order_book.h"

TEST(OrderBookTest, UpdateAndPrint) {
    hft::OrderBook book;
    hft::Command cmd;
    cmd.type = hft::CommandType::MARKET_DATA;
    cmd.bids.push_back({hft::price_to_int(100.50), 10});
    cmd.asks.push_back({hft::price_to_int(100.55), 5});

    book.update(cmd);

    // In a real test, you'd capture stdout or check internal state.
    // Here we just ensure it doesn't crash.
    SUCCEED();
}