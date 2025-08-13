#pragma once

#include "hft/order.h"
#include "hft/deep_profiler.h"
#include <atomic>
#include <vector>
#include <cstdint> //header in C++ ensures the portability of integer types with specialized width and signedness across various systems.

namespace hft {

    struct RateWindow {
        std::atomic<uint32_t> window_start_sec{0}; //epoch seconds of current window
        std::atomic<uint32_t> count{0}; //count in current window
        uint32_t max_per_sec{0};

        RateWindow() noexcept = default;

        explicit RateWindow(uint32_t max_per_sec) noexcept : max_per_sec(max_per_sec) {}

        inline bool try_acquire() noexcept {
            const uint64_t now_s=now_ns()/1'000'000'000ULL;
            uint32_t now_sec=static_cast<uint32_t>(now_s&0xFFFFFFFFU);

            uint32_t cur_win=window_start_sec.load(std::memory_order_relaxed);

            if(cur_win==now_sec) {
                uint32_t prev=count.fetch_add(1,std::memory_order_relaxed);
                //If prev>=max_per_sec then we exceeded(because fetch_add returned previous)
                return prev < max_per_sec;
            }

            //trying to move to new window
            if(window_start_sec.compare_exchange_strong(cur_win,now_sec,std::memory_order_acq_rel   )) {
                //we successfully moved to new second,reset count to 1
                count.store(1,std::memory_order_relaxed);
                return 1<=max_per_sec;
            }

            //another thread updated window_start_sec,try again recursively(tail recursion limited)
            //do a single retry to keep hot-path bounded
            cur_win=window_start_sec.load(std::memory_order_relaxed);
            if(cur_win==now_sec){
                uint32_t prev=count.fetch_add(1,std::memory_order_relaxed);
                return prev<max_per_sec;
            }

            //worst-case deny(very unlikely)
            return false;
        }
    };

    class RiskManager {
        public:
            struct SymbolLimits {
                Symbol symbol;
                Quantity max_qty; //per-order quantity limit for this symbol
                int64_t max_notational_ticks; //max notational (price*qty in ticks) per order for this symbol
            };
        private:
            Quantity global_max_qty_{0};
            int64_t global_max_notational_ticks_{0}; //ticks*qty
            std::vector<SymbolLimits> symbol_limits_; //small vector; configured at init
            RateWindow rate_window_;
            bool performance_mode_{false}; // Performance mode flag

        public:
            explicit RiskManager(Quantity global_max_qty,int64_t global_max_notational_ticks,uint32_t global_max_orders_per_sec=1000) noexcept
                : global_max_qty_(global_max_qty),
                global_max_notational_ticks_(global_max_notational_ticks),
                rate_window_(global_max_orders_per_sec),
                performance_mode_(false) {}
                
            // Performance mode: disable expensive checks during benchmarks
            inline void set_performance_mode(bool enabled) noexcept { performance_mode_ = enabled; }
            inline bool is_performance_mode() const noexcept { return performance_mode_; }

            //add or update symbol specific limits(call in init thread, not hot path)
            inline void set_symbol_limit(Symbol sid,Quantity max_qty,int64_t max_notational_ticks) noexcept {
                for(auto &s:symbol_limits_){
                    if(s.symbol==sid){
                        s.max_qty=max_qty;
                        s.max_notational_ticks=max_notational_ticks;
                        return;
                    }
                }
                symbol_limits_.push_back({sid,max_qty,max_notational_ticks});
            }

            //Main hot-path validation: lock-free checks, minimal branching.
            //Returns true if the order passes checks; false otherwise
            inline bool validate(const Order& o) noexcept {
                DEEP_PROFILE_FUNCTION();
                // Fast path: basic quantity check with branch prediction hints
                if(__builtin_expect(o.qty <= 0, 0)) return false;
                if(__builtin_expect(o.qty > global_max_qty_, 0)) return false;

                //per symbol qty limit(if present) - optimized with early exit
                if(!symbol_limits_.empty()) {
                    for(const auto &sl:symbol_limits_){
                        if(sl.symbol==o.symbol){
                            if(__builtin_expect(o.qty > sl.max_qty, 0)) return false;
                            break;
                        }
                    }
                }

                //notional check: price*qty in ticks(using 128-bit if overflow risk)
                //price and qty are signed int 64;multiply as int 128 to be safe
                // Optimized: use absolute values to avoid sign check
                __int128 abs_price = o.price < 0 ? -static_cast<__int128>(o.price) : static_cast<__int128>(o.price);
                __int128 abs_qty = o.qty < 0 ? -static_cast<__int128>(o.qty) : static_cast<__int128>(o.qty);
                __int128 notional = abs_price * abs_qty;
                
                if(__builtin_expect(notional > static_cast<__int128>(global_max_notational_ticks_), 0)) return false;

                //per-symbol notional check - optimized with early exit
                if(!symbol_limits_.empty()) {
                    for(const auto &sl:symbol_limits_) {
                        if(sl.symbol==o.symbol) {
                            if(__builtin_expect(notional > static_cast<__int128>(sl.max_notational_ticks), 0)) return false;
                            break;
                        }
                    }
                }

                //global rate limit (light weight atomic counter) - skip in performance mode
                if(!performance_mode_ && !rate_window_.try_acquire()) return false;

                //passed all checks
                return true;
            }

            // Alias for validate method for compatibility
            inline bool check_risk(const Order& o) noexcept {
                return validate(o);
            }
            
            //Inspectors(no locks)
            inline Quantity global_max_qty() const noexcept {
                return global_max_qty_;
            }
            inline uint64_t global_max_notional_ticks() const noexcept {
                return global_max_notational_ticks_;
            }

    };
};