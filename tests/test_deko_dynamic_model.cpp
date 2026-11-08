#include "test_framework.h"
#include "render/deko/dynamic_model.h"
#include "render/model_geometry.h"
#include <limits>
using namespace gt3r;
using namespace gt3r::render;
using namespace gt3r::render::deko;
namespace {
std::vector<scene::Mesh> triangle() {
    scene::Mesh mesh; mesh.vertices.resize(3); mesh.indices={0,1,2};
    scene::Primitive primitive; primitive.index_count=3; primitive.texture=0; mesh.primitives.push_back(primitive);
    return {mesh};
}
scene::Texture tiny() { scene::Texture texture; texture.width=2; texture.height=2; texture.rgba={1,2,3,4}; return texture; }
}
TEST(deko_dynamic_texture_batches_copy_pixels_and_pad_tiny_images) {
    TextureUpdates state({tiny()});
    CHECK(state.metadata()[0].rgba.empty());
    CHECK(!state.revisions()[0].pixels);
    std::vector<u32> pixels={10,20,30,40};
    ModelTextureUpdate update{0,&pixels};
    CHECK(state.apply({&update,1}));
    const auto snapshot=state.revisions()[0];
    CHECK(snapshot.version==1); CHECK(snapshot.pixels->size()==32);
    CHECK((*snapshot.pixels)[0]==10); CHECK((*snapshot.pixels)[15]==20);
    CHECK((*snapshot.pixels)[16]==30); CHECK((*snapshot.pixels)[31]==40);
    pixels[0]=99; CHECK((*snapshot.pixels)[0]==10);
    CHECK(state.apply({&update,1}));
    CHECK(state.revisions()[0].version==2); CHECK((*state.revisions()[0].pixels)[0]==99);
    CHECK((*snapshot.pixels)[0]==10); // old consumers retain an immutable revision
}
TEST(deko_dynamic_texture_bad_batch_preserves_every_previous_revision) {
    TextureUpdates state({tiny(),tiny()});
    std::vector<u32> pixels={1,2,3,4};
    ModelTextureUpdate original{0,&pixels}; CHECK(state.apply({&original,1}));
    const auto before=state.revisions()[0];
    pixels[0]=55;
    ModelTextureUpdate bad[]={{0,&pixels},{2,&pixels}};
    CHECK(!state.apply(bad));
    CHECK(state.revisions()[0].version==before.version);
    CHECK(state.revisions()[0].pixels==before.pixels);
    CHECK(!state.revisions()[1].pixels);
    ModelTextureUpdate missing{1,nullptr}; CHECK(!state.apply({&missing,1}));
    std::vector<u32> wrong(3); ModelTextureUpdate resized{1,&wrong}; CHECK(!state.apply({&resized,1}));
    CHECK(state.apply({})); CHECK(state.revisions()[0].version==before.version);
}
TEST(deko_dynamic_layout_reuses_vertices_but_rebuilds_changed_materials_or_indices) {
    const auto base=triangle(); auto changed=base;
    changed[0].vertices[1].pos[0]=5; changed[0].transform[12]=2;
    CHECK(same_model_geometry_layout(base,changed));
    changed[0].indices={0,2,1}; CHECK(!same_model_geometry_layout(base,changed));
    changed=base; changed[0].lod=1; CHECK(!same_model_geometry_layout(base,changed));
    changed=base; changed[0].primitives[0].flags=scene::kAlphaBlend; CHECK(!same_model_geometry_layout(base,changed));
    changed=base; changed[0].primitives[0].material.valid=true; CHECK(!same_model_geometry_layout(base,changed));
    changed=base; changed[0].primitives[0].env_kind=2; CHECK(!same_model_geometry_layout(base,changed));
    changed=base; changed[0].vertices.resize(4); CHECK(!same_model_geometry_layout(base,changed));
}
TEST(deko_dynamic_geometry_validation_accepts_empty_and_rejects_bad_batches) {
    CHECK(valid_dynamic_geometry({},1));
    auto meshes=triangle(); CHECK(valid_dynamic_geometry(meshes,1));
    auto bad=meshes; bad[0].vertices[2].pos[0]=std::numeric_limits<float>::quiet_NaN();
    meshes.push_back(bad[0]); CHECK(!valid_dynamic_geometry(meshes,1));
    bad=triangle(); bad[0].indices[2]=99; CHECK(!valid_dynamic_geometry(bad,1));
    bad=triangle(); bad[0].primitives[0].index_count=2; CHECK(!valid_dynamic_geometry(bad,1));
    bad=triangle(); bad[0].primitives[0].texture=1; CHECK(!valid_dynamic_geometry(bad,1));
    bad=triangle(); bad[0].primitives[0].material.scale=std::numeric_limits<float>::infinity(); CHECK(!valid_dynamic_geometry(bad,1));
    bad=triangle(); bad[0].transform[3]=std::numeric_limits<float>::quiet_NaN(); CHECK(!valid_dynamic_geometry(bad,1));
}
TEST(deko_texture_family_serial_changes_only_after_valid_nonempty_batch) {
    TextureUpdates state({tiny()}); CHECK(state.serial()==0);
    CHECK(state.apply({})); CHECK(state.serial()==0);
    std::vector<u32> pixels={1,2,3,4}; ModelTextureUpdate good{0,&pixels};
    CHECK(state.apply({&good,1})); CHECK(state.serial()==1);
    ModelTextureUpdate bad{9,&pixels}; CHECK(!state.apply({&bad,1})); CHECK(state.serial()==1);
    CHECK(state.apply({&good,1})); CHECK(state.serial()==2);
}
TEST(deko_animation_pixels_recycle_exclusive_storage_but_preserve_retained_snapshots) {
    TextureUpdates state({tiny()});
    std::vector<u32> pixels={1,2,3,4}; ModelTextureUpdate update{0,&pixels};
    CHECK(state.apply({&update,1}));
    const auto* storage=state.revisions()[0].pixels.get();
    pixels[0]=9;
    CHECK(state.apply({&update,1}));
    CHECK(state.revisions()[0].pixels.get()==storage);
    CHECK(state.allocations()==1); CHECK(state.reuses()==1);
    const auto retained=state.revisions()[0].pixels;
    pixels[0]=10;
    CHECK(state.apply({&update,1}));
    CHECK(state.revisions()[0].pixels!=retained);
    CHECK((*retained)[0]==9); CHECK((*state.revisions()[0].pixels)[0]==10);
    CHECK(state.allocations()==2); CHECK(state.reuses()==1);
}
TEST(deko_animation_recycled_batches_keep_last_duplicate_and_validate_before_mutating) {
    TextureUpdates state({tiny()});
    std::vector<u32> first={1,2,3,4},last={8,7,6,5};
    ModelTextureUpdate initial{0,&first}; CHECK(state.apply({&initial,1}));
    ModelTextureUpdate bad[]={{0,&last},{1,&first}};
    CHECK(!state.apply(bad)); CHECK((*state.revisions()[0].pixels)[0]==1);
    CHECK(state.allocations()==1); CHECK(state.reuses()==0);
    ModelTextureUpdate duplicates[]={{0,&first},{0,&last}};
    CHECK(state.apply(duplicates));
    CHECK((*state.revisions()[0].pixels)[0]==8);
    CHECK((*state.revisions()[0].pixels)[31]==5);
    CHECK(state.serial()==2);
}
TEST(deko_animation_borrowed_revision_inputs_can_swap_without_being_overwritten) {
    scene::Texture tall; tall.width=2; tall.height=16;
    TextureUpdates state({tall,tall});
    std::vector<u32> a(32,11),b(32,22);
    ModelTextureUpdate initial[]={{0,&a},{1,&b}}; CHECK(state.apply(initial));
    // Borrowed raw input pointers do not increment the revision use counts.
    ModelTextureUpdate swap[]={{0,state.revisions()[1].pixels.get()},{1,state.revisions()[0].pixels.get()}};
    CHECK(state.apply(swap));
    CHECK(*state.revisions()[0].pixels==b); CHECK(*state.revisions()[1].pixels==a);
    CHECK(state.reuses()==0); CHECK(state.allocations()==4);
    a.assign(32,33); ModelTextureUpdate next{0,&a}; CHECK(state.apply({&next,1}));
    CHECK(*state.revisions()[0].pixels==a); CHECK(state.reuses()==1);
}
