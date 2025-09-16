#!/usr/bin/env python3
"""
Market Data Processor for HFT Engine
Converts real market data into orders and executes trading strategies
"""

import hft_engine_cpp as hft
import pandas as pd
import numpy as np
from typing import List, Dict, Tuple, Optional
import time
import random
from dataclasses import dataclass
from datetime import datetime

@dataclass
class EnginePerformanceMetrics:
    """Track HFT engine performance metrics"""
    total_orders: int = 0
    filled_orders: int = 0
    cancelled_orders: int = 0
    total_volume: float = 0.0
    total_value: float = 0.0
    processing_times: List[float] = None
    throughput_ops_sec: float = 0.0
    
    def __post_init__(self):
        if self.processing_times is None:
            self.processing_times = []

class MarketDataProcessor:
    """
    Processes real market data through the HFT engine
    """
    
    def __init__(self):
        self.order_books = {}  # symbol -> OrderBook
        self.order_counter = 1
        self.metrics = EnginePerformanceMetrics()
        self.price_history = {}  # symbol -> [prices]
        
    def create_order_book(self, symbol: str) -> hft.OrderBook:
        """Create and return an order book for a symbol"""
        if symbol not in self.order_books:
            self.order_books[symbol] = hft.OrderBook(symbol)
            self.price_history[symbol] = []
            print(f"📚 Created order book for {symbol}")
        return self.order_books[symbol]
    
    def get_next_order_id(self) -> int:
        """Get next unique order ID"""
        order_id = self.order_counter
        self.order_counter += 1
        return order_id
    
    def calculate_spread_orders(self, current_price: float, spread_bps: int = 10) -> Tuple[float, float]:
        """
        Calculate bid/ask prices with specified spread
        
        Args:
            current_price: Current market price
            spread_bps: Spread in basis points (default 10 bps = 0.1%)
            
        Returns:
            (bid_price, ask_price)
        """
        spread_pct = spread_bps / 10000.0  # Convert bps to percentage
        half_spread = current_price * spread_pct / 2
        
        bid_price = current_price - half_spread
        ask_price = current_price + half_spread
        
        return bid_price, ask_price
    
    def create_market_making_orders(self, tick_data: Dict, book: hft.OrderBook, 
                                  quantity: int = 100, spread_bps: int = 10) -> List[hft.Order]:
        """
        Create market making orders (bid/ask pair)
        
        Args:
            tick_data: Tick data dictionary
            book: Order book instance
            quantity: Order quantity
            spread_bps: Spread in basis points
            
        Returns:
            List of created orders
        """
        current_price = tick_data['price']
        symbol = tick_data['symbol']
        
        bid_price, ask_price = self.calculate_spread_orders(current_price, spread_bps)
        
        orders = []
        
        # Create bid order
        bid_order = hft.Order(
            self.get_next_order_id(),
            symbol,
            hft.Side.BUY,
            hft.OrderType.LIMIT,
            bid_price,
            quantity
        )
        orders.append(bid_order)
        
        # Create ask order
        ask_order = hft.Order(
            self.get_next_order_id(),
            symbol,
            hft.Side.SELL,
            hft.OrderType.LIMIT,
            ask_price,
            quantity
        )
        orders.append(ask_order)
        
        return orders
    
    def create_momentum_orders(self, tick_data: Dict, book: hft.OrderBook, 
                             quantity: int = 50) -> List[hft.Order]:
        """
        Create momentum-based orders
        
        Args:
            tick_data: Tick data dictionary
            book: Order book instance
            quantity: Order quantity
            
        Returns:
            List of created orders
        """
        symbol = tick_data['symbol']
        current_price = tick_data['price']
        
        # Keep price history
        if symbol not in self.price_history:
            self.price_history[symbol] = []
        
        self.price_history[symbol].append(current_price)
        
        # Only keep last 20 prices for momentum calculation
        if len(self.price_history[symbol]) > 20:
            self.price_history[symbol] = self.price_history[symbol][-20:]
        
        # Need at least 5 prices to calculate momentum
        if len(self.price_history[symbol]) < 5:
            return []
        
        prices = np.array(self.price_history[symbol])
        momentum = (prices[-1] - prices[-5]) / prices[-5]  # 5-tick momentum
        
        orders = []
        
        # Strong momentum threshold (0.1%)
        if abs(momentum) > 0.001:
            side = hft.Side.BUY if momentum > 0 else hft.Side.SELL
            
            # Aggressive pricing for momentum
            price_adjustment = current_price * 0.0005  # 5 bps
            order_price = current_price + price_adjustment if side == hft.Side.BUY else current_price - price_adjustment
            
            order = hft.Order(
                self.get_next_order_id(),
                symbol,
                side,
                hft.OrderType.LIMIT,
                order_price,
                quantity
            )
            orders.append(order)
        
        return orders
    
    def process_tick_data(self, tick_data: List[Dict], strategy: str = "market_making", 
                         max_ticks: Optional[int] = None) -> Dict:
        """
        Process tick data through the HFT engine
        
        Args:
            tick_data: List of tick data dictionaries
            strategy: Trading strategy ('market_making', 'momentum', 'mixed')
            max_ticks: Maximum number of ticks to process (for testing)
            
        Returns:
            Processing results and metrics
        """
        print(f"🚀 Processing {len(tick_data)} ticks with {strategy} strategy")
        
        start_time = time.perf_counter()
        processed_ticks = 0
        
        # Process each tick
        for i, tick in enumerate(tick_data):
            if max_ticks and i >= max_ticks:
                break
                
            symbol = tick['symbol']
            book = self.create_order_book(symbol)
            
            # Create orders based on strategy
            orders = []
            
            tick_start = time.perf_counter()
            
            if strategy == "market_making":
                orders = self.create_market_making_orders(tick, book)
            elif strategy == "momentum":
                orders = self.create_momentum_orders(tick, book)
            elif strategy == "mixed":
                # Mix of both strategies
                if random.random() < 0.7:  # 70% market making
                    orders = self.create_market_making_orders(tick, book)
                else:  # 30% momentum
                    orders.extend(self.create_momentum_orders(tick, book))
            
            # Add orders to the book
            for order in orders:
                success = book.add_order(order)
                if success:
                    self.metrics.total_orders += 1
                    self.metrics.total_volume += order.quantity
                    self.metrics.total_value += order.price * order.quantity
            
            tick_processing_time = time.perf_counter() - tick_start
            self.metrics.processing_times.append(tick_processing_time)
            
            processed_ticks += 1
            
            # Print progress every 100 ticks
            if processed_ticks % 100 == 0:
                print(f"  Processed {processed_ticks} ticks...")
        
        total_time = time.perf_counter() - start_time
        self.metrics.throughput_ops_sec = processed_ticks / total_time if total_time > 0 else 0
        
        print(f"✅ Processed {processed_ticks} ticks in {total_time:.3f}s")
        
        return {
            'processed_ticks': processed_ticks,
            'total_time': total_time,
            'throughput_ops_sec': self.metrics.throughput_ops_sec,
            'total_orders': self.metrics.total_orders,
            'order_books': self.order_books
        }
    
    def get_order_book_state(self, symbol: str) -> Dict:
        """Get current state of an order book"""
        if symbol not in self.order_books:
            return {}
        
        book = self.order_books[symbol]
        
        return {
            'symbol': symbol,
            'best_bid': book.get_best_bid(),
            'best_ask': book.get_best_ask(),
            'mid_price': book.get_mid_price(),
            'bid_quantity': book.get_bid_quantity(book.get_best_bid()),
            'ask_quantity': book.get_ask_quantity(book.get_best_ask()),
            'bids': book.get_bids(5),  # Top 5 bid levels
            'asks': book.get_asks(5)   # Top 5 ask levels
        }
    
    def get_performance_report(self) -> Dict:
        """Generate comprehensive performance report"""
        if not self.metrics.processing_times:
            return {}
        
        processing_times = np.array(self.metrics.processing_times)
        
        return {
            'orders': {
                'total_orders': self.metrics.total_orders,
                'total_volume': self.metrics.total_volume,
                'total_value': self.metrics.total_value,
                'avg_order_size': self.metrics.total_volume / max(self.metrics.total_orders, 1),
                'avg_order_value': self.metrics.total_value / max(self.metrics.total_orders, 1)
            },
            'performance': {
                'throughput_ops_sec': self.metrics.throughput_ops_sec,
                'avg_processing_time_us': np.mean(processing_times) * 1_000_000,
                'p50_processing_time_us': np.percentile(processing_times, 50) * 1_000_000,
                'p90_processing_time_us': np.percentile(processing_times, 90) * 1_000_000,
                'p99_processing_time_us': np.percentile(processing_times, 99) * 1_000_000,
                'max_processing_time_us': np.max(processing_times) * 1_000_000
            },
            'order_books': {
                symbol: self.get_order_book_state(symbol) 
                for symbol in self.order_books.keys()
            }
        }
    
    def reset_metrics(self):
        """Reset performance metrics"""
        self.metrics = EnginePerformanceMetrics()
    
    def simulate_realistic_trading(self, tick_data: List[Dict], 
                                 duration_seconds: int = 60) -> Dict:
        """
        Simulate realistic trading with time-based execution
        
        Args:
            tick_data: List of tick data
            duration_seconds: Simulation duration
            
        Returns:
            Simulation results
        """
        print(f"🎯 Simulating {duration_seconds}s of realistic trading")
        
        start_time = time.perf_counter()
        results = []
        
        for i, tick in enumerate(tick_data):
            # Check if simulation time exceeded
            elapsed = time.perf_counter() - start_time
            if elapsed > duration_seconds:
                break
            
            symbol = tick['symbol']
            book = self.create_order_book(symbol)
            
            # Mix strategies dynamically
            strategy_choice = random.random()
            orders = []
            
            if strategy_choice < 0.6:  # 60% market making
                orders = self.create_market_making_orders(tick, book, quantity=random.randint(50, 200))
            elif strategy_choice < 0.9:  # 30% momentum
                orders = self.create_momentum_orders(tick, book, quantity=random.randint(25, 100))
            # 10% no action (realistic)
            
            # Process orders
            for order in orders:
                book.add_order(order)
                self.metrics.total_orders += 1
            
            # Capture periodic snapshots
            if i % 50 == 0:
                results.append({
                    'tick_number': i,
                    'timestamp': tick['timestamp'],
                    'price': tick['price'],
                    'book_state': self.get_order_book_state(symbol)
                })
        
        total_time = time.perf_counter() - start_time
        
        return {
            'simulation_time': total_time,
            'ticks_processed': i + 1,
            'snapshots': results,
            'final_metrics': self.get_performance_report()
        }

def main():
    """Test the market processor"""
    processor = MarketDataProcessor()
    
    # Create sample tick data
    sample_ticks = []
    base_price = 150.0
    for i in range(100):
        price = base_price + random.uniform(-1, 1) * (i % 10) * 0.01
        sample_ticks.append({
            'timestamp': pd.Timestamp.now() + pd.Timedelta(seconds=i),
            'symbol': 'AAPL',
            'price': price,
            'volume': random.randint(100, 1000),
            'tick_type': 'trade'
        })
    
    print("=== Testing Market Data Processor ===")
    
    # Test market making strategy
    results = processor.process_tick_data(sample_ticks, strategy="market_making", max_ticks=50)
    print(f"Market Making Results: {results}")
    
    # Get performance report
    report = processor.get_performance_report()
    print(f"\nPerformance Report:")
    for category, metrics in report.items():
        print(f"  {category}:")
        if isinstance(metrics, dict):
            for key, value in metrics.items():
                print(f"    {key}: {value}")
        else:
            print(f"    {metrics}")

if __name__ == "__main__":
    main()