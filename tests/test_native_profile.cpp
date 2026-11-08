#include "test_framework.h"
#include "render/native_profile.h"
using namespace gt3r::render;
TEST(native_race_cadence_excludes_loading_pause_and_first_frame_after_menu) {
    RaceCadenceGate gate;
    CHECK(!gate.present(false,1000000));
    CHECK(!gate.present(true,40000000000ull)); // loading gap excluded
    auto dt=gate.present(true,40016666667ull); CHECK(dt); if(dt) CHECK(*dt>16.66 && *dt<16.68);
    CHECK(!gate.present(false,50000000000ull)); // paused
    CHECK(!gate.present(true,90000000000ull)); // resume gap excluded
    dt=gate.present(true,90033333333ull); CHECK(dt); if(dt) CHECK(*dt>33.33 && *dt<33.34);
}
TEST(native_race_cadence_and_gpu_reports_reject_uninitialized_or_reversed_time) {
    RaceCadenceGate gate;
    CHECK(!gate.present(true,100)); CHECK(!gate.present(true,100)); CHECK(!gate.present(true,99));
    CHECK(!valid_gpu_interval(0,100)); CHECK(!valid_gpu_interval(200,100));
    CHECK(valid_gpu_interval(100,100)); CHECK(valid_gpu_interval(100,200));
}

#include "../tools/switch/deko-model/race_profile.h"
using gt3r::deko_probe::RaceProfile;
TEST(native_race_profile_rotates_rare_gpu_ranges_after_warmup) {
    RaceProfile profile;
    unsigned samples=0;
    for(unsigned i=1;i<=352;++i) {
        profile.begin({});
        if(i<128 || i%32) CHECK(!profile.target);
        else {
            CHECK(profile.target); if(profile.target) CHECK(size_t(*profile.target)==samples++);
        }
        profile.present(uint64_t(i)*16666667);
    }
    CHECK(samples==size_t(GpuProfileRange::Count));
    CHECK(!profile.active);
    CHECK(profile.cadence.count==351);
}
TEST(native_race_profile_ignores_menu_gaps_and_counts_only_drawn_main_bodies) {
    RaceProfile profile;
    profile.begin({}); profile.present(1000000);
    profile.present(40000000000ull); // menu
    profile.begin({});
    profile.model(ProfilePass::Environment,ProfileRole::Car,1,0,5,100,true,1);
    CHECK(profile.bodies==0); CHECK(profile.role[size_t(ProfileRole::Car)].draws==0);
    profile.model(ProfilePass::Main,ProfileRole::Car,1,0,5,100,true,1);
    profile.model(ProfilePass::Main,ProfileRole::Car,1,0,2,20,true,2);
    profile.model(ProfilePass::Main,ProfileRole::Car,1,0,0,0,true,1);
    CHECK(profile.bodies==1); CHECK(profile.role[size_t(ProfileRole::Car)].triangles==120);
    profile.present(50000000000ull); CHECK(profile.cadence.count==0);
    profile.begin({}); profile.present(50016666667ull); CHECK(profile.cadence.count==1);
}
TEST(native_race_profile_bounds_slow_records_and_rejects_bad_gpu_reports) {
    RaceProfile profile;
    for(unsigned i=1;i<=100;++i) { profile.begin({}); profile.present(uint64_t(i)*20000000); }
    CHECK(profile.slow.size()==16);
    profile.gpu_result(GpuProfileRange::Main,-1,{},0);
    CHECK(profile.badGpu==1); CHECK(profile.gpu[size_t(GpuProfileRange::Main)].count==0);
    for(unsigned i=0;i<100;++i) profile.gpu_result(GpuProfileRange::Main,double(i),{},i);
    CHECK(profile.gpuSlow.size()==16); CHECK(profile.gpuSlow.front().ms==99);
    CHECK(profile.gpu[size_t(GpuProfileRange::Main)].count==100);
}
TEST(native_race_clock_changes_keep_gpu_samples_associated_with_original_configuration) {
    RaceProfile profile;
    RaceProfile::ClockState oc{{1700000000,900000000,1800000000},7};
    RaceProfile::ClockState stock{{1020000000,460800000,1600000000},7};
    profile.begin({}); profile.clock_state(oc); profile.clock_state(oc); CHECK(profile.clocks.size()==1);
    profile.present(1);
    profile.begin({}); profile.clock_state(stock); CHECK(profile.clocks.size()==2);
    CHECK(profile.clocks_at(1)==oc); CHECK(profile.clocks_at(2)==stock);
    profile.gpu_result(GpuProfileRange::Main,5,{},1); // arrives after the clock change
    CHECK(profile.gpuSlow[0].clocks==oc);
    profile.clock_state({}); CHECK(profile.clocks.back().state.known==0);
}
