#pragma once

// Platform-specific includes and optimizations
#if defined(__AVX512F__) || defined(__AVX512DQ__) || defined(__AVX2__) || defined(__SSE4_2__) \
 || defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#  include <immintrin.h>
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#  include <arm_neon.h>
#  define CPU_RELAX() __asm__ volatile("yield" ::: "memory")
#  define PREFETCH_L1(addr) __builtin_prefetch((const void*)(addr), 0, 3)
#  define PREFETCH_L2(addr) __builtin_prefetch((const void*)(addr), 0, 2)
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#  define CPU_RELAX() _mm_pause()
#  define PREFETCH_L1(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#  define PREFETCH_L2(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T1)
#else
#  define CPU_RELAX() do {} while(0)
#  define PREFETCH_L1(addr) do {} while(0)
#  define PREFETCH_L2(addr) do {} while(0)
#endif

// Compiler optimization hints
#define FORCE_INLINE __attribute__((always_inline)) inline
#define HOT_PATH __attribute__((hot))
#define COLD_PATH __attribute__((cold))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define ASSUME_ALIGNED(ptr, alignment) __builtin_assume_aligned((ptr), (alignment))
#define CACHE_ALIGNED alignas(64)

namespace hft {
    constexpr size_t CACHE_LINE_SIZE = 64;
    constexpr size_t SIMD_WIDTH = 8;
}


