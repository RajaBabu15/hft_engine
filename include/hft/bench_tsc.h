#pragma once

#include "types.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#ifdef __APPLE__
    #include <mach/mach_time.h>
#endif

namespace hft {
    namespace bench {

        struct Config {
            std::string cal_file;   
            bool        force_recal = false;
            std::size_t samples     = 200000;
            std::size_t warmup      = 1000;
            unsigned    duration_ms = 200;
        };

        struct Stats {
            std::size_t samples = 0;
            std::uint64_t min = 0;
            std::uint64_t p50 = 0;
            std::uint64_t p95 = 0;
            std::uint64_t p99 = 0;
            std::uint64_t max = 0;
            double mean = 0.0;
            double stddev = 0.0;
        };

        inline Stats compute_stats(std::vector<std::uint64_t>& v) {
            Stats s{};
            if (v.empty()) return s;

            std::sort(v.begin(), v.end());
            s.samples = v.size();
            s.min = v.front();
            s.max = v.back();

            const auto n = v.size();
            const auto sum = std::accumulate(v.begin(), v.end(), std::uint64_t(0));
            s.mean = static_cast<double>(sum) / static_cast<double>(n);

            double accum = 0.0;
            for (auto x : v) {
                const double d = static_cast<double>(x) - s.mean;
                accum += d * d;
            }
            s.stddev = n > 1 ? std::sqrt(accum / static_cast<double>(n - 1)) : 0.0;

            auto p = [&](double pct) -> std::uint64_t {
                const std::size_t idx = static_cast<std::size_t>(
                    std::min<double>(n - 1, std::floor((pct / 100.0) * n))
                );
                return v[idx];
            };

            s.p50 = p(50.0);
            s.p95 = p(95.0);
            s.p99 = p(99.0);
            return s;
        }

        inline void print_stats(const std::string& label,
                                std::vector<std::uint64_t>& v,
                                std::ostream& os = std::cout) {
            if (v.empty()) {
                os << label << ": no samples\n";
                return;
            }
            Stats s = compute_stats(v);
            os  << std::fixed << std::setprecision(2);
            os  << label
                << " samples=" << s.samples
                << " min=" << s.min
                << " med=" << s.p50
                << " mean=" << s.mean
                << " 95%=" << s.p95
                << " 99%=" << s.p99
                << " max=" << s.max
                << " stddev=" << s.stddev
                << "\n";
        }

        template <typename F>
        inline std::vector<std::uint64_t> measure_deltas(F f, std::size_t warmup, std::size_t samples) {
            std::vector<std::uint64_t> out;
            out.reserve(samples);

            for (std::size_t i = 0; i < warmup; ++i) (void)f();

            std::uint64_t prev = f();
            for (std::size_t i = 0; i < samples; ++i) {
                const std::uint64_t cur = f();
                const std::uint64_t d = (cur >= prev) ? (cur - prev) : 0;
                out.push_back(d);
                prev = cur;
            }
            return out;
        }

        inline double fraction_leq(const std::vector<std::uint64_t>& v, std::uint64_t threshold) {
            if (v.empty()) return 0.0;
            std::size_t c = 0;
            for (auto x : v) if (x <= threshold) ++c;
            return 100.0 * static_cast<double>(c) / static_cast<double>(v.size());
        }

        struct Output {
            bool tsc_enabled = false;
            std::vector<std::uint64_t> v_tsc;
            std::vector<std::uint64_t> v_steady;
            #ifdef __APPLE__
                std::vector<std::uint64_t> v_mach;
            #endif
        };

        inline Output run(const Config& cfg, std::ostream& os = std::cout) {
            Output out{};

            os  << "bench_tsc: samples=" << cfg.samples
                << " duration_ms=" << cfg.duration_ms << "\n";

            if (!cfg.cal_file.empty()) {
                os  << "calibration file: " << cfg.cal_file
                    << (cfg.force_recal ? " (force recalibrate)\n" : " (try load)\n");
                if (!hft::calibrate_tsc_with_persistence(cfg.cal_file, cfg.force_recal, cfg.duration_ms)) {
                    os << "TSC calibration failed or not available — falling back to steady_clock only\n";
                } else {
                    os << "TSC calibration loaded/enabled\n";
                }
            } else {
                if (!hft::calibrate_tsc(cfg.duration_ms)) {
                    os << "TSC calibration failed or not available\n";
                } else {
                    os << "TSC calibrated (in-memory only)\n";
                }
            }

            out.tsc_enabled = hft::tsc_enabled();
            os << "TSC enabled: " << (out.tsc_enabled ? "yes\n" : "no\n");

            os << "Measuring hft::now_ns() deltas...\n";
            out.v_tsc = measure_deltas([](){ return hft::now_ns(); }, cfg.warmup, cfg.samples);
            {
                auto tmp = out.v_tsc; 
                print_stats("hft::now_ns()", tmp, os);
            }

            os << "Measuring std::chrono::steady_clock::now() deltas...\n";
            out.v_steady = measure_deltas([](){
                return static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
            }, cfg.warmup, cfg.samples);
            {
                auto tmp = out.v_steady;
                print_stats("steady_clock::now()", tmp, os);
            }

            #ifdef __APPLE__
                os << "Measuring mach_absolute_time() deltas...\n";
                out.v_mach = measure_deltas([](){
                    static mach_timebase_info_data_t s_timebase = {};
                    if (s_timebase.denom == 0) {
                        (void)mach_timebase_info(&s_timebase);
                    }
                    const std::uint64_t t = mach_absolute_time();
                    return t * s_timebase.numer / s_timebase.denom;
                }, cfg.warmup, cfg.samples);
                {
                    auto tmp = out.v_mach;
                    print_stats("mach_absolute_time()", tmp, os);
                }
            #endif

            os << "Fraction of now_ns() deltas <= 100 ns: " << fraction_leq(out.v_tsc, 100) << "%\n";
            os << "Fraction of steady_clock deltas <= 100 ns: " << fraction_leq(out.v_steady, 100) << "%\n";
            #ifdef __APPLE__
                os << "Fraction of mach_absolute_time deltas <= 100 ns: " << fraction_leq(out.v_mach, 100) << "%\n";
            #endif

            return out;
        }
    };
};
