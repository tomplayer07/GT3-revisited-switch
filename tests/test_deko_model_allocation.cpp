#include "test_framework.h"
#include "render/deko/model_allocation.h"
#include <limits>
#include <vector>
using namespace gt3r::render::deko;

TEST(deko_native_static_model_slices_preserve_separate_buffer_contents) {
    for(const auto vertices:{36u,19800u,1048576u}) {
        const uint32_t indices=vertices/3+4,descriptors=12288;
        const auto layout=model_static_layout(vertices,indices,descriptors);
        CHECK(layout.vertex==0); CHECK(layout.index%4096==0); CHECK(layout.descriptors%4096==0);
        CHECK(layout.index>=vertices); CHECK(layout.descriptors>=layout.index+indices);
        CHECK(layout.bytes>=layout.descriptors+descriptors);
        std::vector<uint8_t> storage(layout.bytes,0);
        std::fill_n(storage.begin()+layout.vertex,vertices,0x11);
        std::fill_n(storage.begin()+layout.index,indices,0x22);
        std::fill_n(storage.begin()+layout.descriptors,descriptors,0x33);
        CHECK(storage[vertices-1]==0x11);
        CHECK(storage[layout.index]==0x22); CHECK(storage[layout.index+indices-1]==0x22);
        CHECK(storage[layout.descriptors]==0x33); CHECK(storage.back()==0x33);
    }
}
TEST(deko_native_static_model_layout_rejects_empty_or_overflowing_buffers) {
    for(const auto size:{0u,std::numeric_limits<uint32_t>::max()}) {
        bool failed=false;
        try { model_static_layout(36,12,size); } catch(const std::exception&) { failed=true; }
        CHECK(failed);
    }
}

TEST(deko_static_image_slices_preserve_alignment_and_do_not_overlap) {
    AllocationSlices images;
    uint64_t end=0;
    for (unsigned i=0;i<100;++i) {
        const uint64_t alignment=i%2 ? 512 : 4096;
        const uint64_t bytes=1024+37*i;
        const auto offset=images.reserve(bytes,alignment);
        CHECK(offset%alignment==0);
        CHECK(offset>=end);
        end=uint64_t(offset)+bytes;
        CHECK(images.size()==end);
    }
}

TEST(deko_static_image_slices_reject_overflow_without_consuming_space) {
    AllocationSlices slices(1024);
    CHECK(slices.reserve(1000,1)==0);
    for (const auto alignment:{0u,256u}) {
        bool threw=false;
        try { slices.reserve(25,alignment); } catch(const std::overflow_error&) { threw=true; }
        CHECK(threw); CHECK(slices.size()==1000);
    }
    bool threw=false;
    try { slices.reserve(std::numeric_limits<uint64_t>::max(),1); }
    catch(const std::overflow_error&) { threw=true; }
    CHECK(threw); CHECK(slices.size()==1000);
    CHECK(slices.reserve(24,1)==1000); CHECK(slices.size()==1024);
}

TEST(deko_showroom_buffers_fit_model_while_race_budget_stays_unchanged) {
    const auto small=model_buffer_budget(300,true);
    CHECK(small.slots==2); CHECK(small.draws==300);
    CHECK(small.commands>=300*1024);
    CHECK(small.slots*(small.commands+small.draws*256)<1024*1024);
    const auto race=model_buffer_budget(300,false);
    CHECK(race.slots==3); CHECK(race.draws==8192); CHECK(race.commands==4*1024*1024);
    const auto seattle=model_buffer_budget(8615,true);
    CHECK(seattle.commands==race.commands); CHECK(seattle.draws==8615);
    const auto maximum=model_buffer_budget(16384,true);
    CHECK(maximum.commands==race.commands); CHECK(maximum.draws==16384);
    for(const auto invalid:{0u,16385u}) {
        bool threw=false;
        try { model_buffer_budget(invalid,true); } catch(const std::invalid_argument&) { threw=true; }
        CHECK(threw);
    }
}

TEST(deko_native_initial_upload_pool_bounds_small_parts_and_largest_texture_table) {
    CHECK(model_upload_command_bytes(0,true)==64*1024);
    CHECK(model_upload_command_bytes(1,true)<128*1024);
    CHECK(model_upload_command_bytes(446,true)>=64*1024+446*512);
    CHECK(model_upload_command_bytes(1304,true)>=64*1024+1304*512);
    CHECK(model_upload_command_bytes(4097,true)<4*1024*1024);
    CHECK(model_upload_command_bytes(4097,true)%4096==0);
    CHECK(model_upload_command_bytes(1,false)==4*1024*1024);
    bool threw=false;
    try { model_upload_command_bytes(4098,true); } catch(const std::invalid_argument&) { threw=true; }
    CHECK(threw);
}
