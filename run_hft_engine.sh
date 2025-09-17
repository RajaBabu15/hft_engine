#!/bin/bash
echo "🚀 HFT Engine: Build and Run Script"
echo "=================================="

mkdir -p build && cd build

echo "📦 Building HFT Engine with Release optimizations..."
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(sysctl -n hw.ncpu)

if [ $? -eq 0 ]; then
    echo "✅ Build successful!"
    echo "🚀 Running Integrated HFT Engine..."
    echo ""
    ./hft_engine
else
    echo "❌ Build failed!"
    exit 1
fi