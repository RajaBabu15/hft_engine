#!/bin/bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release .. >/dev/null 2>&1 && make -j$(sysctl -n hw.ncpu) >/dev/null 2>&1
if [ $? -eq 0 ]; then
    ./hft_engine
fi
