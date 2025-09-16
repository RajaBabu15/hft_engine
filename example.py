#!/usr/bin/env python3
"""
Simple HFT Engine Example
Demonstrates basic usage of the Python bindings
"""

import hft_engine_cpp as hft
import time

def main():
    """Basic example of using the HFT engine from Python"""
    print("=== HFT Engine Python Example ===")
    print(f"Version: {hft.__version__}")
    
    # Create order book
    book = hft.OrderBook("AAPL")
    
    # Create and add orders
    buy_order = hft.Order(1, "AAPL", hft.Side.BUY, hft.OrderType.LIMIT, 150.00, 100)
    sell_order = hft.Order(2, "AAPL", hft.Side.SELL, hft.OrderType.LIMIT, 151.00, 100)
    
    book.add_order(buy_order)
    book.add_order(sell_order)
    
    # Display market data
    print(f"Best Bid: ${book.get_best_bid():.2f}")
    print(f"Best Ask: ${book.get_best_ask():.2f}")
    print(f"Mid Price: ${book.get_mid_price():.2f}")
    
    # Test lock-free queue performance
    print("\n=== Performance Test ===")
    queue = hft.LockFreeQueue()
    
    start = time.perf_counter()
    for i in range(10000):
        queue.push(i)
    
    count = 0
    while not queue.empty():
        if queue.pop() is not None:
            count += 1
    
    elapsed_ms = (time.perf_counter() - start) * 1000
    print(f"Processed {count} items in {elapsed_ms:.2f} ms")
    print(f"Throughput: {count / (elapsed_ms / 1000):.0f} ops/sec")
    
    print("\n✅ Example completed successfully!")

if __name__ == "__main__":
    main()