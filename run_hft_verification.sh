#!/bin/bash
set -e
./build.sh
if [ $? -ne 0 ]; then
    echo "❌ Build failed!"
    exit 1
fi
if ./build/hft_engine; then
    ./clean.sh
    exit 0
else
    echo "❌ PERFORMANCE TESTS FAILED!"
    ./clean.sh
    exit 1
fi