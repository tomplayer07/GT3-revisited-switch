#pragma once
#include "render/native_profile.h"
#include "../deko-probe/timing.h"
#include <array>
#include <vector>
#include <optional>
namespace gt3r::deko_probe {
// Bounded RAM-only race diagnostics. GPU ranges are sampled once per 32 live
// frames, after warm-up, rotating targets. Reports flush the tiled cache; these
// are diagnostic elapsed ranges, not a zero-overhead GPU busy-time measurement.
class RaceProfile {
public:
    using Pass=render::ProfilePass; using Role=render::ProfileRole; using Gpu=render::GpuProfileRange;
    static constexpr size_t passes=size_t(Pass::Count),roles=size_t(Role::Count),ranges=size_t(Gpu::Count);
    struct Counters { double record=0,wait=0,setup=0,cull=0,packets=0,submit=0; uint64_t draws=0,triangles=0,submits=0; };
    struct Slow { double cadence=0; render::RaceProfileInfo info; render::RaceProfileCpu cpu; uint64_t frame=0,bodies=0; std::array<Counters,roles> work{}; };
    bool active=false,usedGpu=false;
    uint64_t frames=0,sampleCursor=0,bodies=0,badGpu=0;
    uint64_t shadowsDisabledFrames=0;
    render::RaceProfileInfo info;
    render::RaceProfileCpu cpu;
    std::optional<Gpu> target;
    std::array<Counters,passes> pass{},totalsPass{};
    std::array<Counters,roles> role{},totalsRole{};
    std::array<TimingStats,ranges> gpu;
    std::array<TimingStats,3> trafficCadence;
    TimingStats cadence,simulation,render,environment,collection,draw,lamps,acquireCpu,uiCpu,presentCpu,animations,wet,tail;
    struct ViewCpu { double validation=0,target=0,schedule=0,dispatch=0,phase=0,textures=0,placement=0,model=0; uint64_t views=0; };
    std::array<ViewCpu,passes> frontend{};
    std::array<TimingStats,5> animationStages;
    render::RaceCadenceGate cadenceGate;
    std::vector<Slow> slow;
    struct ClockState { std::array<uint32_t,3> hz{}; uint8_t known=0; bool operator==(const ClockState&) const=default; };
    struct ClockChange { uint64_t frame; ClockState state; };
    std::vector<ClockChange> clocks;
    ClockState clocks_at(uint64_t frame) const {
        ClockState result;
        for(const auto& change:clocks) { if(change.frame>frame) break; result=change.state; }
        return result;
    }
    void clock_state(const ClockState& state) {
        if(!clocks.empty() && clocks.back().state==state) return;
        clocks.push_back({frames,state});
        if(clocks.size()>128) clocks.erase(clocks.begin());
    }
    struct GpuSlow { Gpu kind; double ms; render::RaceProfileInfo info; uint64_t frame; ClockState clocks; };
    std::vector<GpuSlow> gpuSlow;
    void begin(const render::RaceProfileInfo& value) {
        active=true; info=value; ++frames; usedGpu=false; target.reset(); bodies=0; cpu={}; pass={}; role={};
        shadowsDisabledFrames+=value.shadows_disabled;
        if(frames>=128 && frames%32==0) target=Gpu(sampleCursor++%ranges);
    }
    void model(Pass p,Role r,double record,double wait,uint64_t draws,uint64_t triangles,bool body,unsigned phase,double setup=0,double cull=0,double packets=0,double submit=0) {
        if(!active) return;
        const auto add=[&](Counters& c) { c.record+=record; c.wait+=wait; c.setup+=setup; c.cull+=cull; c.packets+=packets; c.submit+=submit; c.draws+=draws; c.triangles+=triangles; ++c.submits; };
        add(pass[size_t(p)]);
        if(p==Pass::Main) { add(role[size_t(r)]); if(body && draws && phase!=2) ++bodies; }
    }
    bool gpu_matches(Pass pass,bool post) const {
        if(!active || usedGpu || !target) return false;
        if(post) return *target==Gpu::Post;
        switch(pass) {
        case Pass::Environment:return *target==Gpu::Environment;
        case Pass::WetRoad:return *target==Gpu::WetRoad;
        case Pass::WetBlur:return *target==Gpu::WetBlur;
        case Pass::Main:return *target==Gpu::Main || *target==Gpu::MainScenery || *target==Gpu::MainCarsEffects;
        case Pass::Mirror:return *target==Gpu::Mirror;
        default:return false;
        }
    }
    void gpu_result(Gpu kind,double ms,const render::RaceProfileInfo& pos,uint64_t frame) {
        if(!std::isfinite(ms) || ms<0) { ++badGpu; return; }
        gpu[size_t(kind)].add(ms);
        gpuSlow.push_back({kind,ms,pos,frame,clocks_at(frame)});
        std::stable_sort(gpuSlow.begin(),gpuSlow.end(),[](const auto& a,const auto& b){return a.ms>b.ms;});
        if(gpuSlow.size()>16) gpuSlow.resize(16);
    }
    void present(uint64_t now,double acquire=0,double ui=0,double presentation=0) {
        const auto ms=cadenceGate.present(active,now);
        if(active) {
            acquireCpu.add(acquire); uiCpu.add(ui); presentCpu.add(presentation);
            simulation.add(cpu.simulation); render.add(cpu.total); environment.add(cpu.environment); collection.add(cpu.collection); draw.add(cpu.draw); lamps.add(cpu.lamps); animations.add(cpu.animations); wet.add(cpu.wet); tail.add(cpu.tail);
            const double animationValues[]={cpu.palettes,cpu.moving,cpu.texture_updates,cpu.geometry_updates,cpu.metadata};
            for(size_t i=0;i<animationStages.size();++i) animationStages[i].add(animationValues[i]);
            const auto accumulate=[](Counters& a,const Counters& b) { a.record+=b.record; a.wait+=b.wait; a.setup+=b.setup; a.cull+=b.cull; a.packets+=b.packets; a.submit+=b.submit; a.draws+=b.draws; a.triangles+=b.triangles; a.submits+=b.submits; };
            for(size_t i=0;i<passes;++i) accumulate(totalsPass[i],pass[i]);
            for(size_t i=0;i<roles;++i) accumulate(totalsRole[i],role[i]);
            if(ms) {
                cadence.add(*ms); trafficCadence[bodies<=1 ? 0 : bodies<=3 ? 1 : 2].add(*ms);
                slow.push_back({*ms,info,cpu,frames,bodies,role});
                std::stable_sort(slow.begin(),slow.end(),[](const auto& a,const auto& b){return a.cadence>b.cadence;});
                if(slow.size()>16) slow.resize(16);
            }
        }
        active=false;
    }
    static const char* gpu_name(Gpu kind) {
        static const char* labels[]={"environment_face","wet_scene","wet_blur","main_scenery","main_cars_effects","main_all","mirror","post_grade"}; return labels[size_t(kind)];
    }
    void report() const {
        if(!frames) return;
        trace("Native race profile: live driving only; no previews, pauses, replays or loading. GPU ranges sampled 1/32 after 128 warmup frames; reports flush tile cache and elapsed includes queue/CPU feeding gaps.");
        char shadowLog[192]; std::snprintf(shadowLog,sizeof(shadowLog),"Race shadow diagnostic: shadows_on_frames=%llu shadows_off_frames=%llu (headlamp pools unchanged)",
            (unsigned long long)(frames-shadowsDisabledFrames),(unsigned long long)shadowsDisabledFrames); trace(shadowLog);
        acquireCpu.report("race_acquire_cpu","live"); uiCpu.report("race_ui_cpu","live"); presentCpu.report("race_present_cpu","live");
        cadence.report("native_race_cadence","live"); simulation.report("race_simulation_cpu","live"); render.report("race_render_cpu","live");
        environment.report("race_environment_cpu","live"); collection.report("race_collection_cpu","live"); draw.report("race_draw_cpu","live"); lamps.report("race_glows_cpu","live");
        animations.report("race_animation_upload_cpu","live"); wet.report("race_wet_prepare_cpu","live"); tail.report("race_hud_tail_cpu","live");
        const char* animationLabels[]={"race_palette_refresh_cpu","race_moving_emit_cpu","race_texture_revision_cpu","race_dynamic_geometry_cpu","race_geometry_metadata_cpu"};
        for(size_t i=0;i<animationStages.size();++i) animationStages[i].report(animationLabels[i],"live");
        trafficCadence[0].report("race_cadence_bodies_0_1","live"); trafficCadence[1].report("race_cadence_bodies_2_3","live"); trafficCadence[2].report("race_cadence_bodies_4_plus","live");
        static const char* passNames[]={"other","environment","wet_scene","wet_blur","main","mirror","post"};
        static const char* roleNames[]={"scenery_other","cars","effects"};
        const auto work=[&](const char* name,const Counters& c) { char line[256]; std::snprintf(line,sizeof(line),"Race work %s frames=%llu submits_per_frame=%.2f draws_per_frame=%.2f triangles_per_frame=%.2f record_cpu_ms_per_frame=%.3f wait_cpu_ms_per_frame=%.3f",name,(unsigned long long)frames,double(c.submits)/frames,double(c.draws)/frames,double(c.triangles)/frames,c.record/frames,c.wait/frames); trace(line);
            char detail[320]; std::snprintf(detail,sizeof(detail),"Race record stages %s setup_ms=%.3f mesh_cull_ms=%.3f packets_ms=%.3f queue_submit_ms=%.3f (per live frame; partitions of record_cpu)",name,c.setup/frames,c.cull/frames,c.packets/frames,c.submit/frames); trace(detail); };
        for(size_t i=0;i<passes;++i) if(totalsPass[i].submits) work(passNames[i],totalsPass[i]);
        for(size_t i=0;i<roles;++i) if(totalsRole[i].submits) work(roleNames[i],totalsRole[i]);
        for(size_t i=0;i<passes;++i) if(frontend[i].views) {
            const auto& v=frontend[i]; char detail[512];
            std::snprintf(detail,sizeof(detail),"Race frontend %s frames=%llu views_per_frame=%.2f validation_ms=%.3f target_ms=%.3f schedule_ms=%.3f dispatch_ms=%.3f phase_ms=%.3f textures_ms=%.3f placement_ms=%.3f model_ms=%.3f (per live frame; phase/textures/placement/model nested in dispatch)",passNames[i],(unsigned long long)frames,double(v.views)/frames,v.validation/frames,v.target/frames,v.schedule/frames,v.dispatch/frames,v.phase/frames,v.textures/frames,v.placement/frames,v.model/frames);
            trace(detail);
        }
        for(size_t i=0;i<ranges;++i) gpu[i].report(gpu_name(Gpu(i)),"gpu_elapsed_sampled");
        char line[640];
        for(const auto& c:clocks) { std::snprintf(line,sizeof(line),"Race clocks frame=%llu cpu_hz=%u gpu_hz=%u ram_hz=%u known_mask=%u (read-only service rates, polled once per second)",(unsigned long long)c.frame,c.state.hz[0],c.state.hz[1],c.state.hz[2],unsigned(c.state.known)); trace(line); }
        if(clocks.size()>1) trace("Race profile contains clock changes: aggregate timings mix configurations; use frame-tagged samples or a constant-clock rerun.");
        for(const auto& f:slow) { std::snprintf(line,sizeof(line),"Race slow frame=%llu cadence_ms=%.3f x=%.1f z=%.1f speed=%.1f bodies=%llu opponents=%d sim_cpu=%.3f render_cpu=%.3f collect_cpu=%.3f scene_tri=%llu car_tri=%llu",(unsigned long long)f.frame,f.cadence,f.info.x,f.info.z,f.info.speed,(unsigned long long)f.bodies,f.info.opponents,f.cpu.simulation,f.cpu.total,f.cpu.collection,(unsigned long long)f.work[0].triangles,(unsigned long long)f.work[1].triangles); trace(line); }
        for(const auto& f:gpuSlow) { std::snprintf(line,sizeof(line),"Race GPU sample=%s frame=%llu elapsed_ms=%.3f x=%.1f z=%.1f speed=%.1f cpu_hz=%u gpu_hz=%u ram_hz=%u clock_mask=%u",gpu_name(f.kind),(unsigned long long)f.frame,f.ms,f.info.x,f.info.z,f.info.speed,f.clocks.hz[0],f.clocks.hz[1],f.clocks.hz[2],unsigned(f.clocks.known)); trace(line); }
        std::snprintf(line,sizeof(line),"Race GPU invalid_reports=%llu",(unsigned long long)badGpu); trace(line);
    }
};
} // namespace gt3r::deko_probe
