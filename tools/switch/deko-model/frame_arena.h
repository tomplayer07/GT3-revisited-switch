#pragma once
#include <deko3d.h>
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include "render/native_profile.h"
#include "render/deko/frame_storage.h"
namespace gt3r::deko_probe {
// Immutable slices for every invocation in a frame. Only bank reuse waits for
// the GPU; drawing a wheel/model several times never overwrites earlier draws.
class FrameArena {
    struct Chunk { DkMemBlock memory{}; uint32_t size=0,used=0; };
    struct Report {
        render::GpuProfileRange kind; render::RaceProfileInfo info; uint64_t frame;
        DkMemBlock memory; uint32_t offset; bool ended=false;
    };
    struct Bank { std::vector<Chunk> chunks; DkFence fence{}; size_t cursor=0; DkCmdBuf query{}; std::vector<Report> reports; };
    DkDevice device_; DkQueue queue_;
    std::array<Bank,3> banks_;
    render::deko::FrameBankCycle<3> cycle_;
    unsigned bank_=0; uint64_t bytes_=0;
    render::deko::FrameCpuVisibility cpuVisibility_;
    uint64_t cpuInvalidations_=0;
    void* sink_=nullptr;
    void (*report_)(void*,render::GpuProfileRange,double,const render::RaceProfileInfo&,uint64_t)=nullptr;
    void collect_reports(unsigned bank) {
        for(const auto& sample:banks_[bank].reports) {
            const auto* values=reinterpret_cast<const volatile uint64_t*>(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(sample.memory))+sample.offset);
            const uint64_t start=values[1],end=values[3]; // Deko reports {counter,timestamp}, 16 bytes each
            const double ms=sample.ended && render::valid_gpu_interval(start,end) ? double(dkTimestampToNs(end-start))/1e6 : -1;
            if(report_) report_(sink_,sample.kind,ms,sample.info,sample.frame);
        }
        banks_[bank].reports.clear();
    }
    void timestamp(DkMemBlock memory,uint32_t offset) {
        const auto commands=allocate(4096);
        auto& bank=banks_[bank_];
        if(!bank.query) { DkCmdBufMaker maker; dkCmdBufMakerDefaults(&maker,device_); bank.query=dkCmdBufCreate(&maker); if(!bank.query) throw "Native GPU query command creation failed"; }
        dkCmdBufClear(bank.query); dkCmdBufAddMemory(bank.query,commands.memory,commands.offset,commands.size);
        dkCmdBufReportCounter(bank.query,DkCounter_Timestamp,dkMemBlockGetGpuAddr(memory)+offset);
        dkQueueSubmitCommands(queue_,dkCmdBufFinishList(bank.query));
    }
public:
    struct Slice { DkMemBlock memory{}; uint32_t offset=0,size=0; DkGpuAddr gpu() const { return dkMemBlockGetGpuAddr(memory)+offset; }
        void* cpu() const { return static_cast<uint8_t*>(dkMemBlockGetCpuAddr(memory))+offset; } };
    FrameArena(DkDevice device,DkQueue queue):device_(device),queue_(queue) {}
    ~FrameArena() {
        if(!dkQueueIsInErrorState(queue_)) { dkQueueWaitIdle(queue_); for(unsigned i=0;i<banks_.size();++i) collect_reports(i); }
        for(auto& bank:banks_) { if(bank.query) dkCmdBufDestroy(bank.query); for(auto& chunk:bank.chunks) dkMemBlockDestroy(chunk.memory); }
    }
    void gpu_sink(void* state,decltype(report_) callback) { sink_=state; report_=callback; }
    size_t begin_gpu(render::GpuProfileRange kind,const render::RaceProfileInfo& info,uint64_t frame) {
        auto data=allocate(32); std::memset(data.cpu(),0,32);
        auto& reports=banks_[bank_].reports;
        const auto index=reports.size(); reports.push_back({kind,info,frame,data.memory,data.offset,false});
        timestamp(data.memory,data.offset); return index;
    }
    void end_gpu(size_t index) {
        auto& reports=banks_[bank_].reports;
        if(index>=reports.size() || reports[index].ended) return;
        const auto memory=reports[index].memory; const auto offset=reports[index].offset;
        timestamp(memory,offset+16); reports[index].ended=true;
    }
    unsigned begin() {
        bank_=cycle_.begin(
            [&](unsigned i) { return dkFenceWait(&banks_[i].fence,0)==DkResult_Success; },
            [&](unsigned i) { dkQueueFlush(queue_); if(dkFenceWait(&banks_[i].fence,-1)!=DkResult_Success) throw "Native frame arena fence failed"; },
            [&](unsigned i) { collect_reports(i); banks_[i].cursor=0; for(auto& chunk:banks_[i].chunks) chunk.used=0; });
        return bank_;
    }
    uint64_t generation() const { return cycle_.generation(); }
    uint64_t waits() const { return cycle_.waits(); }
    uint64_t bytes() const { return bytes_; }
    uint64_t cpu_invalidations() const { return cpuInvalidations_; }
    void prepare_cpu_reads() {
        begin();
        cpuVisibility_.prepare(generation(),[&] {
            const auto commands=allocate(4096);
            auto& bank=banks_[bank_];
            if(!bank.query) { DkCmdBufMaker maker; dkCmdBufMakerDefaults(&maker,device_); bank.query=dkCmdBufCreate(&maker); if(!bank.query) throw "Native visibility command creation failed"; }
            dkCmdBufClear(bank.query); dkCmdBufAddMemory(bank.query,commands.memory,commands.offset,commands.size);
            // Reused banks have completed their fence. No earlier draw in this
            // generation can reference a later allocation: every slice is
            // aligned, disjoint and immutable through the frame's final fence.
            // Enqueue before the first consumer; subsequent submissions only
            // write fresh slices, never the cached addresses of earlier draws.
            dkCmdBufBarrier(bank.query,DkBarrier_None,DkInvalidateFlags_L2Cache);
            dkQueueSubmitCommands(queue_,dkCmdBufFinishList(bank.query));
            ++cpuInvalidations_;
        });
    }
    Slice allocate(uint32_t size,uint32_t align=256) {
        begin(); if(!size || !align || size>32u*1024*1024) throw "Invalid native frame arena request";
        auto& bank=banks_[bank_];
        for(;;) {
            if(bank.cursor==bank.chunks.size()) {
                const uint32_t capacity=(std::max(4u*1024*1024,size+align)+4095)&~4095u;
                dkQueueFlush(queue_); // dispatch before driver mapping/heap changes
                DkMemBlockMaker maker; dkMemBlockMakerDefaults(&maker,device_,capacity);
                maker.flags=DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached;
                auto memory=dkMemBlockCreate(&maker); if(!memory) throw "Native frame arena allocation failed";
                bank.chunks.push_back({memory,capacity,0}); bytes_+=capacity;
                // Heap growth may map pages not covered by the earlier arena
                // visibility preparation. Conservatively prepare again before
                // the first consumer of the new mapping.
                cpuVisibility_.invalidate();
            }
            auto& chunk=bank.chunks[bank.cursor];
            if(auto offset=render::deko::frame_slice(chunk.size,chunk.used,size,align)) return {chunk.memory,*offset,size};
            ++bank.cursor;
        }
    }
    void end() {
        cycle_.end([&](unsigned i) { dkQueueSignalFence(queue_,&banks_[i].fence,!banks_[i].reports.empty()); });
    }
};
} // namespace gt3r::deko_probe
