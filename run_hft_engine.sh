#!/bin/bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(sysctl -n hw.ncpu)
if [ $? -eq 0 ]; then
    ./hft_engine
else
    echo "❌ Build failed!"
    exit 1
fi
