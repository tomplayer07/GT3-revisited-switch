#pragma once
#include <array>
#include <memory>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace gt3r::render::deko {
// Many views share one visible frame. Sweep ordinary retirements once per
// present epoch; a full target pool may force another sweep after GPU progress.
// This is a scheduling gate only: individual fences still authorize retirement.
class FrameResourceSweep {
    uint64_t frame_=0;
    bool collected_=false;
public:
    uint64_t scheduled=0,forced=0,skipped=0;
    template<class Collect> void collect(uint64_t frame,bool force,Collect&& sweep) {
        if(!force && collected_ && frame_==frame) { ++skipped; return; }
        sweep();
        frame_=frame; collected_=true;
        if(force) ++forced; else ++scheduled;
    }
};
// Readers retire on GPU completion even if their model is never drawn again.
// The publisher must finish a slot's previous submission before reusing it.
template<class Fence,size_t Slots> class FencedReadSlots {
    struct Slot { Fence fence{}; std::shared_ptr<void> resources; };
    std::array<Slot,Slots> slots_;
public:
    void publish(size_t slot,const Fence& fence,std::shared_ptr<void> resources) {
        slots_.at(slot)={fence,std::move(resources)};
    }
    template<class Complete> size_t collect(Complete&& complete) {
        size_t retired=0;
        for(auto& slot:slots_) if(slot.resources && complete(slot.fence)) {
            slot.resources.reset(); ++retired;
        }
        return retired;
    }
};
// An invocation list also supports several reads of the same model in one
// frame bank: publishing a later fence never drops an earlier unfinished read.
template<class Fence> class FencedReadList {
    struct Read { Fence fence; std::shared_ptr<void> resources; };
    std::vector<Read> reads_;
public:
    void publish(const Fence& fence,std::shared_ptr<void> resources) {
        if(resources) reads_.push_back({fence,std::move(resources)});
    }
    template<class Complete> size_t collect(Complete&& complete) {
        const auto before=reads_.size();
        std::erase_if(reads_,[&](Read& read) { return complete(read.fence); });
        return before-reads_.size();
    }
};
} // namespace gt3r::render::deko
