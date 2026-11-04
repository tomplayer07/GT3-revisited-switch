#pragma once
#include "timing.h"

namespace gt3r::deko_probe {
struct FramePhases {
    double physics=0, animation=0, vehicle=0, prepare=0;
    double acquire=0, capture=0, scene=0, ui=0, present=0;
    double modelWait=0, modelRecord=0;
    double streetGlows=0, carGlows=0;
    unsigned ticks=0;
    bool reset=false;
};
// Retain the 32 worst app loops over 25 ms; no allocation or I/O while sampling.
struct SlowFrames {
    struct Sample { uint64_t frame=0, buttons=0; double app=0, beforePresent=0; FramePhases phases; };
    std::array<Sample,32> samples{};
    size_t used=0;
    uint64_t total=0;
    void add(const Sample& sample) {
        if (sample.app<=25) return;
        ++total;
        if (used<samples.size()) { samples[used++]=sample; return; }
        auto smallest=std::min_element(samples.begin(),samples.end(),[](const auto& a,const auto& b){return a.app<b.app;});
        if (sample.app>smallest->app) *smallest=sample;
    }
    void report() const {
        char line[768];
        std::snprintf(line,sizeof(line),"Slow frames: threshold_ms=25 total=%llu retained=%zu capacity=32 (CPU wall times; nested phases overlap)",(unsigned long long)total,used); trace(line);
        auto ordered=samples;
        std::sort(ordered.begin(),ordered.begin()+used,[](const auto& a,const auto& b){return a.frame<b.frame;});
        for (size_t i=0;i<used;++i) {
            const auto& s=ordered[i]; const auto& p=s.phases;
            std::snprintf(line,sizeof(line),"Slow frame=%llu buttons=0x%llx app_ms=%.3f before_present_ms=%.3f prepare_ms=%.3f physics_ms=%.3f animation_ms=%.3f vehicle_ms=%.3f ticks=%u reset=%d street_glows_ms=%.3f car_glows_ms=%.3f acquire_ms=%.3f capture_ms=%.3f scene_ms=%.3f model_wait_ms=%.3f model_record_ms=%.3f ui_ms=%.3f present_ms=%.3f",
                (unsigned long long)s.frame,(unsigned long long)s.buttons,s.app,s.beforePresent,p.prepare,p.physics,p.animation,p.vehicle,p.ticks,int(p.reset),p.streetGlows,p.carGlows,p.acquire,p.capture,p.scene,p.modelWait,p.modelRecord,p.ui,p.present); trace(line);
        }
    }
};
}
