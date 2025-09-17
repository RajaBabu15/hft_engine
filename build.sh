#!/bin/bash
set -e
BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"
mkdir -p $BUILD_DIR
cd $BUILD_DIR
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo "4")
cd ..
