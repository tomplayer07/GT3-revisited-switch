#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace gt3r::deko_probe {
void trace(const char*);
using TimingClock = std::chrono::steady_clock;
inline double elapsed_ms(TimingClock::time_point a, TimingClock::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
// Bounded RAM-only histogram: 0.25 ms bins, overflow at 255.75 ms.
struct TimingStats {
    std::array<uint64_t, 1024> bins{};
    uint64_t count=0, over60=0, over30=0;
    double total=0, maximum=0;
    void add(double ms) {
        ++count; total+=ms; maximum=std::max(maximum,ms);
        ++bins[std::min(size_t(ms*4), bins.size()-1)];
        over60+=ms>1000.0/60; over30+=ms>1000.0/30;
    }
    double percentile(unsigned percent) const {
        const uint64_t target=(count*percent+99)/100;
        uint64_t accumulated=0;
        for (size_t i=0; i<bins.size(); ++i) {
            accumulated+=bins[i];
            if (accumulated>=target) return double(i+1)*0.25;
        }
        return maximum;
    }
    void report(const char* label, const char* mode) const {
        if (!count) return;
        char line[384];
        std::snprintf(line,sizeof(line),
            "Timing %s mode=%s n=%llu avg_ms=%.3f p95_bin_upper_ms=%.2f p99_bin_upper_ms=%.2f max_ms=%.3f over_16_67=%llu over_33_33=%llu overflow=%llu",
            label,mode,(unsigned long long)count,total/count,percentile(95),percentile(99),maximum,
            (unsigned long long)over60,(unsigned long long)over30,(unsigned long long)bins.back());
        trace(line);
    }
};
}
