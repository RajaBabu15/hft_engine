#!/usr/bin/env python3
"""
Real Market Data HFT Engine Demo
Downloads real market data using yfinance and processes it through the HFT engine
"""

import hft_engine_cpp as hft
from market_data_loader import MarketDataLoader, POPULAR_SYMBOLS
from market_processor import MarketDataProcessor
import pandas as pd
import numpy as np
import time
import json
from typing import List, Dict
import argparse

def print_banner():
    """Print demo banner"""
    print("=" * 60)
    print("🚀 HFT ENGINE - REAL MARKET DATA DEMO 🚀")
    print("=" * 60)
    print("Using real market data from Yahoo Finance")
    print("Processing through C++ HFT engine via Python bindings")
    print("=" * 60)

def print_data_summary(data: Dict[str, pd.DataFrame]):
    """Print summary of loaded data"""
    print("\n📊 MARKET DATA SUMMARY")
    print("-" * 40)
    
    total_records = 0
    for symbol, df in data.items():
        records = len(df)
        total_records += records
        start_date = df.index[0].strftime('%Y-%m-%d %H:%M')
        end_date = df.index[-1].strftime('%Y-%m-%d %H:%M')
        avg_price = df['Close'].mean()
        volatility = df['Close'].pct_change().std() * 100
        
        print(f"  {symbol}:")
        print(f"    Records: {records:,}")
        print(f"    Period: {start_date} to {end_date}")
        print(f"    Avg Price: ${avg_price:.2f}")
        print(f"    Volatility: {volatility:.2f}%")
        print()
    
    print(f"📈 Total Records: {total_records:,}")
    return total_records

def print_order_book_summary(processor: MarketDataProcessor):
    """Print order book summary"""
    print("\n📚 ORDER BOOK SUMMARY")
    print("-" * 40)
    
    for symbol in processor.order_books.keys():
        state = processor.get_order_book_state(symbol)
        if state:
            print(f"  {symbol}:")
            print(f"    Best Bid: ${state['best_bid']:.2f}")
            print(f"    Best Ask: ${state['best_ask']:.2f}")  
            print(f"    Mid Price: ${state['mid_price']:.2f}")
            print(f"    Spread: {((state['best_ask'] - state['best_bid'])/state['mid_price']*10000):.1f} bps")
            
            # Show depth
            bids = state.get('bids', [])
            asks = state.get('asks', [])
            if bids or asks:
                print(f"    Market Depth:")
                print(f"      Bids: {len(bids)} levels, Asks: {len(asks)} levels")
            print()

def print_performance_metrics(report: Dict):
    """Print comprehensive performance metrics"""
    print("\n⚡ PERFORMANCE METRICS")
    print("-" * 40)
    
    if 'orders' in report:
        orders = report['orders']
        print("  ORDER STATISTICS:")
        print(f"    Total Orders: {orders.get('total_orders', 0):,}")
        print(f"    Total Volume: {orders.get('total_volume', 0):,.0f}")
        print(f"    Total Value: ${orders.get('total_value', 0):,.2f}")
        print(f"    Avg Order Size: {orders.get('avg_order_size', 0):.0f}")
        print(f"    Avg Order Value: ${orders.get('avg_order_value', 0):,.2f}")
        print()
    
    if 'performance' in report:
        perf = report['performance']
        print("  ENGINE PERFORMANCE:")
        print(f"    Throughput: {perf.get('throughput_ops_sec', 0):,.0f} ops/sec")
        print(f"    Avg Latency: {perf.get('avg_processing_time_us', 0):.1f} μs")
        print(f"    P50 Latency: {perf.get('p50_processing_time_us', 0):.1f} μs")
        print(f"    P90 Latency: {perf.get('p90_processing_time_us', 0):.1f} μs")
        print(f"    P99 Latency: {perf.get('p99_processing_time_us', 0):.1f} μs")
        print(f"    Max Latency: {perf.get('max_processing_time_us', 0):.1f} μs")
        print()

