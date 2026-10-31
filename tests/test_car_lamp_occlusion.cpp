#include "test_framework.h"
#include "race/occluder.h"

using namespace gt3r;

TEST(car_lamp_opaque_panel_blocks_rear_view_but_not_front_view) {
    scene::Model body;
    scene::Mesh panel;
    panel.vertices.resize(3);
    panel.vertices[0].pos[0] = -2; panel.vertices[0].pos[1] = -2;
    panel.vertices[1].pos[0] = 2; panel.vertices[1].pos[1] = -2;
    panel.vertices[2].pos[1] = 2;
    panel.indices = {0, 1, 2};
    scene::Primitive p; p.index_count = 3;
    panel.primitives.push_back(p); body.meshes.push_back(panel);
    race::Occluder solid; solid.add_opaque_body(body); solid.build(0.5f);
    CHECK(solid.segment_blocked({0, 0, -4}, {0, 0, 1}));
    CHECK(!solid.segment_blocked({0, 0, 4}, {0, 0, 1}));
    body.meshes[0].primitives[0].flags = scene::kAlphaBlend;
    race::Occluder glass; glass.add_opaque_body(body); glass.build(0.5f);
    CHECK(glass.empty());
    body.meshes[0].primitives[0].flags = scene::kAlphaTest;
    body.meshes[0].primitives[0].texture = 0;
    scene::Texture texture; texture.rgba = {0xFFFFFFFF, 0x00FFFFFF};
    body.textures.push_back(texture);
    race::Occluder holes; holes.add_opaque_body(body); holes.build(0.5f);
    CHECK(holes.empty());
    body.textures[0].rgba[1] = 0xFFFFFFFF;
    race::Occluder opaque_texture; opaque_texture.add_opaque_body(body); opaque_texture.build(0.5f);
    CHECK(!opaque_texture.empty());
}
