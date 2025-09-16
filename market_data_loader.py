#!/usr/bin/env python3
"""
Market Data Loader for HFT Engine
Handles downloading, caching, and loading real market data using yfinance
"""

import os
import pandas as pd
import yfinance as yf
from datetime import datetime, timedelta
from typing import List, Dict, Optional, Tuple
import numpy as np

class MarketDataLoader:
    """
    Loads market data from local cache or downloads from yfinance
    """
    
    def __init__(self, data_dir: str = "data"):
        self.data_dir = data_dir
        os.makedirs(data_dir, exist_ok=True)
        
    def _get_cache_filename(self, symbol: str, period: str, interval: str) -> str:
        """Generate cache filename for the data"""
        return f"{symbol}_{period}_{interval}.csv"
    
    def _load_from_cache(self, symbol: str, period: str, interval: str) -> Optional[pd.DataFrame]:
        """Load data from local cache if it exists and is recent"""
        filename = self._get_cache_filename(symbol, period, interval)
        filepath = os.path.join(self.data_dir, filename)
        
        if not os.path.exists(filepath):
            return None
            
        # Check if file is recent (less than 1 day old for intraday data)
        file_age = datetime.now() - datetime.fromtimestamp(os.path.getmtime(filepath))
        if interval in ['1m', '2m', '5m', '15m', '30m', '60m', '90m'] and file_age > timedelta(hours=6):
            return None
        elif file_age > timedelta(days=1):
            return None
            
        try:
            df = pd.read_csv(filepath, index_col=0, parse_dates=True)
            print(f"✅ Loaded {symbol} from cache: {len(df)} records")
            return df
        except Exception as e:
            print(f"❌ Error loading cache for {symbol}: {e}")
            return None
    
    def _save_to_cache(self, df: pd.DataFrame, symbol: str, period: str, interval: str):
        """Save data to local cache"""
        filename = self._get_cache_filename(symbol, period, interval)
        filepath = os.path.join(self.data_dir, filename)
        
        try:
            df.to_csv(filepath)
            print(f"💾 Saved {symbol} to cache: {len(df)} records")
        except Exception as e:
            print(f"❌ Error saving cache for {symbol}: {e}")
    
    def download_data(self, symbol: str, period: str = "1d", interval: str = "1m") -> pd.DataFrame:
        """
        Download market data for a symbol
        
        Args:
            symbol: Stock symbol (e.g., 'AAPL', 'MSFT')
            period: Data period ('1d', '5d', '1mo', '3mo', '6mo', '1y', '2y', '5y', '10y', 'ytd', 'max')
            interval: Data interval ('1m', '2m', '5m', '15m', '30m', '60m', '90m', '1h', '1d', '5d', '1wk', '1mo', '3mo')
        
        Returns:
            DataFrame with OHLCV data
        """
        # Try cache first
        cached_data = self._load_from_cache(symbol, period, interval)
        if cached_data is not None:
            return cached_data
        
        print(f"📡 Downloading {symbol} data (period: {period}, interval: {interval})...")
        
        try:
            ticker = yf.Ticker(symbol)
            data = ticker.history(period=period, interval=interval)
            
            if data.empty:
                raise ValueError(f"No data found for symbol {symbol}")
            
            # Clean the data
            data = data.dropna()
            
            # Save to cache
            self._save_to_cache(data, symbol, period, interval)
            
            print(f"✅ Downloaded {symbol}: {len(data)} records from {data.index[0]} to {data.index[-1]}")
            return data
            
        except Exception as e:
            print(f"❌ Error downloading {symbol}: {e}")
            raise
    
    def load_multiple_symbols(self, symbols: List[str], period: str = "1d", interval: str = "1m") -> Dict[str, pd.DataFrame]:
        """
        Load data for multiple symbols
        
        Args:
            symbols: List of stock symbols
            period: Data period
            interval: Data interval
            
        Returns:
            Dictionary mapping symbols to their DataFrames
        """
        data = {}
        for symbol in symbols:
            try:
                data[symbol] = self.download_data(symbol, period, interval)
            except Exception as e:
                print(f"⚠️ Skipping {symbol} due to error: {e}")
                continue
        
        return data
    
    def get_tick_data(self, df: pd.DataFrame, symbol: str) -> List[Dict]:
        """
        Convert OHLCV DataFrame to tick-like data for the HFT engine
        
        Args:
            df: OHLCV DataFrame
            symbol: Stock symbol
            
        Returns:
            List of tick dictionaries
        """
        ticks = []
        
        for timestamp, row in df.iterrows():
            # Create multiple ticks per bar to simulate intraday activity
            open_price = float(row['Open'])
            high_price = float(row['High'])
            low_price = float(row['Low'])
            close_price = float(row['Close'])
            volume = int(row['Volume'])
            
            # Generate synthetic ticks within the OHLC range
            # Open tick
            ticks.append({
                'timestamp': timestamp,
                'symbol': symbol,
                'price': open_price,
                'volume': volume // 4,
                'tick_type': 'open'
            })
            
            # High tick (if different from open)
            if high_price != open_price:
                tick_time = timestamp + pd.Timedelta(seconds=15)
                ticks.append({
                    'timestamp': tick_time,
                    'symbol': symbol,
                    'price': high_price,
                    'volume': volume // 4,
                    'tick_type': 'high'
                })
            
            # Low tick (if different from open and high)
            if low_price not in [open_price, high_price]:
                tick_time = timestamp + pd.Timedelta(seconds=30)
                ticks.append({
                    'timestamp': tick_time,
                    'symbol': symbol,
                    'price': low_price,
                    'volume': volume // 4,
                    'tick_type': 'low'
                })
            
            # Close tick
            tick_time = timestamp + pd.Timedelta(seconds=45)
            ticks.append({
                'timestamp': tick_time,
                'symbol': symbol,
                'price': close_price,
                'volume': volume // 4,
                'tick_type': 'close'
            })
        
        return ticks
    
    def get_market_data_stats(self, df: pd.DataFrame, symbol: str) -> Dict:
        """Get statistics about the market data"""
        return {
            'symbol': symbol,
            'records': len(df),
            'start_date': df.index[0],
            'end_date': df.index[-1],
            'avg_price': df['Close'].mean(),
            'price_range': (df['Low'].min(), df['High'].max()),
            'avg_volume': df['Volume'].mean(),
            'total_volume': df['Volume'].sum(),
            'volatility': df['Close'].pct_change().std() * 100,
        }

