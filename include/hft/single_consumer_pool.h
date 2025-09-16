#pragma once

#include "hft/order_node.h"

#include <vector>
#include <cstdint>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <algorithm> 

#if defined(NUMA_AWARE)
    #include <numa.h>       
    #include <sched.h>      
    #if defined(__linux__)
        #include <sys/mman.h> 
    #endif
#endif


#if defined(_WIN32)
    #define NOMINMAX
    #include <windows.h>
#else
    #include <stdlib.h>  
    #include <unistd.h>  
    #include <sys/mman.h> 
#endif

namespace hft {

    namespace impl { 

        inline size_t get_page_size() noexcept {
            #if defined(__APPLE__)
                return 16384;
            #elif defined(_WIN32)
                SYSTEM_INFO si;
                GetSystemInfo(&si);
                return static_cast<size_t>(si.dwPageSize);
            #else
                long p = sysconf(_SC_PAGESIZE);
                return p > 0 ? static_cast<size_t>(p) : 4096;
            #endif
        }

        inline void* platform_alloc(size_t bytes, size_t align = 64, bool /* try_large_page */ = false) {
            if (bytes == 0) return nullptr;

            #if defined(_WIN32)
                if (try_large_page) {
                    SIZE_T largeMin = GetLargePageMinimum();
                    if (largeMin != 0 && (bytes % largeMin) == 0) {
                        void* p = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES, PAGE_READWRITE);
                        if (p) return p;
                    }
                }
                void* p = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                if (!p) throw std::bad_alloc();
                return p;

            #else
                #if defined(__APPLE__) && defined(__aarch64__)
                    align = std::max<size_t>(align, 128);
                #endif
                
                size_t req_align = std::max<size_t>(align, sizeof(void*));
                void* p;
                int rc = posix_memalign(&p, req_align, bytes);
                if (rc != 0 || !p) throw std::bad_alloc();
                return p;
            #endif
        }

        inline void platform_free(void* p, size_t /*bytes*/ = 0) noexcept {
            if (!p) return;
            #if defined(_WIN32)
                VirtualFree(p, 0, MEM_RELEASE);
            #else
                free(p);
            #endif
        }
        
        inline void prefault_memory(void* ptr, size_t bytes) noexcept {
            if (!ptr || bytes == 0) return;
            volatile unsigned char* p = reinterpret_cast<volatile unsigned char*>(ptr);
            
            #if defined(__APPLE__)
                constexpr size_t page = 16384;
            #else
                size_t page = get_page_size();
            #endif
                
            for (size_t offset = 0; offset < bytes; offset += page) {
                unsigned char tmp = p[offset];
                p[offset] = tmp;
            }
            if (bytes > page) p[bytes - 1] = p[bytes - 1];
        }

    }; 

    class SingleConsumerPool {
        private:
            OrderNode *nodes_{nullptr};
            size_t capacity_{0};
            std::vector<uint32_t> free_indices_;
            int32_t free_top_{0};

            [[maybe_unused]] bool used_numa_{false};
            bool used_platform_alloc_{false};

        public:
            explicit SingleConsumerPool(size_t capacity) : capacity_(capacity), free_indices_(capacity) {
                #if defined(NUMA_AWARE)
                    if (numa_available() >= 0) {
                        int cpu = sched_getcpu();
                        int numa_node = numa_node_of_cpu(cpu);
                        void *aligned_mem = numa_alloc_onnode(sizeof(OrderNode) * capacity, numa_node);
                        if (aligned_mem) {
                            nodes_ = reinterpret_cast<OrderNode *>(aligned_mem);
                            used_numa_ = true;
                            used_platform_alloc_ = false;
                            #if defined(MADV_HUGEPAGE)
                            madvise(nodes_, capacity * sizeof(OrderNode), MADV_HUGEPAGE);
                            #endif
                        }
                    }
                #endif
                
                if (!nodes_) {
                    size_t bytes = capacity * sizeof(OrderNode);
                    try {
                        void* p = impl::platform_alloc(bytes, /*align=*/128, /*try_large_page=*/false);
                        nodes_ = reinterpret_cast<OrderNode*>(p);
                        used_platform_alloc_ = true;
                        impl::prefault_memory(nodes_, bytes);
                    } catch (...) {
                        nodes_ = new OrderNode[capacity];
                        used_platform_alloc_ = false;
                    }
                }

                // Optimized initialization - single loop
                free_top_ = static_cast<int32_t>(capacity);
                for (uint32_t i = 0; i < capacity; ++i) {
                    nodes_[i].index = i;
                    nodes_[i].generation = 0;
                    free_indices_[i] = i;
                }
            }

        ~SingleConsumerPool() {
            #if defined(NUMA_AWARE)
                    if (used_numa_) {
                        if (nodes_) numa_free(nodes_, capacity_ * sizeof(OrderNode));
                        return;
                    }
            #endif
                    if (used_platform_alloc_) {
                        impl::platform_free(nodes_, capacity_ * sizeof(OrderNode));
                        return;
                    }
                    delete[] nodes_;
            }

        [[gnu::hot]] OrderNode *acquire() noexcept {
            #if defined(__GNUC__) || defined(__clang__)
            if (__builtin_expect(!!(free_top_ <= 0), 0)) return nullptr;
            #else
            if ((free_top_ <= 0)) return nullptr;
            #endif
            --free_top_;
            uint32_t idx = free_indices_[free_top_];
            OrderNode *node = &nodes_[idx];
            ++node->generation;
            node->reset();
            return node;
        }
    
        [[gnu::hot]] void release(OrderNode *node) noexcept {
            #if defined(__GNUC__) || defined(__clang__)
            if (__builtin_expect(!!(free_top_ < static_cast<int32_t>(capacity_)), 1)) {
            #else
            if ((free_top_ < static_cast<int32_t>(capacity_))) {
            #endif
                free_indices_[free_top_++] = node->index;
            }
        }

        OrderNode *get_node(uint32_t index) noexcept {
            #if defined(__GNUC__) || defined(__clang__)
            if (__builtin_expect(!!(index >= capacity_), 0)) return nullptr;
            #else
            if ((index >= capacity_)) return nullptr;
            #endif
            return &nodes_[index];
        }

        size_t capacity() const noexcept { return capacity_; }
    };
}; // namespace hft