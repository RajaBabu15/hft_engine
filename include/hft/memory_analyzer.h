#pragma once

#include "hft/types.h"
#include "hft/platform.h"
#include <iostream>
#include <iomanip>
#include <sys/mman.h>
#include <unistd.h>

namespace hft {

// Memory and cache optimization analyzer
class MemoryAnalyzer {
public:
    static void analyze_memory_layout() {
        std::cout << "\n=== MEMORY LAYOUT ANALYSIS ===" << std::endl;
        
        // Cache line size detection
        size_t cache_line_size = get_cache_line_size();
        std::cout << "Cache line size: " << cache_line_size << " bytes" << std::endl;
        
        // Page size
        size_t page_size = getpagesize();
        std::cout << "Memory page size: " << page_size << " bytes" << std::endl;
        
        // TLB and NUMA info
        analyze_numa_topology();
        
        // Memory prefetch analysis
        analyze_prefetch_performance();
    }
    
    static void analyze_data_structure_alignment() {
        std::cout << "\n=== DATA STRUCTURE ALIGNMENT ===" << std::endl;
        
        // Check critical structure alignments
        std::cout << "sizeof(Order): " << sizeof(Order) << " bytes" << std::endl;
        std::cout << "alignof(Order): " << alignof(Order) << " bytes" << std::endl;
        
        // Check if structures fit in cache lines
        size_t cache_line = get_cache_line_size();
        std::cout << "Orders per cache line: " << cache_line / sizeof(Order) << std::endl;
        
        // Memory access pattern recommendations
        suggest_optimizations();
    }
    
private:
    static size_t get_cache_line_size() {
        long cache_line = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
        return (cache_line > 0) ? static_cast<size_t>(cache_line) : 64; // default to 64 bytes
    }
    
    static void analyze_numa_topology() {
        #ifdef NUMA_AWARE
            std::cout << "NUMA topology analysis enabled" << std::endl;
            // Add NUMA-specific analysis here
        #else
            std::cout << "NUMA analysis not available (compiled without NUMA support)" << std::endl;
        #endif
    }
    
    static void analyze_prefetch_performance() {
        std::cout << "\nPrefetch performance test:" << std::endl;
        
        constexpr size_t TEST_SIZE = 1024 * 1024; // 1MB
        alignas(64) uint64_t* test_array = new(std::align_val_t(64)) uint64_t[TEST_SIZE];
        
        // Initialize test data
        for (size_t i = 0; i < TEST_SIZE; ++i) {
            test_array[i] = i;
        }
        
        // Test without prefetch
        auto start = now_ns();
        uint64_t sum1 = 0;
        for (size_t i = 0; i < TEST_SIZE; i += 8) {
            sum1 += test_array[i];
        }
        auto end = now_ns();
        uint64_t time_no_prefetch = end - start;
        
        // Test with prefetch
        start = now_ns();
        uint64_t sum2 = 0;
        for (size_t i = 0; i < TEST_SIZE; i += 8) {
            if (i + 64 < TEST_SIZE) {
                PREFETCH_L1(&test_array[i + 64]);
            }
            sum2 += test_array[i];
        }
        end = now_ns();
        uint64_t time_with_prefetch = end - start;
        
        std::cout << "Without prefetch: " << time_no_prefetch << " ns" << std::endl;
        std::cout << "With prefetch: " << time_with_prefetch << " ns" << std::endl;
        std::cout << "Prefetch speedup: " << std::fixed << std::setprecision(2) 
                  << static_cast<double>(time_no_prefetch) / time_with_prefetch << "x" << std::endl;
        
        delete[] test_array;
        
        // Prevent optimization from removing the computation
        volatile uint64_t prevent_opt = sum1 + sum2;
        (void)prevent_opt;
    }
    
    static void suggest_optimizations() {
        std::cout << "\n=== OPTIMIZATION SUGGESTIONS ===" << std::endl;
        std::cout << "1. Ensure hot data structures are cache-aligned" << std::endl;
        std::cout << "2. Group frequently accessed fields together" << std::endl;
        std::cout << "3. Use padding to avoid false sharing" << std::endl;
        std::cout << "4. Consider SIMD-friendly data layouts" << std::endl;
        std::cout << "5. Implement memory pooling for frequent allocations" << std::endl;
    }
};

} // namespace hft