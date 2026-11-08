#pragma once
#include <switch.h>
#include "race_profile.h"
namespace gt3r::deko_probe {
// Read-only. Never calls either clock-setting API. Unknown services/modules
// stay explicitly unknown rather than substituting expected stock frequencies.
class ClockReader {
    bool attempted_=false,ready_=false,modern_=false;
    std::array<ClkrstSession,3> sessions_{};
    std::array<bool,3> open_{};
    uint64_t previous_=0;
    RaceProfile::ClockState state_;
    void initialize() {
        attempted_=true; modern_=hosversionAtLeast(8,0,0);
        const Result rc=modern_ ? clkrstInitialize() : pcvInitialize();
        ready_=R_SUCCEEDED(rc);
        if(!ready_) { char line[128]; std::snprintf(line,sizeof(line),"Race clock read service unavailable: result=0x%08x",unsigned(rc)); trace(line); return; }
        if(modern_) {
            const PcvModuleId modules[]={PcvModuleId_CpuBus,PcvModuleId_GPU,PcvModuleId_EMC};
            for(size_t i=0;i<3;++i) open_[i]=R_SUCCEEDED(clkrstOpenSession(&sessions_[i],modules[i],3));
        }
    }
public:
    ~ClockReader() {
        if(!ready_) return;
        if(modern_) { for(size_t i=0;i<3;++i) if(open_[i]) clkrstCloseSession(&sessions_[i]); clkrstExit(); }
        else pcvExit();
    }
    RaceProfile::ClockState poll(uint64_t now) {
        if(previous_ && now>=previous_ && now-previous_<1000000000ull) return state_;
        previous_=now;
        if(!attempted_) initialize();
        state_={};
        if(ready_) {
            const PcvModule modules[]={PcvModule_CpuBus,PcvModule_GPU,PcvModule_EMC};
            for(size_t i=0;i<3;++i) {
                uint32_t hz=0;
                const Result rc=modern_ ? (open_[i] ? clkrstGetClockRate(&sessions_[i],&hz) : Result(1)) : pcvGetClockRate(modules[i],&hz);
                if(R_SUCCEEDED(rc) && hz) { state_.hz[i]=hz; state_.known|=uint8_t(1u<<i); }
            }
        }
        return state_;
    }
};
} // namespace gt3r::deko_probe
