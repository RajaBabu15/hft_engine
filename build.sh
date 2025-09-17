#!/bin/bash
set -e

# Advanced build configuration
BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"
CPU_CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo "4")
MAX_JOBS=$((CPU_CORES + 2))  # Use more jobs than cores for I/O overlap

echo "NUMA-Optimized HFT Build with Extreme Performance Tuning"
echo "CPU Cores: $CPU_CORES, Build Jobs: $BUILD_JOBS"
echo "Build Type: Release with NUMA/Lock-Free/Vectorized optimizations"

# Set environment variables for optimal compilation
export CC=clang
export CXX=clang++
export CFLAGS="-O3 -march=native"
export CXXFLAGS="-O3 -march=native -std=c++17"

# Clean and create build directory
rm -rf $BUILD_DIR
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Check if hiredis-cluster is available
REDIS_CLUSTER_SUPPORT="OFF"
if pkg-config --exists hiredis-cluster 2>/dev/null || \
   [ -f /usr/local/include/hiredis/cluster.h ] || \
   [ -f /opt/homebrew/include/hiredis/cluster.h ]; then
    echo "✓ hiredis-cluster found - enabling cluster support"
    REDIS_CLUSTER_SUPPORT="ON"
else
    echo "✗ hiredis-cluster not found - building with single Redis support only"
fi

# Configure with advanced options
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      -DENABLE_REDIS_CLUSTER=$REDIS_CLUSTER_SUPPORT \
      ..

# Build with maximum parallelization
echo "Building with $MAX_JOBS parallel jobs..."
make -j$MAX_JOBS

cd ..
echo "Advanced build completed!"
