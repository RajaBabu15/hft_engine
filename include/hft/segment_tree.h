#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
    #include <windows.h>
    #include <malloc.h> 
#else
    #include <unistd.h> 
#endif

#include "hft/deep_profiler.h"
#if defined(HFT_ENABLE_MICRO_OPTS)
    #if defined(__GNUC__) || defined(__clang__)
    #elif defined(_MSC_VER)
        #include <xmmintrin.h> 
    #endif
#endif

namespace hft {
    #if defined(__GNUC__) || defined(__clang__)
        #define HFT_LIKELY(x)   (__builtin_expect(!!(x), 1))
        #define HFT_UNLIKELY(x) (__builtin_expect(!!(x), 0))
        #define HFT_FORCE_INLINE __attribute__((always_inline)) inline
        #define HFT_HOT __attribute__((hot))
        #define HFT_RESTRICT __restrict__
    #elif defined(_MSC_VER)
        #define HFT_LIKELY(x)   (x)
        #define HFT_UNLIKELY(x) (x)
        #define HFT_FORCE_INLINE __forceinline
        #define HFT_HOT
        #define HFT_RESTRICT __restrict
    #else
        #define HFT_LIKELY(x)   (x)
        #define HFT_UNLIKELY(x) (x)
        #define HFT_FORCE_INLINE inline
        #define HFT_HOT
        #define HFT_RESTRICT
    #endif

    #if defined(HFT_ENABLE_MICRO_OPTS)
        #if defined(__GNUC__) || defined(__clang__)
            #define HFT_PREFETCH(addr) __builtin_prefetch((addr), 0, 3)
            #define HFT_PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
        #elif defined(_MSC_VER)
            #define HFT_PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
            #define HFT_PREFETCH_WRITE(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
        #else
            #define HFT_PREFETCH(addr) ((void)0)
            #define HFT_PREFETCH_WRITE(addr) ((void)0)
        #endif
    #else
        #define HFT_PREFETCH(addr) ((void)0)
        #define HFT_PREFETCH_WRITE(addr) ((void)0)
    #endif
    
    template <typename T, std::size_t Align = 64>
    struct AlignedAllocator {
        using value_type = T;

        AlignedAllocator() noexcept {}
        template <class U> AlignedAllocator(const AlignedAllocator<U, Align>&) noexcept {}

        T* allocate(std::size_t n) {
            if (n == 0) return nullptr;
            std::size_t bytes = n * sizeof(T);
            #if defined(_WIN32)
                void* p = _aligned_malloc(bytes, Align);
                if (!p) throw std::bad_alloc();
                return static_cast<T*>(p);
            #else
                void* p = nullptr;
                int rc = posix_memalign(&p, Align, bytes);
                if (rc != 0) throw std::bad_alloc();
                return static_cast<T*>(p);
            #endif
        }

        void deallocate(T* p, std::size_t) noexcept {
            if (!p) return;
            #if defined(_WIN32)
                _aligned_free(p);
            #else
                free(p);
            #endif
        }

        template <class U>
        struct rebind { using other = AlignedAllocator<U, Align>; };
    };

    template <typename T, std::size_t A, typename U, std::size_t B>
    inline bool operator==(const AlignedAllocator<T,A>&, const AlignedAllocator<U,B>&) noexcept { return A == B; }
    template <typename T, std::size_t A, typename U, std::size_t B>
    inline bool operator!=(const AlignedAllocator<T,A>& a, const AlignedAllocator<U,B>& b) noexcept { return !(a == b); }

    inline size_t page_size_hint() noexcept {
        #if defined(__APPLE__)
            return 16384u; 
        #elif defined(_WIN32)
            static size_t cached_page_size = 0;
            if (HFT_UNLIKELY(cached_page_size == 0)) {
                SYSTEM_INFO si;
                GetSystemInfo(&si);
                cached_page_size = si.dwPageSize ? si.dwPageSize : 4096u;
            }
            return cached_page_size;
        #else
            static size_t cached_page_size = 0;
            if (HFT_UNLIKELY(cached_page_size == 0)) {
                long p = sysconf(_SC_PAGESIZE);
                cached_page_size = p > 0 ? static_cast<size_t>(p) : 4096u;
            }
            return cached_page_size;
        #endif
    }

    inline void prefault_memory(void* ptr, size_t bytes) noexcept {
        if (HFT_UNLIKELY(!ptr || bytes == 0)) return;
        volatile unsigned char *p = reinterpret_cast<volatile unsigned char*>(ptr);
        const size_t page = page_size_hint();

        size_t off = 0;
        if (bytes >= page) {
            p[0] = p[0];
            off = page;
            if (bytes >= 2 * page) {
                p[page] = p[page];
                off = 2 * page;
            }
        }
        for (; off < bytes; off += page) {
            p[off] = p[off];
        }
        p[bytes - 1] = p[bytes - 1];
    }

    

    class alignas(64) SegmentTree { 
        size_t n_ = 0;
        std::vector<uint32_t, AlignedAllocator<uint32_t, 64>> t_;

        public:
            SegmentTree() = default;
            explicit SegmentTree(size_t leaves) { init(leaves); }

