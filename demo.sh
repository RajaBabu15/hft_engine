#!/bin/bash
set -e

echo "🚀 HFT Engine Demo with Async Logging"
echo "====================================="
echo

# Build the engine
echo "📦 Building HFT Engine..."
./build.sh > /dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "❌ Build failed!"
    exit 1
fi
echo "✅ Build successful!"
echo

# Backup existing log file if it exists
if [ -f "logs/engine_logs.log" ]; then
    echo "📝 Backing up existing log file..."
    mv logs/engine_logs.log logs/engine_logs_backup_$(date +%Y%m%d_%H%M%S).log
fi

# Run the HFT engine
echo "🎯 Running HFT Engine with async logging..."
echo "   Log file: logs/engine_logs.log"
echo
if ./build/hft_engine; then
    echo
    echo "📊 Async Logger Analysis:"
    echo "========================="
    
    if [ -f "logs/engine_logs.log" ]; then
        echo "📈 Log file size: $(stat -f%z logs/engine_logs.log) bytes"
        echo "📊 Total log entries: $(wc -l < logs/engine_logs.log)"
        echo
        
        echo "📋 Log entry breakdown:"
        echo "  ORDER_RECEIVED: $(grep -c 'ORDER_RECEIVED' logs/engine_logs.log 2>/dev/null || echo 0)"
        echo "  ORDER_MATCHED:  $(grep -c 'ORDER_MATCHED' logs/engine_logs.log 2>/dev/null || echo 0)"
        echo "  ORDER_CANCELLED:$(grep -c 'ORDER_CANCELLED' logs/engine_logs.log 2>/dev/null || echo 0)"
        echo "  ENGINE events:  $(grep -c '\[ENGINE\]' logs/engine_logs.log 2>/dev/null || echo 0)"
        echo "  LATENCY logs:   $(grep -c 'LATENCY' logs/engine_logs.log 2>/dev/null || echo 0)"
        echo "  THROUGHPUT logs:$(grep -c 'THROUGHPUT' logs/engine_logs.log 2>/dev/null || echo 0)"
        echo
        
        echo "🔍 Sample log entries (last 5):"
        echo "------------------------------"
        tail -5 logs/engine_logs.log
        echo
        
        echo "📁 Full log available at: logs/engine_logs.log"
    else
        echo "⚠️  Log file not found (this may indicate an issue)"
    fi
    
    echo
    echo "✅ Demo completed successfully!"
    echo "   The HFT engine now includes high-performance async logging"
    echo "   with thread-safe, batched writes for minimal latency impact."
    
    ./clean.sh > /dev/null 2>&1
    exit 0
else
    echo "❌ HFT Engine execution failed!"
    ./clean.sh > /dev/null 2>&1
    exit 1
fi