def run_single_symbol_demo(symbol: str = 'AAPL', strategy: str = 'market_making'):
    """Run demo with single symbol"""
    print(f"\n🎯 SINGLE SYMBOL DEMO: {symbol}")
    print(f"Strategy: {strategy}")
    print("-" * 40)
    
    # Load data
    loader = MarketDataLoader()
    try:
        data = loader.download_data(symbol, period='1d', interval='1m')
    except Exception as e:
        print(f"❌ Failed to download {symbol}: {e}")
        return
    
    # Get statistics
    stats = loader.get_market_data_stats(data, symbol)
    print(f"📊 Loaded {stats['records']} records for {symbol}")
    print(f"   Price range: ${stats['price_range'][0]:.2f} - ${stats['price_range'][1]:.2f}")
    print(f"   Volatility: {stats['volatility']:.2f}%")
    
    # Convert to ticks (limit to first 500 for demo)
    ticks = loader.get_tick_data(data.head(125), symbol)  # 125 bars = ~500 ticks
    print(f"🔄 Generated {len(ticks)} ticks from market data")
    
    # Process through HFT engine
    processor = MarketDataProcessor()
    results = processor.process_tick_data(ticks, strategy=strategy, max_ticks=len(ticks))
    
    # Show results
    print(f"\n✅ Processing Results:")
    print(f"   Processed: {results['processed_ticks']} ticks")
    print(f"   Time: {results['total_time']:.3f}s")
    print(f"   Throughput: {results['throughput_ops_sec']:,.0f} ticks/sec")
    print(f"   Orders Generated: {results['total_orders']}")
    
    # Performance report
    report = processor.get_performance_report()
    print_performance_metrics(report)
    print_order_book_summary(processor)
    
    return processor, results

def run_multi_symbol_demo(symbols: List[str] = None, strategy: str = 'mixed'):
    """Run demo with multiple symbols"""
    if symbols is None:
        symbols = ['AAPL', 'MSFT', 'GOOGL'][:3]  # Limit for demo
    
    print(f"\n🎯 MULTI-SYMBOL DEMO: {', '.join(symbols)}")
    print(f"Strategy: {strategy}")
    print("-" * 40)
    
    # Load data for all symbols
    loader = MarketDataLoader()
    all_data = loader.load_multiple_symbols(symbols, period='1d', interval='5m')  # 5min data
    
    if not all_data:
        print("❌ No data loaded")
        return
    
    print_data_summary(all_data)
    
    # Convert all to ticks and mix them
    all_ticks = []
    for symbol, data in all_data.items():
        ticks = loader.get_tick_data(data.head(50), symbol)  # 50 bars each
        all_ticks.extend(ticks)
    
    # Sort by timestamp to simulate realistic mixed feed
    all_ticks.sort(key=lambda x: x['timestamp'])
    
    print(f"🔄 Generated {len(all_ticks)} mixed ticks from {len(all_data)} symbols")
    
    # Process through HFT engine
    processor = MarketDataProcessor()
    results = processor.process_tick_data(all_ticks, strategy=strategy, max_ticks=len(all_ticks))
    
    # Show results
    print(f"\n✅ Processing Results:")
    print(f"   Processed: {results['processed_ticks']} ticks")
    print(f"   Time: {results['total_time']:.3f}s")
    print(f"   Throughput: {results['throughput_ops_sec']:,.0f} ticks/sec")
    print(f"   Orders Generated: {results['total_orders']}")
    print(f"   Symbols Traded: {len(processor.order_books)}")
    
    # Performance report
    report = processor.get_performance_report()
    print_performance_metrics(report)
    print_order_book_summary(processor)
    
    return processor, results