            void init(size_t leaves) {
                n_ = 1;
                while (n_ < leaves) n_ <<= 1;
                t_.reserve(2 * n_ + 64);
                t_.assign(2 * n_, 0u);
                prefault_memory(t_.data(), t_.size() * sizeof(uint32_t));
            }

            inline size_t size() const noexcept { return n_; }

            HFT_HOT HFT_FORCE_INLINE void set(size_t idx, uint32_t val) noexcept {
                DEEP_PROFILE_FUNCTION();
                assert(idx < n_);
                #if defined(HFT_ENABLE_MICRO_OPTS)
                    uint32_t* HFT_RESTRICT tree = t_.data();
                #else
                    uint32_t* tree = t_.data();
                #endif

                size_t pos = idx + n_;
                tree[pos] = val;

                #if defined(HFT_ENABLE_MICRO_OPTS)
                    if (HFT_LIKELY(pos > 1)) {
                        size_t p1 = pos >> 1;
                        if (p1 < t_.size()) HFT_PREFETCH_WRITE(&tree[p1]);
                        size_t p2 = p1 >> 1;
                        if (p2 < t_.size()) HFT_PREFETCH_WRITE(&tree[p2]);
                    }
                #endif

                while (pos > 1) {
                    pos >>= 1;
                    const size_t left_child = pos << 1;
                    tree[pos] = tree[left_child] + tree[left_child | 1u];
                }
            }

            HFT_HOT HFT_FORCE_INLINE bool any(size_t l, size_t r) const noexcept {
                if (HFT_UNLIKELY(l > r)) return false;
                assert(r < n_);
                #if defined(HFT_ENABLE_MICRO_OPTS)
                    const uint32_t* HFT_RESTRICT tree = t_.data();
                #else
                    const uint32_t* tree = t_.data();
                #endif

                size_t L = l + n_, R = r + n_;
                #if defined(HFT_ENABLE_MICRO_OPTS)
                const size_t tsz = t_.size();
                #endif
                while (L <= R) {
                    if (L & 1) {
                        #if defined(HFT_ENABLE_MICRO_OPTS)
                            if (HFT_LIKELY(L + 2 < tsz)) HFT_PREFETCH(&tree[L + 2]);
                        #endif
                        if (HFT_UNLIKELY(tree[L])) return true;
                        ++L;
                    }
                    if (!(R & 1)) {
                        #if defined(HFT_ENABLE_MICRO_OPTS)
                            if (HFT_LIKELY(R >= 2)) HFT_PREFETCH(&tree[R - 2]);
                        #endif
                        if (HFT_UNLIKELY(tree[R])) return true;
                        --R;
                    }
                    L >>= 1; R >>= 1;
                }
                return false;
            }

            HFT_HOT HFT_FORCE_INLINE int find_first(size_t l, size_t r) const noexcept {
                DEEP_PROFILE_FUNCTION();
                if (HFT_UNLIKELY(l > r) || HFT_UNLIKELY(r >= n_)) return -1;

                #if defined(HFT_ENABLE_MICRO_OPTS)
                    const uint32_t* HFT_RESTRICT tree = t_.data();
                #else
                    const uint32_t* tree = t_.data();
                #endif

                if (HFT_UNLIKELY(tree[1] == 0)) return -1;
                {
                    DEEP_PROFILE_SCOPE("SegmentTree.find_first.any");
                    if (!any(l, r)) return -1;
                }

                size_t node = 1;
                size_t nl = 0;
                size_t nr = n_ - 1;

                while (nl != nr) {
                    const size_t mid = (nl + nr) >> 1;
                    const size_t leftNode = node << 1;

                    #if defined(HFT_ENABLE_MICRO_OPTS)
                        const size_t tsz = t_.size();
                        if (HFT_LIKELY(leftNode < tsz)) HFT_PREFETCH(&tree[leftNode]);
                        if (HFT_LIKELY((leftNode | 1u) < tsz)) HFT_PREFETCH(&tree[leftNode | 1u]);
                    #else
                        (void)leftNode; // Suppress unused warning
                    #endif

                    const bool left_intersects = !(r < nl || l > mid);      
                    const bool left_has = (leftNode < t_.size()) && (tree[leftNode] != 0);

                    if (left_intersects && left_has) {
                        node = leftNode;
                        nr = mid;
                    } else {
                        node = leftNode | 1;
                        nl = mid + 1;
                    }
                }

                return (nl >= l && nl <= r && tree[node]) ? static_cast<int>(nl) : -1;
            }

            HFT_FORCE_INLINE uint32_t get_unchecked(size_t idx) const noexcept {
                return t_[idx + n_];
            }

            uint64_t range_sum(size_t l, size_t r) const noexcept {
                if (HFT_UNLIKELY(l > r)) return 0;
                assert(r < n_);
                #if defined(HFT_ENABLE_MICRO_OPTS)
                    const uint32_t* HFT_RESTRICT tree = t_.data();
                #else
                    const uint32_t* tree = t_.data();
                #endif
                uint64_t res = 0;
                size_t L = l + n_, R = r + n_;
                while (L <= R) {
                    if (L & 1) { res += tree[L++]; }
                    if (!(R & 1)) { res += tree[R--]; }
                    L >>= 1; R >>= 1;
                }
                return res;
            }
    };
} ;