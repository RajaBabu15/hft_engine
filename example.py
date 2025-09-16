#!/usr/bin/env python3
"""
HFT Engine Example
Demonstrates basic usage of the Python bindings
"""

import hft_engine_cpp as hft
import time

def basic_order_book_example():
    """Basic example of using the order book"""
    # Create order book
    book = hft.OrderBook("AAPL")
    
    # Create and add orders
    buy_order = hft.Order(1, "AAPL", hft.Side.BUY, hft.OrderType.LIMIT, 150.00, 100)
    sell_order = hft.Order(2, "AAPL", hft.Side.SELL, hft.OrderType.LIMIT, 151.00, 100)
    
    book.add_order(buy_order)
    book.add_order(sell_order)
    
    # Get market data
    best_bid = book.get_best_bid()
    best_ask = book.get_best_ask()
    mid_price = book.get_mid_price()
    
    return {
        'best_bid': best_bid,
        'best_ask': best_ask,
        'mid_price': mid_price
    }

def performance_test(num_items=10000):
    """Test lock-free queue performance"""
    queue = hft.LockFreeQueue()
    
    start = time.perf_counter()
    for i in range(num_items):
        queue.push(i)
    
    count = 0
    while not queue.empty():
        if queue.pop() is not None:
            count += 1
    
    elapsed_ms = (time.perf_counter() - start) * 1000
    throughput = count / (elapsed_ms / 1000) if elapsed_ms > 0 else 0
    
    return {
        'items_processed': count,
        'elapsed_ms': elapsed_ms,
        'throughput_ops_per_sec': throughput
    }

def main():
    """Run examples"""
    # Order book example
    market_data = basic_order_book_example()
    print(f"Market Data - Bid: ${market_data['best_bid']:.2f}, Ask: ${market_data['best_ask']:.2f}, Mid: ${market_data['mid_price']:.2f}")
    
    # Performance test
    perf_results = performance_test()
    print(f"Performance - Processed {perf_results['items_processed']} items in {perf_results['elapsed_ms']:.2f} ms")
    print(f"Throughput: {perf_results['throughput_ops_per_sec']:.0f} ops/sec")

if __name__ == "__main__":
    main()
