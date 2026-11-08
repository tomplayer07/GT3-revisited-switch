#include "test_framework.h"
#include "render/deko/draw_schedule.h"
using namespace gt3r::render;
TEST(deko_race_schedule_finishes_joined_scenery_before_cars_and_smoke) {
    std::vector<DrawItem> items(6);
    items[0].background=true;
    items[1].in_order=true; items[1].depth_reset=true;
    items[2].in_order=items[2].in_order_join=items[2].depth_reset=true;
    items[5].late=true;
    const auto draws=deko::draw_schedule(items,{},false);
    CHECK(draws.size()==10);
    CHECK(draws[0].item==0 && draws[0].phase==0);
    CHECK(draws[1].item==1 && draws[1].phase==1 && !draws[1].reset_depth);
    CHECK(draws[2].item==2 && draws[2].phase==1 && !draws[2].reset_depth);
    CHECK(draws[3].item==1 && draws[3].phase==2 && !draws[3].reset_depth);
    CHECK(draws[4].item==2 && draws[4].phase==2 && draws[4].reset_depth);
    CHECK(draws[5].item==3 && draws[5].phase==1);
    CHECK(draws[6].item==4 && draws[6].phase==1);
    CHECK(draws[7].item==3 && draws[7].phase==2);
    CHECK(draws[8].item==4 && draws[8].phase==2);
    CHECK(draws[9].item==5 && draws[9].phase==0);
}
TEST(deko_race_translucent_cars_sort_far_to_near_without_reordering_opaque) {
    std::vector<DrawItem> items(3);
    items[0].transform=translate({0,0,5}); items[1].transform=translate({0,0,20}); items[2].transform=translate({0,0,10});
    const auto draws=deko::draw_schedule(items,{},true);
    CHECK(draws.size()==6);
    for(size_t i=0;i<3;++i) CHECK(draws[i].item==i && draws[i].phase==1);
    CHECK(draws[3].item==1 && draws[3].phase==2);
    CHECK(draws[4].item==2 && draws[4].phase==2);
    CHECK(draws[5].item==0 && draws[5].phase==2);
}
TEST(deko_race_schedule_allows_only_background_or_only_smoke_views) {
    DrawItem sky; sky.background=true;
    DrawItem smoke; smoke.late=true;
    const auto draws=deko::draw_schedule({smoke,sky},{},true);
    CHECK(draws.size()==2);
    CHECK(draws[0].item==1); CHECK(draws[1].item==0);
    CHECK(deko::draw_schedule({}, {},true).empty());
}
