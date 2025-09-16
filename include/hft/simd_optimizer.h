#pragma once

#include "hft/types.h"
#include "hft/ultra_profiler.h"
#include <immintrin.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <string>

namespace hft {

// SIMD-optimized batch processing for orders
class SIMDOptimizer {
public:
    // Pack hot order data into SIMD-friendly structure
    struct alignas(32) OrderBatch {
        std::array<Price, 8> prices;        // 8 x 8 = 64 bytes
        std::array<Quantity, 8> quantities; // 8 x 8 = 64 bytes
        std::array<Symbol, 8> symbols;      // 8 x 4 = 32 bytes
        std::array<Side, 8> sides;          // 8 x 1 = 8 bytes
        // Total: 168 bytes (aligned to 32-byte boundary for AVX)
    };
    
    // SIMD-optimized price comparison (8 prices at once)
    static uint8_t compare_prices_simd(const Price* prices1, const Price* prices2) {
        ULTRA_PROFILE_CRITICAL(simd_price_compare);
        
        #ifdef __AVX2__
            // Load 8 prices (64-bit each) into AVX registers
            __m256i p1_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prices1));
            __m256i p1_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prices1 + 4));
            __m256i p2_lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prices2));
            __m256i p2_hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prices2 + 4));
            
            // Compare prices (greater than for buy orders)
            __m256i cmp_lo = _mm256_cmpgt_epi64(p1_lo, p2_lo);
            __m256i cmp_hi = _mm256_cmpgt_epi64(p1_hi, p2_hi);
            
            // Pack results into bytes
            __m256i packed = _mm256_packs_epi32(
                _mm256_packs_epi32(cmp_lo, cmp_hi),
                _mm256_setzero_si256()
            );
            
            return static_cast<uint8_t>(_mm256_movemask_epi8(packed));
        #else
            // Fallback scalar implementation
            uint8_t result = 0;
            for (int i = 0; i < 8; i++) {
                if (prices1[i] > prices2[i]) {
                    result |= (1 << i);
                }
            }
            return result;
        #endif
    }
    
    // SIMD-optimized quantity matching
    static void match_quantities_simd(const Quantity* incoming, Quantity* resting, Quantity* matched) {
        ULTRA_PROFILE_CRITICAL(simd_quantity_match);
        
        #ifdef __AVX2__
            __m256i inc = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(incoming));
            __m256i rest = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(resting));
            
            // Calculate minimum quantity for each pair
            __m256i min_qty = _mm256_min_epu64(inc, rest);
            
            // Store matched quantities
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(matched), min_qty);
            
            // Update resting quantities (resting - matched)
            __m256i new_rest = _mm256_sub_epi64(rest, min_qty);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(resting), new_rest);
        #else
            // Fallback scalar implementation
            for (int i = 0; i < 8; i++) {
                matched[i] = std::min(incoming[i], resting[i]);
                resting[i] -= matched[i];
            }
        #endif
    }
    
    // Vectorized risk check for batch of orders
    static uint8_t batch_risk_check(const Price* prices, const Quantity* quantities, 
                                   Price max_price, Quantity max_quantity) {
        ULTRA_PROFILE_CRITICAL(simd_risk_check);
        
        #ifdef __AVX2__
            __m256i prices_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prices));
            __m256i quantities_vec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(quantities));
            
            __m256i max_price_vec = _mm256_set1_epi64x(max_price);
            __m256i max_qty_vec = _mm256_set1_epi64x(max_quantity);
            
            // Check price limits
            __m256i price_ok = _mm256_or_si256(
                _mm256_cmpeq_epi64(prices_vec, _mm256_setzero_si256()), // Market orders (price = 0)
                _mm256_cmpgt_epi64(max_price_vec, prices_vec)
            );
            
            // Check quantity limits
            __m256i qty_ok = _mm256_cmpgt_epi64(max_qty_vec, quantities_vec);
            
            // Combine checks
            __m256i all_ok = _mm256_and_si256(price_ok, qty_ok);
            
            // Pack and return bitmask
            return static_cast<uint8_t>(_mm256_movemask_epi8(all_ok));
        #else
            uint8_t result = 0;
            for (int i = 0; i < 4; i++) {
                bool price_ok = (prices[i] == 0) || (prices[i] <= max_price);
                bool qty_ok = quantities[i] <= max_quantity;
                if (price_ok && qty_ok) {
                    result |= (1 << i);
                }
            }
            return result;
        #endif
    }
    
    // Prefetch multiple cache lines for order batch
    static void prefetch_order_batch(const void* order_batch, size_t count) {
        ULTRA_PROFILE_CRITICAL(batch_prefetch);
        
        const char* ptr = static_cast<const char*>(order_batch);
        for (size_t i = 0; i < count; i++) {
            PREFETCH_L1(ptr + i * 64); // Prefetch each cache line
        }
    }
    
    // Cache-optimized memory copy for hot order data
    static void copy_hot_data_simd(const void* src, void* dst, size_t bytes) {
        ULTRA_PROFILE_CRITICAL(simd_memory_copy);
        
        #ifdef __AVX2__
            if (bytes >= 32 && (reinterpret_cast<uintptr_t>(src) % 32 == 0) && 
                (reinterpret_cast<uintptr_t>(dst) % 32 == 0)) {
                
                const __m256i* src_vec = static_cast<const __m256i*>(src);
                __m256i* dst_vec = static_cast<__m256i*>(dst);
                
                size_t vec_count = bytes / 32;
                for (size_t i = 0; i < vec_count; i++) {
                    __m256i data = _mm256_load_si256(&src_vec[i]);
                    _mm256_store_si256(&dst_vec[i], data);
                }
                
                // Handle remaining bytes with regular copy
                size_t remaining = bytes % 32;
                if (remaining > 0) {
                    const char* src_bytes = static_cast<const char*>(src) + (vec_count * 32);
                    char* dst_bytes = static_cast<char*>(dst) + (vec_count * 32);
                    for (size_t i = 0; i < remaining; i++) {
                        dst_bytes[i] = src_bytes[i];
                    }
                }
            } else {
                // Fallback to regular memcpy for unaligned data
                std::memcpy(dst, src, bytes);
            }
        #else
            std::memcpy(dst, src, bytes);
        #endif
    }
    
    // Get SIMD capabilities at runtime
    static std::string get_simd_info() {
        std::string info = "SIMD Support: ";
        
        #ifdef __AVX2__
            info += "AVX2 ";
        #endif
        #ifdef __AVX__
            info += "AVX ";
        #endif
        #ifdef __SSE4_2__
            info += "SSE4.2 ";
        #endif
        #ifdef __SSE2__
            info += "SSE2 ";
        #endif
        
        if (info == "SIMD Support: ") {
            info += "None (scalar fallback)";
        }
        
        return info;
    }
};

} // namespace hft