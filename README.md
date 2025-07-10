# High-Frequency Trading (HFT) Engine

A professional-grade **High-Frequency Trading Engine** built in C++ that connects to Binance cryptocurrency exchange via WebSocket to receive real-time market data and maintain a live order book for algorithmic trading strategies.

## 🎯 Project Overview

This HFT engine demonstrates institutional-grade trading infrastructure with microsecond-level optimizations. It serves as a foundation for:

- **Cryptocurrency Trading Bots**
- **Market Making Algorithms** 
- **Statistical Arbitrage Strategies**
- **Real-time Market Analysis**
- **Backtesting Infrastructure**

## 🚀 What This Project Achieves

1. **Real-time Market Data Processing**: Connects to Binance's WebSocket API for live order book updates
2. **High-Performance Order Book Management**: Maintains accurate, real-time market depth data
3. **Ultra-Low Latency Architecture**: Built for algorithmic trading with microsecond optimizations
4. **Market Analysis Foundation**: Provides real-time market conditions for trading decisions

**⚠️ Important Note**: This is a **READ-ONLY** market data system. It does **NOT** execute actual trades or place orders. It only displays real-time market data for analysis and strategy development.

## 🏗️ Architecture & Components

### Core Components

| Component | File | Purpose |
|-----------|------|----------|
| **WebSocket Client** | `websocket_client.h/cpp` | Real-time connection to Binance API |
| **Matching Engine** | `matching_engine.h/cpp` | High-performance message processor |
| **Order Book** | `order_book.h/cpp` | Market depth data management |
| **Command System** | `command.h` | Internal communication structures |
| **Configuration** | `config.json` | Trading pair and display settings |

### Technical Features

- **Lock-free Programming**: Eliminates thread contention for ultra-low latency
- **Cache Optimization**: 64-byte aligned structures prevent false sharing
- **Asynchronous Processing**: Separates I/O from computation
- **Multi-threading**: Dedicated threads for data reception and processing
- **Memory Efficient**: Optimized STL containers for trading data

## 📊 Real-time Output Example

```
Connecting to Binance for symbol: "btcusdt"
Press Ctrl+C to exit.
[2025-07-10 07:54:52] [connect] Successful connection

--- ORDER BOOK ---
------------------------------------
|       BIDS       |       ASKS       |
| Price    | Qty     | Price    | Qty     |
------------------------------------
| 111224.58 |       5 | 111224.59 |       2 |
| 111224.57 |       0 | 111224.60 |       0 |
| 111223.89 |       0 | 111225.00 |       0 |
| 111223.88 |       0 | 111226.00 |       0 |
------------------------------------
```

## 🔄 Data Flow Process

1. **WebSocket Connection**: Connects to `wss://stream.binance.com:9443/ws/btcusdt@depth20@100ms`
2. **Data Reception**: Receives order book updates every 100ms
3. **JSON Parsing**: Converts market data to internal command format
4. **Lock-free Queue**: Passes data through ultra-fast message queue
5. **Order Book Update**: Updates bid/ask levels in real-time
6. **Display**: Renders formatted order book to terminal

## ⚡ Performance Optimizations

- **Lock-free Queue**: Boost's lock-free queue for 64K commands
- **Cache Line Alignment**: Prevents CPU cache false sharing
- **Single Writer Pattern**: Eliminates lock contention
- **Memory Pool**: Reduces allocation overhead
- **Branch Prediction**: Optimized conditional logic

## 📈 Trading vs. Display-Only Functionality

### Current State: **DISPLAY-ONLY** 📊

This project is currently a **market data viewer** and does **NOT** execute any actual trading operations. Here's what it does:

**✅ What it DOES:**
- Connects to Binance WebSocket API
- Receives real-time market data
- Displays live order book updates
- Maintains accurate bid/ask spreads
- Provides foundation for trading algorithms

**❌ What it does NOT do:**
- Place buy/sell orders
- Execute trades
- Manage positions or portfolio
- Handle authentication for trading
- Risk management or position sizing

### 🚀 Future Trading Extension

To convert this into an actual trading system, you would need to add:

1. **Binance Trading API Integration**
   ```cpp
   // Example: Order placement functionality
   class TradingClient {
       void place_order(Side side, Price price, Quantity qty);
       void cancel_order(OrderId id);
   };
   ```

2. **Authentication & API Keys**
   - Binance API key and secret
   - HMAC signature generation
   - Rate limiting compliance

3. **Trading Strategy Logic**
   ```cpp
   class MarketMakingStrategy {
       void on_order_book_update(const OrderBook& book);
       void place_quotes(Price bid, Price ask);
   };
   ```

4. **Risk Management**
   - Position limits
   - Stop-loss mechanisms
   - Exposure monitoring

### ⚠️ Trading Risks Warning

**IMPORTANT**: Adding actual trading functionality involves significant financial risk:
- Cryptocurrency markets are highly volatile
- Algorithmic trading can lead to rapid losses
- Always test strategies in simulation first
- Never risk more than you can afford to lose
- Consider regulatory compliance in your jurisdiction

## Prerequisites

- Docker
- A C++20 compatible compiler (GCC 11+ or Clang 14+)
- CMake (3.16+)
- Boost libraries (`system`, `thread`)
- OpenSSL
- `nlohmann/json` library

On **Ubuntu 22.04**, you can install C++ dependencies with:
```bash
sudo apt update && sudo apt install -y \
    build-essential cmake git libssl-dev \
    libboost-all-dev nlohmann-json3-dev pkg-config
```

## 🛠️ How to Build and Run

### Method 1: Ubuntu/WSL (Recommended)

1. **Install Dependencies:**
   ```bash
   sudo apt update && sudo apt install -y build-essential cmake libboost-all-dev libssl-dev nlohmann-json3-dev pkg-config git
   ```

2. **Clone Repository & Dependencies:**
   ```bash
   git clone <your-repo-url>
   cd hft_engine
   mkdir -p external && cd external
   git clone https://github.com/zaphoyd/websocketpp.git
   cd ..
   ```

3. **Build Project:**
   ```bash
   mkdir build && cd build
   cmake ..
   make -j4
   ```

4. **Configure Trading Pair:**
   ```bash
   cp ../config.json .
   # Edit config.json to change symbol (default: "btcusdt")
   ```

5. **Run the Engine:**
   ```bash
   ./hft_engine
   ```

### Method 2: Windows with WSL

1. **Enable WSL and install Ubuntu:**
   ```powershell
   wsl --install -d Ubuntu-24.04
   ```

2. **Open WSL terminal and follow Method 1 steps above**

3. **Navigate to Windows project folder:**
   ```bash
   cd /mnt/c/Users/[username]/CLionProjects/hft_engine
   ```

### Method 3: Native Windows (Advanced)

1. **Install dependencies via vcpkg or manually**
2. **Use Visual Studio with CMake support**
3. **Configure paths for Boost, OpenSSL, nlohmann_json**

### Expected Output

Successful run shows:
```
Connecting to Binance for symbol: "btcusdt"
Press Ctrl+C to exit.
[2025-07-10 07:54:52] [connect] Successful connection
[2025-07-10 07:54:52] [connect] WebSocket Connection 57.182.125.171:9443

--- ORDER BOOK ---
------------------------------------
|       BIDS       |       ASKS       |
| Price    | Qty     | Price    | Qty     |
------------------------------------
| 111224.58 |       5 | 111224.59 |       2 |
...
```

## How to Build and Run (with Docker)

1.  **Build the Docker Image:**
    ```bash
    docker build -t hft-engine .
    ```

2.  **Run the Container:**
    ```bash
    docker run -it --rm hft-engine
    ```