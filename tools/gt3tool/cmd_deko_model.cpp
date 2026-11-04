// Export scene::Model into a small explicit, little-endian Deko bring-up packet.
// Runtime GPU code consumes neutral geometry; PS2 decoding stays in the engine.
#include "tool.h"
#include "formats/model/car_model.h"
#include "formats/course/course_model.h"
#include "render/math3d.h"
#include <algorithm>
#include <cmath>
#include "game/data/car_database.h"
#include "formats/model/wheel_spec.h"
#include <bit>
#include <cstdio>
#include <limits>

namespace gt3r::tool {
namespace {
int export_deko_model(Disc& disc, const Args& a, bool includeCar, bool fullCourse=false) {
    const auto& entry = need(disc.volume(), a[0]);
    auto bytes = disc.volume().read(entry);
    const auto slash = a[0].find_last_of('/');
    const std::string name = slash == std::string::npos ? a[0] : a[0].substr(slash + 1);
    scene::Model model;
    std::optional<WheelSpec> exportWheelSpec;
    size_t firstCarMesh=0, firstBackdropMesh=std::numeric_limits<size_t>::max(), firstBrakeMesh=std::numeric_limits<size_t>::max();
    bool courseCamera=false;
    float camera[4]{};
    if (CarFile::is_car(bytes)) {
        CarModelOptions options;
        options.lod = 0;
        options.reflection = true;
        data::CarDatabase db;
        if (db.load(disc.volume())) options.wheel_spec = wheel_spec_from_database(db, name);
        exportWheelSpec=options.wheel_spec;
        model = load_car_model(bytes, options, name);
    } else if (course::CourseFile::looks_like_course(bytes)) {
        const auto course = course::load_course(bytes);
        if (course.road.grid.empty()) throw FormatError("Deko course export requires a starting grid");
        course::CourseModelOptions options;
        options.backdrop = false; options.course_line = false;
        model = course::build_course_model(bytes, options, name);
        const auto anchor = course.road.grid.front().pos;
        courseCamera=true;
        camera[0]=anchor.x; camera[1]=anchor.y+2; camera[2]=anchor.z;
        camera[3]=course.road.grid.front().yaw+3.14159265f;
        // Conservative mesh-AABB selection around the first grid slot. Keep whole
        // nodes so the bring-up does not cut triangles or alter material ranges.
        if (!fullCourse) model.meshes.erase(std::remove_if(model.meshes.begin(),model.meshes.end(),[&](const auto& mesh) {
            if (mesh.vertices.empty()) return true;
            const auto matrix = render::Mat4::from(mesh.transform);
            float minX=INFINITY,maxX=-INFINITY,minZ=INFINITY,maxZ=-INFINITY;
            for (const auto& v : mesh.vertices) {
                const auto p=matrix.transform_point({v.pos[0],v.pos[1],v.pos[2]});
                minX=std::min(minX,p.x); maxX=std::max(maxX,p.x);
                minZ=std::min(minZ,p.z); maxZ=std::max(maxZ,p.z);
            }
            const float dx=anchor.x-std::clamp(anchor.x,minX,maxX);
            const float dz=anchor.z-std::clamp(anchor.z,minZ,maxZ);
            return dx*dx+dz*dz > 200.0f*200.0f;
        }),model.meshes.end());
        if (fullCourse) {
            firstBackdropMesh=model.meshes.size();
            course::CourseModelOptions background;
            background.scenery=false; background.course_line=false; background.backdrop=true;
            course::append_model(model,course::build_course_model(bytes,background,name+"/backdrop"));
        } else {
        std::vector<int> remap(model.textures.size(),-1);
        std::vector<scene::Texture> used;
        for (auto& mesh : model.meshes) for (auto& primitive : mesh.primitives) {
            if (primitive.texture < 0) continue;
            auto& mapped=remap.at(size_t(primitive.texture));
            if (mapped < 0) { mapped=int(used.size()); used.push_back(std::move(model.textures[size_t(primitive.texture)])); }
            primitive.texture=mapped;
        }
        model.textures=std::move(used);
        }
        std::printf(fullCourse ? "Full course around grid (%.2f %.2f %.2f)\n" : "Course sector: 200 m mesh selection around grid (%.2f %.2f %.2f)\n", anchor.x,anchor.y,anchor.z);
    } else throw FormatError("deko-model requires a GT3 car or course file");
    firstCarMesh=model.meshes.size();
    if (includeCar) {
        if (!courseCamera) throw FormatError("deko-scene requires a course file");
        const auto carBytes=disc.volume().read(need(disc.volume(),"cars/day/ma0095"));
        CarModelOptions options; options.lod=0; options.reflection=true;
        data::CarDatabase db;
        if (db.load(disc.volume())) options.wheel_spec=wheel_spec_from_database(db,"ma0095");
        exportWheelSpec=options.wheel_spec;
        auto car=load_car_model(carBytes,options,"ma0095");
        const int textureBase=int(model.textures.size());
        for (auto& texture : car.textures) model.textures.push_back(std::move(texture));
        // Diagnostic placement ahead of the grid camera; no physics or collisions.
        const render::Vec3 position{camera[0]+std::sin(camera[3])*12,camera[1]-2,camera[2]+std::cos(camera[3])*12};
        const auto placement=render::translate(position)*render::rotate_y(camera[3]);
        for (auto& mesh : car.meshes) {
            mesh.transform=(placement*render::Mat4::from(mesh.transform)).m;
            for (auto& primitive : mesh.primitives) if (primitive.texture>=0) primitive.texture+=textureBase;
            model.meshes.push_back(std::move(mesh));
        }
        firstBrakeMesh=model.meshes.size();
        options.brake=true;
        auto brakeCar=load_car_model(carBytes,options,"ma0095");
        const int brakeTextureBase=int(model.textures.size());
        for (auto& texture:brakeCar.textures) model.textures.push_back(std::move(texture));
        for (auto& mesh:brakeCar.meshes) {
            mesh.transform=(placement*render::Mat4::from(mesh.transform)).m;
            for (auto& primitive:mesh.primitives) if (primitive.texture>=0) primitive.texture+=brakeTextureBase;
            model.meshes.push_back(std::move(mesh));
        }
        std::printf("Added Mazda 787B base/brake node variants 12 m ahead of grid camera\n");
    }

    size_t nv = 0, ni = 0, np = 0;
    for (const auto& mesh : model.meshes) {
        nv += mesh.vertices.size(); ni += mesh.indices.size(); np += mesh.primitives.size();
    }
    auto checked = [](size_t n) -> u32 {
        if (n > std::numeric_limits<u32>::max()) throw FormatError("Deko packet count exceeds 32 bits");
        return u32(n);
    };
    Bytes out;
    auto word = [&](u32 v) { for (int i = 0; i < 4; ++i) out.push_back(u8(v >> (8 * i))); };
    auto real = [&](float v) { word(std::bit_cast<u32>(v)); };
    word(0x4B445447); // bytes GTDK
    word(9);
    word(checked(model.meshes.size())); word(checked(nv)); word(checked(ni));
    word(checked(np)); word(checked(model.textures.size()));
    word(courseCamera ? 1 : 0);
    if (courseCamera) for (float v : camera) real(v);
    size_t meshNumber=0;
    // Each mesh retains its transform; vertex/index/primitive ranges are local.
    for (const auto& mesh : model.meshes) {
        for (float v : mesh.transform) real(v);
        word(meshNumber>=firstBrakeMesh ? 3 : meshNumber>=firstCarMesh ? 1 : meshNumber>=firstBackdropMesh ? 2 : 0);
        int wheel=-1;
        if (meshNumber>=firstCarMesh) {
            const char* names[]={"wheel_fl","wheel_fr","wheel_rl","wheel_rr"};
            for (int i=0;i<4;++i) if (mesh.name.starts_with(names[i])) wheel=i;
        }
        word(uint32_t(wheel));
        const bool spin=mesh.name.find("_band")==std::string::npos && mesh.name.find("_brake")==std::string::npos;
        word(spin ? 1 : 0);
        float center[3]{};
        if (wheel>=0) {
            if (!exportWheelSpec) throw FormatError("Wheel animation export needs database wheel placement");
            const auto& spec=*exportWheelSpec; const int axle=wheel>>1;
            center[0]=(wheel&1 ? 0.5f : -0.5f)*spec.track[axle]; center[1]=spec.radius[axle];
            center[2]=(axle ? 0.5f : -0.5f)*spec.wheelbase;
        }
        for (float value:center) real(value);
        ++meshNumber;
        word(checked(mesh.vertices.size())); word(checked(mesh.indices.size())); word(checked(mesh.primitives.size()));
        for (const auto& v : mesh.vertices) {
            for (float x : v.pos) real(x);
            for (float x : v.normal) real(x);
            for (float x : v.uv) real(x);
            word(v.color);
        }
        for (u32 index : mesh.indices) {
            if (index >= mesh.vertices.size()) throw FormatError("Deko export: invalid mesh index");
            word(index);
        }
        for (const auto& p : mesh.primitives) {
            if (u64(p.first_index) + p.index_count > mesh.indices.size()) throw FormatError("Deko export: invalid primitive range");
            word(p.first_index); word(p.index_count); word(u32(p.texture)); word(p.flags); real(p.alpha_ref);
            for (float v : p.material.ambient) real(v);
            real(p.material.valid && !p.material.specular_local ? 1 : 0);
            for (float v : p.material.diffuse) real(v);
            real(p.material.scale);
            for (float v : p.material.specular) real(v);
            real(p.material.shininess);
            for (float v : p.material.emission) real(v);
            real(0); // reserved; brake-lamp emission is dynamic and not exported yet
            for (float v : p.env_c0) real(v);
            real(p.env_rows ? 1 : 0);
            for (float v : p.env_c1) real(v);
            real(0);
            for (float v : p.material.lamp_emission) real(v);
        }
    }
    for (const auto& t : model.textures) {
        if (u64(t.width) * t.height != t.rgba.size()) throw FormatError("Deko export: invalid texture extent");
        word(t.width); word(t.height);
        word((t.clamp_u ? 1u : 0u) | (t.clamp_v ? 2u : 0u) | (t.no_mips ? 4u : 0u));
        for (u32 pixel : t.rgba) word(pixel);
    }
    if (!save(a[1], out)) return 1;
    std::printf("Deko packet %s: %zu meshes, %zu vertices, %zu triangles, %zu textures, %zu bytes\n",
                name.c_str(), model.meshes.size(), nv, ni / 3, model.textures.size(), out.size());
    return 0;
}
}
int cmd_deko_model(Disc& disc,const Args& a) { return export_deko_model(disc,a,false); }
int cmd_deko_scene(Disc& disc,const Args& a) { return export_deko_model(disc,a,true); }
int cmd_deko_track(Disc& d,const Args& a) { return export_deko_model(d,a,true,true); }
GT3TOOL_COMMAND(deko_track,"deko-track","<course-vol-path> <output.gtdk>","export a complete static course with a Mazda for Deko driving",2,cmd_deko_track);
GT3TOOL_COMMAND(deko_scene, "deko-scene", "<course-vol-path> <output.gtdk>",
                "export a diagnostic course sector with a static Mazda 787B", 2, cmd_deko_scene);
GT3TOOL_COMMAND(deko_model, "deko-model", "<car-or-course-vol-path> <output.gtdk>",
                "export a car or starting-grid course sector for the native Deko prototype", 2, cmd_deko_model);
}
