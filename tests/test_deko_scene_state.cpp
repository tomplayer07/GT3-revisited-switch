#include "test_framework.h"
#include "render/deko/scene_state.h"
#include <limits>
using namespace gt3r::render;
TEST(deko_per_item_lights_replace_global_lights_and_scale_each_component) {
    DrawOptions opt; opt.fill_count=3;
    ItemLights lights; lights.count=2; lights.ambient[0]=2;
    lights.dir[1][2]=-1; lights.diffuse[1][1]=4; lights.specular[1][2]=8;
    DrawItem item; item.lights=&lights;
    item.light_scale[0]=0.5f; item.light_scale[1]=0.25f; item.light_scale[2]=0.125f;
    const auto packed=deko::scene_lights(opt,&item);
    CHECK(packed[0]==1); CHECK(packed[10]==-1); CHECK(packed[7]==1); CHECK(packed[11]==1);
    CHECK(packed[25]==1); CHECK(packed[42]==1);
    CHECK(packed[15]==0); CHECK(packed[19]==0); // global fill 3 never leaks in
    lights.ambient[0]=100; CHECK(packed[0]==1); // upload is an owned copy
}
TEST(deko_item_light_validation_rejects_out_of_range_and_nonfinite_inputs) {
    ItemLights lights; lights.count=0; CHECK(deko::valid_scene_lights(lights));
    lights.count=5; CHECK(!deko::valid_scene_lights(lights));
    lights.count=-1; CHECK(!deko::valid_scene_lights(lights));
    lights.count=1; lights.specular[0][2]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!deko::valid_scene_lights(lights));
}
TEST(deko_mirrored_culling_preserves_double_sided_and_background_draws) {
    CHECK(deko::scene_cull(0,true,false)==0);
    CHECK(deko::scene_cull(1,true,false)==2);
    CHECK(deko::scene_cull(2,true,false)==1);
    CHECK(deko::scene_cull(1,false,false)==1);
    CHECK(deko::scene_cull(2,false,true)==0);
    CHECK(deko::scene_cull(1,true,true)==0);
}
TEST(deko_native_race_body_culling_preserves_material_overrides_and_mirrored_views) {
    DrawOptions opt; DrawItem body; body.car_surface=true;
    CHECK(deko::native_packet_cull(0,true,true,false,&opt,&body)==1);
    CHECK(deko::native_packet_cull(gt3r::scene::kDoubleSided,true,true,false,&opt,&body)==0);
    opt.mirrored=true;
    CHECK(deko::native_packet_cull(0,true,true,false,&opt,&body)==2);
    CHECK(deko::native_packet_cull(gt3r::scene::kDoubleSided,true,true,false,&opt,&body)==0);
    opt.cull=CullMode::None;
    CHECK(deko::native_packet_cull(0,true,true,false,&opt,&body)==0);
    opt.cull=CullMode::Front;
    CHECK(deko::native_packet_cull(0,true,true,false,&opt,&body)==1);
    CHECK(deko::native_packet_cull(0,true,true,true,&opt,&body)==0);
    opt.cull.reset(); opt.mirrored=false; body.single_sided=true;
    CHECK(deko::native_packet_cull(gt3r::scene::kDoubleSided,true,true,false,&opt,&body)==1);
}
TEST(deko_native_body_policy_leaves_wheels_shadows_helpers_and_legacy_probe_unchanged) {
    DrawOptions opt; DrawItem part;
    CHECK(deko::native_packet_cull(0,true,true,false,&opt,&part)==0);
    CHECK(deko::native_packet_cull(0,true,true,false,nullptr,nullptr)==0);
    part.sphere_map=true;
    CHECK(deko::native_packet_cull(0,true,true,false,&opt,&part)==1);
    part.sphere_map=false;
    CHECK(deko::native_packet_cull(0,true,false,false,&opt,&part)==1);
    part.car_surface=true;
    CHECK(deko::native_packet_cull(0,false,true,false,&opt,&part)==0);
}
TEST(deko_common_fragment_excludes_special_modes_cars_effects_and_non_native_paths) {
    DrawItem item;
    for(float mode:{0.f,1.f}) {
        CHECK(deko::common_fragment(mode,true,true,false,false,&item));
        for(auto pass:{ProfilePass::Other,ProfilePass::Environment,ProfilePass::WetRoad,ProfilePass::WetBlur,ProfilePass::Mirror,ProfilePass::Post})
            CHECK(!deko::common_fragment(mode,true,true,false,false,&item,pass));
        CHECK(!deko::common_fragment(mode,false,true,false,false,&item));
        CHECK(!deko::common_fragment(mode,true,false,false,false,&item));
        CHECK(!deko::common_fragment(mode,true,true,true,false,&item));
        CHECK(!deko::common_fragment(mode,true,true,false,true,&item));
        CHECK(!deko::common_fragment(mode,true,true,false,false,nullptr));
        item.profile_role=ProfileRole::Car; item.car_surface=true;
        CHECK(!deko::common_fragment(mode,true,true,false,false,&item));
        item.profile_role=ProfileRole::Effect;
        CHECK(!deko::common_fragment(mode,true,true,false,false,&item));
        item.profile_role=ProfileRole::Scene;
    }
    for(float mode:{-1.f,0.5f,1.5f,2.f,3.f,4.f,5.f,6.f,7.f,8.f,std::numeric_limits<float>::quiet_NaN()})
        CHECK(!deko::common_fragment(mode,true,true,false,false,&item));
}

TEST(deko_native_fog_excludes_background_helpers_and_unfogged_items) {
    DrawOptions opt; opt.fog=true; opt.fog_gt3=true; opt.fog_start=3;
    opt.fog_end=13; opt.fog_max=0.75f; opt.far_clip=100;
    opt.quantize_rgb5=true; opt.env_gt3_lookup=true;
    DrawItem item; item.alpha=0.5f; item.modelview_scale=0.01f;
    auto packed=deko::scene_fog(opt,&item,Mat4{},true);
    CHECK(packed[7]==1); CHECK(packed[8]==3); CHECK(packed[9]==0.25f);
    CHECK(packed[10]==1); CHECK(packed[11]==1); CHECK(packed[19]==0.5f);
    CHECK(packed[33]==0.1f); CHECK(packed[35]==10000);
    CHECK(packed[36]==1); CHECK(packed[37]==1); CHECK(packed[38]==1);
    item.no_fog=true; packed=deko::scene_fog(opt,&item,Mat4{},false);
    CHECK(packed[7]==0); CHECK(packed[35]==10000); // no_fog still clips at far plane
    item.no_fog=false; item.background=true;
    packed=deko::scene_fog(opt,&item,Mat4{},false);
    CHECK(packed[7]==0); CHECK(packed[35]==0);
    item.background=false; item.helper=true;
    packed=deko::scene_fog(opt,&item,Mat4{},false);
    CHECK(packed[7]==0); CHECK(packed[35]==0);
}
TEST(deko_native_linear_fog_bounds_and_disabled_default) {
    DrawOptions opt;
    auto packed=deko::scene_fog(opt,nullptr,Mat4{},false);
    CHECK(packed[7]==0); CHECK(packed[19]==1); CHECK(packed[35]==0);
    opt.fog=true; opt.fog_start=8; opt.fog_end=8; opt.fog_max=2;
    packed=deko::scene_fog(opt,nullptr,Mat4{},false);
    CHECK(std::isfinite(packed[33])); CHECK(packed[34]==1); CHECK(packed[38]==0);
}