# Popular symbols for testing
POPULAR_SYMBOLS = [
    'AAPL',  # Apple
    'MSFT',  # Microsoft  
    'GOOGL', # Alphabet
    'AMZN',  # Amazon
    'TSLA',  # Tesla
    'NVDA',  # NVIDIA
    'META',  # Meta
    'NFLX',  # Netflix
    'AMD',   # AMD
    'INTC'   # Intel
]

def main():
    """Test the data loader"""
    loader = MarketDataLoader()
    
    # Test single symbol
    print("=== Testing Single Symbol Download ===")
    symbol = 'AAPL'
    data = loader.download_data(symbol, period='1d', interval='1m')
    
    print(f"\nData shape: {data.shape}")
    print(f"Columns: {list(data.columns)}")
    print(f"\nFirst few records:")
    print(data.head())
    
    # Get stats
    stats = loader.get_market_data_stats(data, symbol)
    print(f"\nMarket Data Stats for {symbol}:")
    for key, value in stats.items():
        print(f"  {key}: {value}")
    
    # Test tick conversion
    print(f"\n=== Testing Tick Conversion ===")
    ticks = loader.get_tick_data(data.head(3), symbol)  # Just first 3 bars
    print(f"Generated {len(ticks)} ticks from 3 bars")
    for tick in ticks[:5]:
        print(f"  {tick}")

if __name__ == "__main__":
    main()