def run_realistic_simulation():
    """Run realistic trading simulation"""
    print(f"\n🎯 REALISTIC TRADING SIMULATION")
    print("-" * 40)
    
    # Load high-frequency data
    loader = MarketDataLoader()
    symbols = ['AAPL', 'MSFT']
    
    print("📡 Loading intraday data...")
    all_data = loader.load_multiple_symbols(symbols, period='1d', interval='1m')
    
    if not all_data:
        print("❌ No data loaded")
        return
    
    # Create comprehensive tick feed
    all_ticks = []
    for symbol, data in all_data.items():
        ticks = loader.get_tick_data(data.head(100), symbol)  # More data for simulation
        all_ticks.extend(ticks)
    
    all_ticks.sort(key=lambda x: x['timestamp'])
    print(f"🔄 Generated {len(all_ticks)} ticks for simulation")
    
    # Run realistic simulation
    processor = MarketDataProcessor()
    sim_results = processor.simulate_realistic_trading(all_ticks, duration_seconds=30)
    
    # Show results
    print(f"\n✅ Simulation Results:")
    print(f"   Simulation Time: {sim_results['simulation_time']:.3f}s")
    print(f"   Ticks Processed: {sim_results['ticks_processed']:,}")
    print(f"   Snapshots Captured: {len(sim_results['snapshots'])}")
    
    # Show some snapshots
    snapshots = sim_results['snapshots']
    if snapshots:
        print(f"\n📸 Sample Snapshots:")
        for i, snap in enumerate(snapshots[:3]):  # Show first 3
            print(f"   Snapshot {i+1}:")
            print(f"     Tick: {snap['tick_number']}")
            print(f"     Price: ${snap['price']:.2f}")
            book_state = snap['book_state']
            if book_state:
                print(f"     Mid Price: ${book_state.get('mid_price', 0):.2f}")
    
    # Final metrics
    final_metrics = sim_results.get('final_metrics', {})
    if final_metrics:
        print_performance_metrics(final_metrics)
    
    return processor, sim_results

def save_results(processor: MarketDataProcessor, results: Dict, filename: str = None):
    """Save results to file"""
    if filename is None:
        timestamp = pd.Timestamp.now().strftime('%Y%m%d_%H%M%S')
        filename = f"data/hft_results_{timestamp}.json"
    
    # Prepare data for JSON serialization
    report = processor.get_performance_report()
    
    # Convert numpy types and pandas types to native Python types
    def convert_for_json(obj):
        if isinstance(obj, np.integer):
            return int(obj)
        elif isinstance(obj, np.floating):
            return float(obj)
        elif isinstance(obj, pd.Timestamp):
            return obj.isoformat()
        elif isinstance(obj, list) and len(obj) > 0:
            return [convert_for_json(item) for item in obj[:100]]  # Limit list size
        elif isinstance(obj, dict):
            return {k: convert_for_json(v) for k, v in obj.items()}
        else:
            return obj
    
    save_data = {
        'timestamp': pd.Timestamp.now().isoformat(),
        'processing_results': convert_for_json(results),
        'performance_report': convert_for_json(report),
        'summary': {
            'total_orders': report.get('orders', {}).get('total_orders', 0),
            'throughput_ops_sec': report.get('performance', {}).get('throughput_ops_sec', 0),
            'avg_latency_us': report.get('performance', {}).get('avg_processing_time_us', 0)
        }
    }
    
    try:
        with open(filename, 'w') as f:
            json.dump(save_data, f, indent=2)
        print(f"\n💾 Results saved to: {filename}")
    except Exception as e:
        print(f"❌ Error saving results: {e}")

def main():
    """Main demo function"""
    parser = argparse.ArgumentParser(description='HFT Engine Real Data Demo')
    parser.add_argument('--mode', choices=['single', 'multi', 'sim', 'all'], 
                       default='all', help='Demo mode to run')
    parser.add_argument('--symbol', default='AAPL', help='Symbol for single mode')
    parser.add_argument('--strategy', choices=['market_making', 'momentum', 'mixed'],
                       default='mixed', help='Trading strategy')
    parser.add_argument('--save', action='store_true', help='Save results to file')
    
    args = parser.parse_args()
    
    print_banner()
    
    # Check HFT engine
    print("🔧 HFT Engine Info:")
    print(f"   Version: {hft.__version__}")
    print(f"   Build: {dict(hft.build_info)}")
    
    processor = None
    results = None
    
    try:
        if args.mode in ['single', 'all']:
            processor, results = run_single_symbol_demo(args.symbol, args.strategy)
        
        if args.mode in ['multi', 'all']:
            processor, results = run_multi_symbol_demo(strategy=args.strategy)
        
        if args.mode in ['sim', 'all']:
            processor, results = run_realistic_simulation()
        
        # Save results if requested
        if args.save and processor and results:
            save_results(processor, results)
            
    except KeyboardInterrupt:
        print("\n⚠️ Demo interrupted by user")
    except Exception as e:
        print(f"\n❌ Demo error: {e}")
        import traceback
        traceback.print_exc()
    
    print("\n" + "=" * 60)
    print("🎉 HFT ENGINE DEMO COMPLETED")
    print("=" * 60)

if __name__ == "__main__":
    main()