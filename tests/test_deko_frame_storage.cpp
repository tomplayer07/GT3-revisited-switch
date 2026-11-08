#include "test_framework.h"
#include "render/deko/frame_storage.h"
#include <limits>
using namespace gt3r::render::deko;
TEST(deko_cpu_visibility_prepares_once_across_models_and_views_but_again_on_bank_reuse) {
    FrameBankCycle<3> cycle;
    FrameCpuVisibility visibility;
    int invalidations=0;
    for(unsigned frame=0;frame<7;++frame) {
        cycle.begin([](unsigned){return true;},[](unsigned){CHECK(false);},[](unsigned){});
        // Multiple models and secondary views share one arena generation.
        for(unsigned model=0;model<100;++model) {
            const bool queued=visibility.prepare(cycle.generation(),[&]{++invalidations;});
            CHECK(queued==(model==0));
            CHECK(invalidations==int(frame+1));
        }
        cycle.end([](unsigned){});
    }
}
TEST(deko_cpu_visibility_failed_enqueue_does_not_allow_unsynchronized_consumers) {
    FrameCpuVisibility visibility;
    int queued=0;
    CHECK(visibility.prepare(1,[&]{++queued;}));
    bool failed=false;
    try { visibility.prepare(2,[]{throw std::runtime_error("command allocation failed");}); }
    catch(const std::runtime_error&) { failed=true; }
    CHECK(failed);
    CHECK(visibility.prepare(2,[&]{++queued;}));
    CHECK(!visibility.prepare(2,[]{CHECK(false);}));
    CHECK(queued==2);
}
TEST(deko_cpu_visibility_reprepares_after_new_memory_mapping_in_the_same_frame) {
    FrameCpuVisibility visibility;
    int queued=0;
    CHECK(visibility.prepare(1,[&]{++queued;}));
    visibility.invalidate();
    CHECK(visibility.prepare(1,[&]{++queued;}));
    CHECK(!visibility.prepare(1,[]{CHECK(false);}));
    // Growth during preparation itself is covered by that preparation.
    CHECK(visibility.prepare(2,[&]{visibility.invalidate(); ++queued;}));
    CHECK(!visibility.prepare(2,[]{CHECK(false);}));
    CHECK(queued==3);
}
TEST(deko_frame_storage_waits_only_on_bank_reuse_not_model_invocations) {
    FrameBankCycle<3> cycle; int waits=0,signals=0,resets=0;
    const auto begin=[&] { return cycle.begin([](unsigned){return false;},[&](unsigned){++waits;},[&](unsigned){++resets;}); };
    const auto end=[&] { cycle.end([&](unsigned){++signals;}); };
    CHECK(begin()==0);
    for(int model=0;model<200;++model) CHECK(begin()==0);
    CHECK(resets==1); CHECK(waits==0); CHECK(cycle.generation()==1);
    end(); end(); CHECK(signals==1);
    CHECK(begin()==1); end(); CHECK(begin()==2); end();
    CHECK(waits==0); CHECK(begin()==0); CHECK(waits==1);
    CHECK(cycle.waits()==1); CHECK(cycle.generation()==4);
    for(int view=0;view<8;++view) CHECK(begin()==0);
    CHECK(waits==1);
}
TEST(deko_frame_storage_completed_bank_reuses_without_wait_and_failed_wait_does_not_reset) {
    FrameBankCycle<1> cycle; int reset=0;
    cycle.begin([](unsigned){return true;},[](unsigned){},[&](unsigned){++reset;});
    cycle.end([](unsigned){});
    bool failed=false;
    try { cycle.begin([](unsigned){return false;},[](unsigned){throw std::runtime_error("GPU pending");},[&](unsigned){++reset;}); }
    catch(const std::runtime_error&) { failed=true; }
    CHECK(failed); CHECK(reset==1); CHECK(cycle.generation()==1);
    cycle.begin([](unsigned){return true;},[](unsigned){CHECK(false);},[&](unsigned){++reset;});
    CHECK(reset==2); CHECK(cycle.generation()==2);
}
TEST(deko_frame_slices_align_and_never_overlap_or_wrap) {
    uint32_t used=0;
    CHECK(frame_slice(1024,used,33,32)==0); CHECK(used==33);
    CHECK(frame_slice(1024,used,256,256)==256); CHECK(used==512);
    CHECK(!frame_slice(1024,used,600,256)); CHECK(used==512);
    CHECK(frame_slice(1024,used,512,256)==512); CHECK(used==1024);
    CHECK(!frame_slice(1024,used,1,1)); CHECK(used==1024);
    used=std::numeric_limits<uint32_t>::max()-1;
    CHECK(!frame_slice(std::numeric_limits<uint32_t>::max(),used,16,256));
    CHECK(used==std::numeric_limits<uint32_t>::max()-1);
}
