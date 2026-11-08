#include "test_framework.h"
#include "render/deko/instance_transform.h"
using namespace gt3r::render;
TEST(deko_instance_cache_reuses_static_placement_and_reapplies_for_another_instance) {
    deko::InstanceTransformCache cache; std::vector<Mat4> local={translate({1,0,0}),translate({0,2,0})};
    std::vector<Mat4> actual(2); int calls=0;
    auto apply=[&](size_t i,const Mat4& pose){ actual[i]=pose; ++calls; return true; };
    const auto world=translate({3,0,0});
    CHECK(cache.apply(world,local,apply)); CHECK(calls==2); CHECK(actual[0].at(0,3)==4);
    CHECK(cache.apply(world,local,apply)); CHECK(calls==2); CHECK(cache.skips==1);
    CHECK(cache.apply(translate({8,0,0}),local,apply)); CHECK(calls==4); CHECK(actual[0].at(0,3)==9);
    CHECK(cache.apply(world,local,apply)); CHECK(calls==6); CHECK(actual[0].at(0,3)==4);
    local[0]=translate({5,0,0}); cache.invalidate();
    CHECK(cache.apply(world,local,apply)); CHECK(calls==8); CHECK(actual[0].at(0,3)==8);
}
TEST(deko_instance_cache_partial_failure_invalidates_previous_placement) {
    deko::InstanceTransformCache cache; std::vector<Mat4> local(2); int calls=0;
    CHECK(cache.apply(Mat4{},local,[&](size_t,const Mat4&){++calls;return true;}));
    CHECK(!cache.apply(translate({3,0,0}),local,[](size_t i,const Mat4&){return i==0;}));
    CHECK(cache.apply(Mat4{},local,[&](size_t,const Mat4&){++calls;return true;}));
    CHECK(calls==4); // previous cache must not skip after a partially applied new pose
}
