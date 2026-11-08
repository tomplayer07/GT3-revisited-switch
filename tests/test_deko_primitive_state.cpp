#include "test_framework.h"
#include "render/deko/primitive_state.h"
using namespace gt3r;
using namespace gt3r::render::deko;
TEST(deko_prize_sphere_map_uses_primitive_texture_without_car_reflection) {
    auto state=primitive_state(scene::kEnvironmentMap|scene::kAdditive|scene::kUnlit,true,true,true,false,true);
    CHECK(state.sphere); CHECK(!state.reflection); CHECK(!state.skip);
    CHECK(state.blend==NativeBlend::Additive); CHECK(state.transparent);
    state=primitive_state(scene::kEnvironmentMap,true,true,true,false,true);
    CHECK(state.sphere); CHECK(!state.skip); CHECK(state.blend==NativeBlend::Opaque);
    CHECK(state.depth_write);
}
TEST(deko_prize_bowl_multiply_pass_and_shadow_keep_material_depth_policy) {
    auto bowl=primitive_state(scene::kMultiplyDest,true,true,true,false,true);
    CHECK(!bowl.skip); CHECK(bowl.blend==NativeBlend::MultiplyDest); CHECK(bowl.transparent);
    auto shadow=primitive_state(scene::kAlphaBlend|scene::kNoDepthWrite,false,false,true,false,true);
    CHECK(shadow.blend==NativeBlend::Alpha); CHECK(!shadow.depth_write);
    auto depth=primitive_state(scene::kDepthOnly,false,true,true,false,true);
    CHECK(depth.depth_only); CHECK(depth.depth_write); CHECK(depth.transparent);
    CHECK(depth.blend==NativeBlend::Opaque);
}
TEST(deko_car_reflection_and_legacy_course_policy_are_preserved) {
    const auto flags=scene::kEnvironmentMap|scene::kAdditive|scene::kUnlit;
    for(bool frontend:{false,true}) {
        auto car=primitive_state(flags,true,false,frontend,false,true);
        CHECK(car.reflection); CHECK(!car.sphere); CHECK(!car.depth_write); CHECK(!car.skip);
        auto decal=primitive_state(scene::kAlphaBlend,true,false,frontend,false,true);
        CHECK(!decal.depth_write);
    }
    CHECK(primitive_state(scene::kMultiplyDest,true,false,false,false,false).skip);
    CHECK(primitive_state(scene::kAlphaOnly,false,true,true,false,false).skip);
    CHECK(!primitive_state(0,false,false,false,false,false).transparent);
}
TEST(deko_course_alpha_masks_and_wet_road_composite_use_destination_alpha) {
    auto mask=primitive_state(scene::kAlphaOnly,false,true,true,false,true,true);
    CHECK(!mask.skip); CHECK(mask.alpha_only); CHECK(!mask.keep_alpha);
    auto sheen=primitive_state(scene::kAdditive|scene::kDestAlphaAdd|scene::kKeepAlpha,false,true,true,false,true,true);
    CHECK(!sheen.skip); CHECK(sheen.blend==NativeBlend::DestAlphaAdd); CHECK(sheen.keep_alpha);
    auto lerp=primitive_state(scene::kAlphaBlend,true,false,true,false,true,false,1);
    CHECK(lerp.blend==NativeBlend::DestAlphaLerp); CHECK(lerp.keep_alpha);
    auto add=primitive_state(scene::kAlphaBlend,true,false,true,false,true,false,2);
    CHECK(add.blend==NativeBlend::DestAlphaAdd); CHECK(add.keep_alpha);
    CHECK(primitive_state(scene::kAlphaOnly,false,true,true,false,true).skip);
}
