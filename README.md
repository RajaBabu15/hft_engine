# Low-Latency Trading Simulation & Matching Engine

A C++ and Python-based high-frequency trading system engineered for ultra-low latency.

## Performance Results

- **Throughput:** 819,672 messages/sec
- **P50 Latency:** 31,033.33μs
- **P99 Latency:** 45,148.83μs
- **Trades per test:** 409

## Setup and Run

```bash
git clone https://github.com/RajaBabu15/hft_engine.git
cd hft_engine
chmod +x build.sh clean.sh run_hft_verification.sh
./run_hft_verification.sh
```