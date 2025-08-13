#pragma once

#include "hft/types.h"
#include "hft/order_node.h"
#include "hft/matching_engine_types.h"

#if defined(__AVX2__)
    #include <immintrin.h>
#elif defined(__SSE4_2__)
    #include <nmmintrin.h>
#endif

namespace hft {

// Header-only SIMD matcher used by PriceLevel::match_hot_orders
FORCE_INLINE SimdMatchResult match_orders_simd(const OrderNode* const* orders,
                                               const Quantity* qtys,
                                               uint32_t order_count,
                                               Quantity incoming_qty) noexcept {
    SimdMatchResult result{};

#if defined(__AVX2__)
    const __m256i incoming = _mm256_set1_epi32(static_cast<int32_t>(incoming_qty));
    uint32_t produced = 0;

    for (uint32_t i = 0; i < order_count && produced < 16; i += 8) {
        alignas(32) int32_t q[8] = {0};
        for (int j = 0; j < 8 && (i + j) < order_count; ++j) {
            const OrderNode* n = orders[i + j];
            q[j] = (n && n->hot.qty > 0) ? static_cast<int32_t>(qtys[i + j]) : 0;
        }

        __m256i book = _mm256_load_si256((__m256i*)q);
        __m256i valid = _mm256_cmpgt_epi32(book, _mm256_setzero_si256());
        __m256i matched = _mm256_min_epi32(book, incoming);
        matched = _mm256_and_si256(matched, valid);

        alignas(32) int32_t out[8];
        _mm256_store_si256((__m256i*)out, matched);

        for (int j = 0; j < 8 && produced < 16 && (i + j) < order_count; ++j) {
            if (out[j] > 0) {
                result.indices[produced] = i + j;
                result.qtys[produced] = static_cast<Quantity>(out[j]);
                ++produced;
            }
        }
    }
    result.count = produced;

#elif defined(__SSE4_2__)
    const __m128i incoming = _mm_set1_epi32(static_cast<int32_t>(incoming_qty));
    uint32_t produced = 0;

    for (uint32_t i = 0; i < order_count && produced < 16; i += 4) {
        alignas(16) int32_t q[4] = {0};
        for (int j = 0; j < 4 && (i + j) < order_count; ++j) {
            const OrderNode* n = orders[i + j];
            q[j] = (n && n->hot.qty > 0) ? static_cast<int32_t>(qtys[i + j]) : 0;
        }

        __m128i book = _mm_load_si128((__m128i*)q);
        __m128i valid = _mm_cmpgt_epi32(book, _mm_setzero_si128());
        __m128i matched = _mm_min_epi32(book, incoming);
        matched = _mm_and_si128(matched, valid);

        alignas(16) int32_t out[4];
        _mm_store_si128((__m128i*)out, matched);

        for (int j = 0; j < 4 && produced < 16 && (i + j) < order_count; ++j) {
            if (out[j] > 0) {
                result.indices[produced] = i + j;
                result.qtys[produced] = static_cast<Quantity>(out[j]);
                ++produced;
            }
        }
    }
    result.count = produced;

#else
    // Scalar fallback
    for (uint32_t i = 0; i < order_count && result.count < 16; ++i) {
        const OrderNode* n = orders[i];
        if (n && qtys[i] > 0) {
            const Quantity matched = std::min(qtys[i], incoming_qty);
            if (matched > 0) {
                result.indices[result.count] = i;
                result.qtys[result.count] = matched;
                ++result.count;
            }
        }
    }
#endif

    return result;
}

} // namespace hft


