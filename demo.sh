#!/bin/bash
set -e
./build.sh > /dev/null 2>&1
if [ $? -ne 0 ]; then
    exit 1
fi
if ./build/hft_engine; then
    ./clean.sh > /dev/null 2>&1
    exit 0
else
    ./clean.sh > /dev/null 2>&1
    exit 1
fi
