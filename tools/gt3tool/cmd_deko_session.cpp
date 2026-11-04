#include "tool.h"
#include "../switch/deko-session/session.h"
#include "../switch/deko-model/course_motion.h"
#include "game/data/car_database.h"
#include "formats/course/course_model.h"
#include "race/point_lights.h"
#include "race/sun_glare.h"
#include "race/lamp_glows.h"
#include "formats/model/car_model.h"
#include "race/occluder.h"
#include "race/camera_rig.h"
#include <map>
#include <cstdio>
#include <cstdlib>
namespace gt3r::tool {
namespace {
int export_session(Disc& disc,const Args& args) {
    std::map<std::string,Bytes> files;
    data::CarDatabase database;
    const std::string suffix=RegionData{disc.volume().region()}.db_suffix();
    if (!database.load([&](const std::string& path)->std::optional<Bytes> {
        const auto* entry=disc.volume().find(path); if (!entry) return std::nullopt;
        auto bytes=disc.volume().read(*entry); files[path]=bytes; return bytes;
    },suffix)) throw FormatError(database.error());
    files["probe/db_suffix"]=Bytes(suffix.begin(),suffix.end());
    const auto program=disc.volume().read_program(); if (!program) throw FormatError("No disc program");
    files["probe/program"]=*program;
    files["probe/course"]=disc.volume().read(need(disc.volume(),"courses/US/route5_dry"));
    files["probe/car"]=disc.volume().read(need(disc.volume(),"cars/day/ma0095"));
    if (const auto* flare=disc.volume().find("race/flare.bin")) files["probe/flare"]=disc.volume().read(*flare);
    else files["probe/flare"]={};
    Bytes output; auto word=[&](uint32_t value){for(int i=0;i<4;++i) output.push_back(uint8_t(value>>(i*8)));};
    word(0x53524447); word(1); word(uint32_t(files.size()));
    for (const auto& [name,bytes]:files) { word(uint32_t(name.size())); word(uint32_t(bytes.size())); output.insert(output.end(),name.begin(),name.end()); output.insert(output.end(),bytes.begin(),bytes.end()); }
    std::vector<gtm1::ClutSet> cluts;
    course::CourseModelOptions courseOptions; courseOptions.course_line=false; courseOptions.backdrop=true;
    const auto scene=course::build_course_model(files.at("probe/course"),courseOptions,"route5_dry",&cluts);
    if (const char* directory=std::getenv("GT3R_DEKO_INSPECT_TEXTURES")) {
        const render::Vec3 rayEye{424.06f,3.0f,2.46f};
        const auto rayForward=render::normalize(render::Vec3{-1,-0.08f,0});
        const auto rayRight=render::normalize(render::cross(rayForward,{0,1,0}));
        const auto rayUp=render::cross(rayRight,rayForward);
        for (float py : {80.0f,150.0f,230.0f}) for (float px : {220.0f,450.0f,665.0f,800.0f}) {
            const auto direction=render::normalize(rayForward+rayRight*((2*(px-51.2f)/1177.6f-1)*0.41421356f*1280/720)+rayUp*((1-2*(py-28.8f)/662.4f)*0.41421356f));
            float nearest=1e20f; const scene::Mesh* hitMesh=nullptr; const scene::Primitive* hitPrim=nullptr; float hitU=0,hitV=0;
            for (const auto& mesh:scene.meshes) for (const auto& p:mesh.primitives) {
                if (p.flags&(scene::kAdditive|scene::kAlphaOnly)) continue;
                const auto matrix=render::Mat4::from(mesh.transform);
                for (size_t i=p.first_index;i+2<size_t(p.first_index)+p.index_count;i+=3) {
                    const auto& a=mesh.vertices[mesh.indices[i]]; const auto& b=mesh.vertices[mesh.indices[i+1]];const auto& c=mesh.vertices[mesh.indices[i+2]];
                    const auto A=matrix.transform_point({a.pos[0],a.pos[1],a.pos[2]}),B=matrix.transform_point({b.pos[0],b.pos[1],b.pos[2]}),C=matrix.transform_point({c.pos[0],c.pos[1],c.pos[2]});
                    const auto e1=B-A,e2=C-A,h=render::cross(direction,e2); const float det=render::dot(e1,h); if (std::fabs(det)<1e-6f) continue;
                    const auto s=rayEye-A;const float u=render::dot(s,h)/det;if(u<0||u>1)continue;
                    const auto q=render::cross(s,e1);const float v=render::dot(direction,q)/det;if(v<0||u+v>1)continue;
                    const float distance=render::dot(e2,q)/det;if(distance<=0||distance>=nearest)continue;
                    nearest=distance;hitMesh=&mesh;hitPrim=&p;hitU=a.uv[0]*(1-u-v)+b.uv[0]*u+c.uv[0]*v;hitV=a.uv[1]*(1-u-v)+b.uv[1]*u+c.uv[1]*v;
                }
            }
            if(hitPrim)std::printf("Facade ray: pixel=%.0f/%.0f mesh=%s texture=%d flags=%x uv=%.3f/%.3f distance=%.1f\n",px,py,hitMesh->name.c_str(),hitPrim->texture,hitPrim->flags,hitU,hitV,nearest);
        }
        auto fullOptions=courseOptions; fullOptions.reflections=true;
        const auto full=course::build_course_model(files.at("probe/course"),fullOptions,"route5_full");
        for (const auto& mesh:full.meshes) if (mesh.name=="scenery/node496" || mesh.name=="scenery/node497")
            for (const auto& p:mesh.primitives) {
                float lo=1e20f,hi=-1e20f;
                for (size_t i=p.first_index;i<size_t(p.first_index)+p.index_count;++i) { const float y=mesh.vertices[mesh.indices[i]].pos[1]; lo=std::min(lo,y);hi=std::max(hi,y); }
                std::printf("Full building pass: mesh=%s texture=%d flags=%x env=%u y=%.2f..%.2f\n",mesh.name.c_str(),p.texture,p.flags,p.env_kind,lo,hi);
            }
        for (const auto& mesh:scene.meshes) for (const auto& p:mesh.primitives) {
            render::Vec3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
            size_t redVertices=0;
            const auto matrix=render::Mat4::from(mesh.transform);
            for (size_t i=p.first_index;i<size_t(p.first_index)+p.index_count;++i) {
                const auto& v=mesh.vertices[mesh.indices[i]]; const auto point=matrix.transform_point({v.pos[0],v.pos[1],v.pos[2]});
                redVertices+=(v.color&255)>60 && (v.color&255)>2*((v.color>>8)&255) && (v.color&255)>2*((v.color>>16)&255);
                lo.x=std::min(lo.x,point.x); lo.y=std::min(lo.y,point.y); lo.z=std::min(lo.z,point.z);
                hi.x=std::max(hi.x,point.x); hi.y=std::max(hi.y,point.y); hi.z=std::max(hi.z,point.z);
            }
            if (lo.y<1 && hi.y>-1 && hi.y<3 && lo.x<-180 && hi.x>-300 && lo.z<25 && hi.z>-25)
                std::printf("Grid road primitive: mesh=%s texture=%d flags=%x alpha=%.3f xyz=(%.1f %.1f %.1f)..(%.1f %.1f %.1f)\n",mesh.name.c_str(),p.texture,p.flags,p.alpha_ref,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
            if (redVertices>p.index_count/2 && hi.y>5)
                std::printf("Red vertex primitive: mesh=%s texture=%d flags=%x xyz=(%.1f %.1f %.1f)..(%.1f %.1f %.1f)\n",mesh.name.c_str(),p.texture,p.flags,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
        }
        for (auto& set:cluts) for (auto record:set.textures.records())
            std::printf("Animated texture record: id=%zu\n",size_t(set.first)+record);
        for (size_t t=0;t<scene.textures.size();++t) {
            const auto& texture=scene.textures[t]; size_t red=0;
            for (auto pixel:texture.rgba) red+=(pixel&255)>60 && (pixel&255)>2*((pixel>>8)&255) && (pixel&255)>2*((pixel>>16)&255);
            if (red<8 && t!=83 && t!=119 && t!=118 && t!=2 && t!=226 && t!=227) continue;
            const auto path=std::string(directory)+"/texture-"+std::to_string(t)+".ppm";
            FILE* out=std::fopen(path.c_str(),"wb"); if (!out) throw FormatError("Cannot write diagnostic texture");
            std::fprintf(out,"P6\n%u %u\n255\n",texture.width,texture.height);
            for (auto pixel:texture.rgba) for (unsigned channel=0;channel<3;++channel) std::fputc(int((pixel>>(8*channel))&255),out);
            std::fclose(out);
            std::printf("Red texture: id=%zu name=%s size=%ux%u red=%zu clamp=%d/%d\n",t,texture.name.c_str(),texture.width,texture.height,red,int(texture.clamp_u),int(texture.clamp_v));
            if (texture.width<=16 && texture.height<=16) {
                std::printf("Red solid pixel: id=%zu rgba=%08x\n",t,texture.rgba.front());
                for (const auto& mesh:scene.meshes) for (const auto& p:mesh.primitives) if (p.texture==int(t)) {
                    float lowY=1e20f,highY=-1e20f;
                    float minU=1e20f,maxU=-1e20f,minV=1e20f,maxV=-1e20f;
                    for (size_t i=p.first_index;i<size_t(p.first_index)+p.index_count;++i) {
                        const auto& v=mesh.vertices[mesh.indices[i]]; lowY=std::min(lowY,v.pos[1]); highY=std::max(highY,v.pos[1]);
                        minU=std::min(minU,v.uv[0]); maxU=std::max(maxU,v.uv[0]); minV=std::min(minV,v.uv[1]); maxV=std::max(maxV,v.uv[1]);
                    }
                    std::printf("Red solid use: texture=%zu mesh=%s flags=%x alpha=%.3f env=%u indices=%u y=%.2f..%.2f\n",t,mesh.name.c_str(),p.flags,p.alpha_ref,p.env_kind,p.index_count,lowY,highY);
                    std::printf("Red UV: texture=%zu mesh=%s u=%.6f..%.6f v=%.6f..%.6f\n",t,mesh.name.c_str(),minU,maxU,minV,maxV);
                }
            }
        }
    }
    // Opt-in: GT3R_DEKO_DUMP_TEXTURES="209,227" (directory from GT3R_DEKO_DUMP_DIR,
    // default /tmp). Dumps RGB+alpha PPMs and every primitive bound to each texture,
    // for matching texture IDs decoded from the Switch ID view to source materials.
    if (const char* list=std::getenv("GT3R_DEKO_DUMP_TEXTURES")) {
        const char* dumpDir=std::getenv("GT3R_DEKO_DUMP_DIR"); const std::string directory=dumpDir ? dumpDir : "/tmp";
        std::string token; std::vector<size_t> wanted;
        for (const auto& set:cluts) { std::printf("Dump clut set: first=%d records=",set.first); for (auto record:set.textures.records()) std::printf("%zu ",size_t(record)); std::printf("\n"); }
        for (const char* c=list;;++c) { if (*c>='0' && *c<='9') token+=*c; else { if (!token.empty()) wanted.push_back(std::stoul(token)); token.clear(); if (!*c) break; } }
        for (size_t t:wanted) {
            if (t>=scene.textures.size()) { std::printf("Dump texture: id=%zu out of range (%zu)\n",t,scene.textures.size()); continue; }
            const auto& texture=scene.textures[t];
            bool animated=false;
            for (auto& set:cluts) for (auto record:set.textures.records()) animated|=size_t(set.first)+record==t;
            unsigned alphaLo=255,alphaHi=0; double alphaSum=0;
            for (auto pixel:texture.rgba) { const unsigned a=pixel>>24; alphaLo=std::min(alphaLo,a); alphaHi=std::max(alphaHi,a); alphaSum+=a; }
            std::printf("Dump texture: id=%zu name=%s size=%ux%u clamp=%d/%d no_mips=%d animated=%d alpha=%u..%u mean=%.1f first_rgba=%08x\n",t,texture.name.c_str(),texture.width,texture.height,
                int(texture.clamp_u),int(texture.clamp_v),int(texture.no_mips),int(animated),alphaLo,alphaHi,texture.rgba.empty()?0.0:alphaSum/texture.rgba.size(),texture.rgba.empty()?0u:texture.rgba.front());
            for (int alphaPlane=0;alphaPlane<2;++alphaPlane) {
                const auto path=directory+"/dump-texture-"+std::to_string(t)+(alphaPlane?"-alpha":"")+".ppm";
                FILE* out=std::fopen(path.c_str(),"wb"); if (!out) throw FormatError("Cannot write diagnostic texture dump");
                std::fprintf(out,"P6\n%u %u\n255\n",texture.width,texture.height);
                for (auto pixel:texture.rgba) for (unsigned channel=0;channel<3;++channel) std::fputc(alphaPlane ? int(pixel>>24) : int((pixel>>(8*channel))&255),out);
                std::fclose(out);
            }
            for (const auto& mesh:scene.meshes) for (const auto& p:mesh.primitives) if (p.texture==int(t)) {
                float minU=1e20f,maxU=-1e20f,minV=1e20f,maxV=-1e20f; render::Vec3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
                const auto matrix=render::Mat4::from(mesh.transform);
                for (size_t i=p.first_index;i<size_t(p.first_index)+p.index_count;++i) {
                    const auto& v=mesh.vertices[mesh.indices[i]]; const auto point=matrix.transform_point({v.pos[0],v.pos[1],v.pos[2]});
                    minU=std::min(minU,v.uv[0]); maxU=std::max(maxU,v.uv[0]); minV=std::min(minV,v.uv[1]); maxV=std::max(maxV,v.uv[1]);
                    lo.x=std::min(lo.x,point.x); lo.y=std::min(lo.y,point.y); lo.z=std::min(lo.z,point.z);
                    hi.x=std::max(hi.x,point.x); hi.y=std::max(hi.y,point.y); hi.z=std::max(hi.z,point.z);
                }
                std::printf("Dump use: texture=%zu mesh=%s flags=%x alpha=%.3f env=%u tris=%u uv=%.3f..%.3f/%.3f..%.3f xyz=(%.1f %.1f %.1f)..(%.1f %.1f %.1f) first_color=%08x\n",t,mesh.name.c_str(),p.flags,p.alpha_ref,p.env_kind,p.index_count/3,
                    minU,maxU,minV,maxV,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z,p.index_count?mesh.vertices[mesh.indices[p.first_index]].color:0u);
            }
        }
    }
    size_t records=0; for (const auto& set:cluts) records+=set.textures.records().size();
    std::printf("Course animation smoke: %zu texture sets, %zu animated records, %zu course textures\n",cluts.size(),records,scene.textures.size());
    std::vector<gtm1::AnimatedNodes> moving;
    const auto staticScene=course::build_course_model(files.at("probe/course"),courseOptions,"route5_dry",nullptr,&moving);
    size_t nodes=0,parts=0,initialMeshes=0,changedLayouts=0,changedPrimitives=0;
    for (auto& animator:moving) {
        nodes+=animator.nodes().size(); parts+=animator.part_count();
        const auto initial=animator.meshes(); initialMeshes+=initial.size();
        for (int tick=0;tick<600;++tick) {
            animator.update(1.0f/60);
            const auto current=animator.meshes();
            bool changed=current.size()!=initial.size();
            if (!changed) for (size_t m=0;m<initial.size();++m)
                changed=changed || current[m].vertices.size()!=initial[m].vertices.size() || current[m].indices!=initial[m].indices || current[m].primitives.size()!=initial[m].primitives.size();
            changedLayouts+=changed;
            if (!changed) for (size_t m=0;m<initial.size();++m) for (size_t p=0;p<initial[m].primitives.size();++p) {
                const auto& a=initial[m].primitives[p]; const auto& b=current[m].primitives[p];
                changedPrimitives+=a.texture!=b.texture || a.flags!=b.flags || a.first_index!=b.first_index || a.index_count!=b.index_count || a.alpha_ref!=b.alpha_ref;
            }
        }
    }
    std::printf("Moving course smoke: groups=%zu nodes=%zu parts=%zu static_meshes=%zu initial_meshes=%zu changed_layout_samples=%zu changed_primitives=%zu\n",moving.size(),nodes,parts,staticScene.meshes.size(),initialMeshes,changedLayouts,changedPrimitives);
    deko_probe::CourseMotion motion(files.at("probe/course"));
    if (std::getenv("GT3R_DEKO_INSPECT_TEXTURES")) for (const auto& set:motion.cluts) for (auto record:set.textures.records())
        std::printf("Motion animated texture record: id=%zu\n",size_t(set.first)+record);
    course::CourseModelOptions skyOptions;
    skyOptions.scenery=skyOptions.backdrop=skyOptions.course_line=false; skyOptions.sky=true;
    const auto sky=course::build_course_model(files.at("probe/course"),skyOptions,"route5_sky");
    size_t skyTriangles=0; for (const auto& mesh:sky.meshes) skyTriangles+=mesh.indices.size()/3;
    std::printf("Course sky smoke: meshes=%zu textures=%zu triangles=%zu\n",sky.meshes.size(),sky.textures.size(),skyTriangles);
    if (sky.meshes.empty() || sky.textures.empty() || !skyTriangles) throw FormatError("Missing original Route 5 sky");
    const render::Vec3 skyEyeA{0,2,0},skyEyeB{150,-12,700},skyDirection{0,0,1};
    for (const auto& mesh:sky.meshes) {
        const auto transform=render::Mat4::from(mesh.transform);
        const auto a=render::look_at(skyEyeA,skyEyeA+skyDirection,{0,1,0})*render::translate(skyEyeA)*transform;
        const auto b=render::look_at(skyEyeB,skyEyeB+skyDirection,{0,1,0})*render::translate(skyEyeB)*transform;
        for (const auto& vertex:mesh.vertices) {
            const render::Vec3 point{vertex.pos[0],vertex.pos[1],vertex.pos[2]};
            if (render::length(a.transform_point(point)-b.transform_point(point))>0.01f) throw FormatError("Sky translation invariance failed");
        }
        for (const auto& primitive:mesh.primitives)
            if (primitive.flags&((1u<<1)|(1u<<4)|(1u<<9)|(1u<<10))) throw FormatError("Unsupported sky material pass");
    }
    if (motion.scene.textures.size()!=scene.textures.size()) throw FormatError("Motion texture count mismatch");
    for (size_t t=0;t<scene.textures.size();++t)
        if (motion.scene.textures[t].rgba!=scene.textures[t].rgba) throw FormatError("Motion texture identity mismatch");
    size_t emitted=0,changedVertices=0;
    for (int tick=0;tick<6000;++tick) {
        motion.advance(1.0f/60);
        motion.emit([&](size_t index,const scene::Mesh& mesh) {
            if (index>=motion.scene.meshes.size() || mesh.name!=motion.scene.meshes[index].name) throw FormatError("Moving mesh binding mismatch");
            const auto& initial=motion.scene.meshes[index];
            for (size_t v=0;v<mesh.vertices.size();++v) {
                const auto& a=initial.vertices[v]; const auto& b=mesh.vertices[v];
                bool changed=a.color!=b.color;
                for (int axis=0;axis<3;++axis) changed=changed || a.pos[axis]!=b.pos[axis] || a.normal[axis]!=b.normal[axis];
                for (int axis=0;axis<2;++axis) changed=changed || a.uv[axis]!=b.uv[axis];
                changedVertices+=changed;
            }
            ++emitted;
        });
    }
    motion.reset(); motion.emit([](size_t,const scene::Mesh&){});
    if (!changedVertices) throw FormatError("Course moving nodes did not animate");
    std::printf("Moving bridge smoke: meshes=%zu emitted=%zu changed_vertices=%zu simulated_seconds=100 texture_order=identical reset=ok\n",motion.scene.meshes.size(),emitted,changedVertices);
    deko_probe::Session session(output);
    const auto lamps=race::CarLampSet::parse(CarFile::parse(session.car_bytes()).info);
    std::printf("Car lamp data: head=%zu tail=%zu brake=%zu flare_bytes=%zu\n",lamps.head.size(),lamps.tail.size(),lamps.brake.size(),session.flare_bytes().size());
    if (lamps.head.empty() || lamps.brake.empty()) throw FormatError("Missing car lamp records");
    const auto start=session.state().position;
    race::CoursePointLights lights;
    const auto courseFile=course::CourseFile::parse(session.course_bytes());
    lights.load(courseFile.section(session.course_bytes(),28));
    const auto environment=race::SunParams::from_env0(courseFile.section(session.course_bytes(),32));
    race::CarLampGlows glows; glows.load(nullptr,nullptr,courseFile.section(session.course_bytes(),32),session.flare_bytes());
    if (!glows.active()) throw FormatError("Original car glow assets inactive");
    CarModelOptions bodyOptions; bodyOptions.wheels=false; bodyOptions.reflection=false;
    race::Occluder body; body.add_opaque_body(load_car_model(session.car_bytes(),bodyOptions,"lamp_smoke")); body.build(1);
    auto headOnly=lamps; headOnly.tail.clear(); headOnly.brake.clear();
    auto headTriangles=[&](render::Vec3 eye) {
        render::ScreenBatch batch; const SDL_FRect rect{0,0,1280,720}; batch.rect=rect;
        const std::vector<race::CarLampGlows::Car> cars{{&headOnly,{},0,false,&body}};
        glows.add(batch,rect,render::look_at(eye,{0,0.6f,0},{0,1,0}),eye,0.785398f,cars,
            [](const render::Vec3&,const render::Vec3&){return false;});
        return batch.idx.size()/3;
    };
    const auto front=headTriangles({0,1,-8}), rear=headTriangles({0,1,8});
    std::printf("Headlamp occlusion smoke: front=%zu rear=%zu triangles body_blocker=%zu\n",front,rear,body.triangles());
    if (!front || rear) throw FormatError("Headlamp front/rear occlusion smoke failed");
    race::LampGlows street; street.load(nullptr,nullptr,courseFile.section(session.course_bytes(),29),courseFile.section(session.course_bytes(),32),courseFile.section(session.course_bytes(),2),session.flare_bytes());
    street.set_visibility_interval(1);
    race::CameraRig camera; camera.set_mode(race::CameraMode::ChaseFar); camera.set_shake(false);
    camera.update(session.state(),session.setup(),{},1.0/60);
    const SDL_FRect screen{0,0,1280,720}; render::ScreenBatch visibleStreet,blockedStreet;
    visibleStreet.rect=blockedStreet.rect=screen;
    street.add(visibleStreet,screen,camera.view(),camera.eye(),camera.fovy(),[](const render::Vec3&,const render::Vec3&){return false;});
    street.add(blockedStreet,screen,camera.view(),camera.eye(),camera.fovy(),[](const render::Vec3&,const render::Vec3&){return true;});
    std::printf("Street lamp smoke: lamps=%zu visible=%zu blocked=%zu triangles\n",street.count(),visibleStreet.idx.size()/3,blockedStreet.idx.size()/3);
    if (!street.count() || visibleStreet.empty() || !blockedStreet.empty()) throw FormatError("Street glow visibility smoke failed");
    unsigned maximumLights=0;
    auto checkLights=[&]() {
        const auto p=session.state().position;
        const auto selected=lights.select(environment,{p.x,p.y,p.z},session.lamp_hint());
        if (selected.size()>4) throw FormatError("Too many selected car lights");
        for (const auto& light:selected) if (!std::isfinite(light.w) || light.w<0 || !std::isfinite(light.dir.x) || !std::isfinite(light.dir.y) || !std::isfinite(light.dir.z)) throw FormatError("Invalid selected light");
        maximumLights=std::max(maximumLights,unsigned(selected.size()));
    };
    for (int i=0;i<300;++i) { sim::ControlInputs input; input.throttle=0.5f; session.tick(input); checkLights(); }
    std::printf("Course lighting smoke: lamps=%zu active=%d env=%d max_selected=%u\n",lights.lamps().size(),int(lights.active()),int(environment.has_env),maximumLights);
    if (!lights.active() || !environment.has_env || !maximumLights) throw FormatError("Night course lights were not selected");
    const auto& state=session.state(); const auto distance=(state.position-start).length();
    std::printf("ExactRace smoke: ticks=%llu start=(%.2f %.2f %.2f) end=(%.2f %.2f %.2f) distance=%.2fm speed=%.2fkm/h\n",(unsigned long long)session.ticks(),start.x,start.y,start.z,state.position.x,state.position.y,state.position.z,distance,state.speed_kmh);
    if (distance<1 || state.speed_kmh<1) throw FormatError("Physics smoke did not move the car");
    const float beforeBraking=state.speed_kmh;
    for (int i=0;i<120;++i) { sim::ControlInputs input; input.brake=1; session.tick(input); }
    std::printf("ExactRace brake smoke: %.2f -> %.2f km/h after 120 ticks\n",beforeBraking,session.state().speed_kmh);
    if (session.state().speed_kmh>=beforeBraking-10) throw FormatError("Physics brake smoke did not slow the car");
    const float brakeFactor=session.brake_factor();
    for (int i=0;i<30;++i) session.tick({});
    std::printf("Brake-light history smoke: braking=%.2f released=%.2f\n",brakeFactor,session.brake_factor());
    if (brakeFactor<0.99f || session.brake_factor()!=0) throw FormatError("Brake light state did not activate/release");
    deko_probe::Session field(output,6);
    std::array<render::Vec3,6> starts;
    for (unsigned i=0;i<field.car_count();++i) { const auto p=field.state(i).position; starts[i]={p.x,p.y,p.z}; }
    for (int tick=0;tick<600;++tick) { sim::ControlInputs input; input.brake=1; field.tick(input); }
    for (unsigned i=1;i<field.car_count();++i) {
        const auto& car=field.state(i); const float moved=render::length(render::Vec3{car.position.x,car.position.y,car.position.z}-starts[i]);
        std::printf("Opponent smoke: car=%u moved=%.2fm speed=%.2fkm/h brake=%.2f\n",i,moved,car.speed_kmh,field.brake_factor(i));
        if (moved<10 || !std::isfinite(car.speed_kmh)) throw FormatError("Opponent AI failed to drive");
    }
    if (!save(args[0],output)) return 1;
    std::printf("Private physics bundle: %zu files, %zu bytes\n",files.size(),output.size()); return 0;
}
}
GT3TOOL_COMMAND(deko_session,"deko-session","<output.gtds>","build and smoke-test a private Route 5/Mazda exact-physics session",1,export_session);
}
