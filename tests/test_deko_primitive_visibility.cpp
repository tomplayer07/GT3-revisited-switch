#include "test_framework.h"
#include "render/model_geometry.h"
#include "../tools/switch/deko-model/visibility.h"
using namespace gt3r;
using namespace gt3r::render;
using namespace gt3r::deko_probe;
TEST(deko_static_primitive_bounds_cull_outside_packets_inside_a_visible_large_mesh) {
    scene::Mesh mesh;
    for(float x:{0.f,40.f}) {
        const uint32_t start=uint32_t(mesh.vertices.size());
        for(const auto point:{Vec3{x-0.5f,0,-10},Vec3{x+0.5f,0,-10},Vec3{x,1,-10}}) {
            scene::Vertex v; v.pos[0]=point.x; v.pos[1]=point.y; v.pos[2]=point.z; mesh.vertices.push_back(v);
        }
        scene::Primitive p; p.first_index=uint32_t(mesh.indices.size()); p.index_count=3;
        mesh.indices.insert(mesh.indices.end(),{start,start+1,start+2}); mesh.primitives.push_back(p);
    }
    std::vector<scene::Vertex> vertices; std::vector<uint32_t> indices; std::vector<ModelMeshInfo> layout;
    assemble_model_geometry({mesh},0,false,vertices,indices,layout,false);
    CHECK(layout.size()==1); if(layout.empty()) return;
    CHECK(layout[0].primitive_spheres.size()==2); if(layout[0].primitive_spheres.size()!=2) return;
    ViewCuller culler(perspective(1,1,0.1f,100),{},0);
    const auto box=[](const ModelMeshInfo::Sphere& s) { return MeshBounds{s.center,{s.radius,s.radius,s.radius}}; };
    CHECK(culler.visible({{20,0.5f,-10},{20.5f,0.5f,0}},Mat4{}));
    CHECK(culler.visible(box(layout[0].primitive_spheres[0]),Mat4{}));
    CHECK(!culler.visible(box(layout[0].primitive_spheres[1]),Mat4{}));
}
TEST(deko_primitive_visibility_keeps_frustum_crossings_and_transformed_bounds) {
    const auto projection=perspective(1,1,0.1f,100);
    for(const auto& vp:{projection,scale({-1,1,1})*projection}) {
        ViewCuller culler(vp,{},0);
        CHECK(culler.visible({{6,0,-10},{1,1,1}},Mat4{}));
        CHECK(!culler.visible({{8,0,-10},{0.1f,0.1f,0.1f}},Mat4{}));
        CHECK(culler.visible({{0,0,-0.05f},{0.2f,0.2f,0.2f}},Mat4{}));
        CHECK(!culler.visible({{0,0,1},{0.1f,0.1f,0.1f}},Mat4{}));
        CHECK(culler.visible({{0,0,-100},{0.2f,0.2f,0.2f}},Mat4{}));
        CHECK(!culler.visible({{0,0,-102},{0.2f,0.2f,0.2f}},Mat4{}));
        CHECK(culler.visible({{0,0,0},{1,1,1}},translate({0,0,-10})*rotate_y(0.7f)*scale({-2,0.5f,3})));
    }
}
