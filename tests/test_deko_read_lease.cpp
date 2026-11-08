#include "test_framework.h"
#include "render/deko/read_lease.h"
#include <stdexcept>
using namespace gt3r::render::deko;
TEST(deko_view_resource_sweep_preserves_fences_and_retries_under_same_frame_pressure) {
    FrameResourceSweep sweep;
    FencedReadList<int> readers;
    auto image=std::make_shared<int>(1); std::weak_ptr<int> alive=image;
    readers.publish(1,image); image.reset();
    int completed=0,calls=0;
    const auto collect=[&] { ++calls; readers.collect([&](int fence){return fence<=completed;}); };
    // Six cubemap faces and the main/post views share the same frame.
    for(int view=0;view<8;++view) sweep.collect(0,false,collect);
    CHECK(calls==1); CHECK(!alive.expired());
    completed=1;
    sweep.collect(0,false,collect); CHECK(!alive.expired());
    // A full target pool must be able to observe new GPU progress immediately.
    sweep.collect(0,true,collect); CHECK(alive.expired()); CHECK(calls==2);
    auto newer=std::make_shared<int>(2); std::weak_ptr<int> pending=newer;
    readers.publish(2,newer); newer.reset();
    sweep.collect(1,false,collect); CHECK(!pending.expired());
    completed=2;
    sweep.collect(2,false,collect); CHECK(pending.expired());
    CHECK(sweep.scheduled==3); CHECK(sweep.forced==1); CHECK(sweep.skipped==8);
}
TEST(deko_view_resource_sweep_failed_collection_is_retried_before_retirement) {
    FrameResourceSweep sweep; int collected=0; bool failed=false;
    try { sweep.collect(0,false,[]{throw std::runtime_error("collection interrupted");}); }
    catch(const std::runtime_error&) { failed=true; }
    CHECK(failed); CHECK(sweep.scheduled==0);
    sweep.collect(0,false,[&]{++collected;}); CHECK(collected==1);
    sweep.collect(0,false,[]{CHECK(false);});
    sweep.collect(1,false,[&]{++collected;}); CHECK(collected==2);
}
TEST(deko_inactive_model_releases_sampled_target_after_fence_without_redraw) {
    FencedReadSlots<int,2> slots;
    auto image=std::make_shared<int>(42); std::weak_ptr<int> alive=image;
    int fence=3; slots.publish(0,fence,image); image.reset(); fence=99;
    CHECK(slots.collect([](int v){return v<=2;})==0); CHECK(!alive.expired());
    CHECK(slots.collect([](int v){return v<=3;})==1); CHECK(alive.expired());
    CHECK(slots.collect([](int){return true;})==0);
}
TEST(deko_sampled_cube_stays_alive_until_all_consumer_slots_finish) {
    FencedReadSlots<int,2> slots;
    auto cube=std::make_shared<int>(6); std::weak_ptr<int> alive=cube;
    slots.publish(0,1,cube); slots.publish(1,2,cube); cube.reset();
    CHECK(slots.collect([](int f){return f==1;})==1); CHECK(!alive.expired());
    CHECK(slots.collect([](int f){return f==2;})==1); CHECK(alive.expired());
}
TEST(deko_many_invocations_in_one_frame_keep_every_unfinished_reader) {
    FencedReadList<int> reads;
    auto a=std::make_shared<int>(1),b=std::make_shared<int>(2);
    std::weak_ptr<int> old=a,newer=b;
    reads.publish(10,a); reads.publish(11,b); a.reset(); b.reset();
    CHECK(reads.collect([](int fence){return fence<=9;})==0);
    CHECK(!old.expired()); CHECK(!newer.expired());
    CHECK(reads.collect([](int fence){return fence<=10;})==1);
    CHECK(old.expired()); CHECK(!newer.expired());
    CHECK(reads.collect([](int){return true;})==1); CHECK(newer.expired());
}
