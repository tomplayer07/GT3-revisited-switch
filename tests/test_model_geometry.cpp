#include "test_framework.h"
#include "render/model_geometry.h"
#include "formats/model/car_model.h"
#include <limits>
using namespace gt3r;
using namespace gt3r::render;
namespace {
scene::Mesh triangle_mesh() {
    scene::Mesh mesh;
    mesh.vertices.resize(3);
    mesh.vertices[1].pos[0]=2;
    mesh.vertices[2].pos[1]=2;
    mesh.indices={0,1,2,2,1,0,0,2,1};
    scene::Primitive a; a.first_index=0; a.index_count=3;
    auto b=a; b.first_index=3; b.flags=scene::kAlphaBlend;
    auto c=a; c.first_index=6;
    mesh.primitives={a,b,c};
    return mesh;
}
}
TEST(model_geometry_preserves_order_for_native_material_packets) {
    const auto input=triangle_mesh();
    std::vector<scene::Vertex> vertices;
    std::vector<u32> indices;
    std::vector<ModelMeshInfo> meshes;
    assemble_model_geometry({input,input},0,false,vertices,indices,meshes,false);
    CHECK(vertices.size()==6); CHECK(indices.size()==18); CHECK(meshes.size()==2);
    CHECK(meshes[1].vertex_offset==3); CHECK(meshes[1].index_offset==9);
    for (const auto& mesh:meshes) {
        CHECK(mesh.primitives.size()==3); CHECK(mesh.primitive_spheres.size()==3);
        CHECK(mesh.primitives[0].flags==0);
        CHECK(mesh.primitives[1].flags==scene::kAlphaBlend);
        CHECK(mesh.primitives[2].flags==0);
        CHECK(mesh.primitives[2].first_index==6);
        CHECK(mesh.line_first==9); CHECK(mesh.line_count==0);
        CHECK(mesh.primitive_spheres[0].center.x==1);
        CHECK(mesh.primitive_spheres[0].center.y==1);
    }
}
TEST(model_geometry_retains_production_merging_and_local_wire_indices) {
    std::vector<scene::Vertex> vertices;
    std::vector<u32> indices;
    std::vector<ModelMeshInfo> meshes;
    assemble_model_geometry({triangle_mesh()},0,true,vertices,indices,meshes);
    CHECK(meshes[0].primitives.size()==2);
    CHECK(meshes[0].primitives[0].index_count==6);
    CHECK(meshes[0].primitives[1].flags==scene::kAlphaBlend);
    CHECK(meshes[0].primitive_spheres.size()==2);
    CHECK(meshes[0].line_first==9); CHECK(meshes[0].line_count==6);
    for(auto index:indices) CHECK(index<vertices.size());
}
TEST(model_geometry_drops_invalid_triangles_and_overflowed_ranges) {
    auto input=triangle_mesh();
    input.indices[4]=999;
    scene::Primitive overflow;
    overflow.first_index=std::numeric_limits<u32>::max(); overflow.index_count=6;
    input.primitives.push_back(overflow);
    std::vector<scene::Vertex> vertices;
    std::vector<u32> indices;
    std::vector<ModelMeshInfo> meshes;
    assemble_model_geometry({input},0,false,vertices,indices,meshes,false);
    CHECK(indices.size()==6);
    CHECK(meshes[0].primitives.size()==2);
    CHECK(meshes[0].primitive_spheres.size()==2);
    for(auto index:indices) CHECK(index<vertices.size());
}

TEST(model_geometry_native_packets_match_decoded_cars_disc) {
    auto* disc=test::test_disc();
    if(!disc) SKIP("needs GT3R_DISC");
    for (const char* id : {"am0006","ma0095"}) {
        auto entry=disc->volume().find(std::string("cars/day/")+id);
        CHECK(bool(entry));
        if(!entry) continue;
        auto data=disc->volume().read(*entry);
        auto model=load_car_model(data,{},id);
        std::vector<scene::Vertex> vertices;
        std::vector<u32> indices;
        std::vector<ModelMeshInfo> meshes;
        assemble_model_geometry(model.meshes,model.textures.size(),false,vertices,indices,meshes,false);
        CHECK(meshes.size()==model.meshes.size());
        for(size_t m=0;m<meshes.size();++m) {
            const auto& source=model.meshes[m]; const auto& result=meshes[m];
            size_t packet=0;
            for(const auto& primitive:source.primitives) {
                if(!primitive.index_count) continue;
                CHECK(packet<result.primitives.size());
                if(packet>=result.primitives.size()) break;
                const auto& prepared=result.primitives[packet++];
                CHECK(prepared.flags==primitive.flags);
                CHECK(prepared.texture==primitive.texture);
                CHECK(prepared.index_count==primitive.index_count);
                for(u32 i=0;i<prepared.index_count;++i)
                    CHECK(indices[result.index_offset+prepared.first_index+i]==source.indices[primitive.first_index+i]);
            }
            CHECK(packet==result.primitives.size());
        }
    }
}
