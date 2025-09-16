# High-Frequency Trading Engine

A high-performance C++ trading engine optimized for microsecond latency and high throughput.

## Features

- **Ultra-low latency**: Sub-10μs order processing
- **High throughput**: 100k+ orders/second  
- **Advanced matching**: FIFO price-time priority with SIMD optimizations
- **Risk management**: Real-time position limits and P&L tracking
- **Memory optimization**: Lock-free data structures and custom allocators
- **Market data**: FIX 4.4 protocol support and tick data replay
- **Strategies**: Market making algorithms and backtesting framework
- **Caching**: Redis integration for 30x throughput improvement

## Architecture

```
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│ FIX Parser  │────│ Matching     │────│ Risk        │
│             │    │ Engine       │    │ Manager     │
└─────────────┘    └──────────────┘    └─────────────┘
       │                   │                   │
┌─────────────┐    ┌──────────────┐    ┌─────────────┐
│ Market Data │    │ Order Book   │    │ Strategy    │
│ Feed        │    │ (SIMD)       │    │ Engine      │
└─────────────┘    └──────────────┘    └─────────────┘
```

## Quick Start

### Build
```bash
g++ -O3 -std=c++17 -pthread -funroll-loops -march=native -mtune=native -ffast-math -DNDEBUG main.cpp -o hft_engine
```

### Run
```bash
./hft_engine
```

## Performance Targets

- **Latency**: P99 < 10μs
- **Throughput**: 100k+ messages/sec
- **Reliability**: 99.99% uptime
- **Memory**: Sub-GB footprint
