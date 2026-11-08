#pragma once
#include <array>
#include <optional>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
namespace gt3r::render::deko {
// Fresh, aligned, non-overlapping CPU-written slices need one GPU L2
// invalidation per arena generation. Publish only after its command is queued.
// Mutable staging buffers/images have independent synchronization requirements.
class FrameCpuVisibility {
    uint64_t generation_=0;
    bool prepared_=false;
public:
    void invalidate() { prepared_=false; }
    template<class Prepare> bool prepare(uint64_t generation,Prepare&& enqueue) {
        if(prepared_ && generation_==generation) return false;
        enqueue();
        generation_=generation; prepared_=true;
        return true;
    }
};
// The same bank serves every model/view in a picture. Its storage can only be
// reset after the last GPU submission using that bank completes.
template<size_t Banks> class FrameBankCycle {
    static_assert(Banks>0);
    std::array<bool,Banks> submitted_{};
    unsigned bank_=Banks-1; bool active_=false;
    uint64_t generation_=0,waits_=0;
public:
    template<class Ready,class Wait,class Reset> unsigned begin(Ready&& ready,Wait&& wait,Reset&& reset) {
        if(active_) return bank_;
        const unsigned next=(bank_+1)%Banks;
        if(submitted_[next] && !ready(next)) { wait(next); ++waits_; }
        reset(next); bank_=next; submitted_[bank_]=false; active_=true; ++generation_; return bank_;
    }
    template<class Signal> void end(Signal&& signal) {
        if(!active_) return;
        signal(bank_); submitted_[bank_]=true; active_=false;
    }
    uint64_t generation() const { return generation_; }
    uint64_t waits() const { return waits_; }
};
inline std::optional<uint32_t> frame_slice(uint32_t capacity,uint32_t& used,uint32_t bytes,uint32_t alignment) {
    if(!bytes || !alignment) throw std::invalid_argument("Invalid native frame slice");
    const uint64_t offset=(uint64_t(used)+alignment-1)/alignment*alignment;
    if(offset>capacity || uint64_t(bytes)>capacity-offset) return {};
    used=uint32_t(offset+bytes); return uint32_t(offset);
}
} // namespace gt3r::render::deko
