#include "app/app.h"
#include "app/screen_registry.h"
#include "app/tool_buttons.h"
#include "base/log.h"
#include "render/menu_car_load.h"
#include "render/view3d.h"
#include "render/test_models.h"
#include <cmath>

namespace gt3r {
namespace {
scene::Mesh quad(std::array<render::Vec3,4> points,const char* name) {
    scene::Mesh mesh; mesh.name=name;
    for(size_t i=0;i<4;++i) {
        scene::Vertex vertex;
        vertex.pos[0]=points[i].x; vertex.pos[1]=points[i].y; vertex.pos[2]=points[i].z;
        vertex.normal[2]=1; vertex.uv[0]=float(i%2); vertex.uv[1]=1-float(i/2);
        mesh.vertices.push_back(vertex);
    }
    mesh.indices={0,1,2,2,1,3};
    scene::Primitive p; p.index_count=6; p.flags=scene::kUnlit|scene::kDoubleSided; mesh.primitives.push_back(p);
    return mesh;
}
scene::Model room_model() {
    scene::Model model;
    constexpr float a=4;
    model.meshes.push_back(quad({render::Vec3{a,-a,-a},{a,-a,a},{a,a,-a},{a,a,a}},"+X"));
    model.meshes.push_back(quad({render::Vec3{-a,-a,a},{-a,-a,-a},{-a,a,a},{-a,a,-a}},"-X"));
    model.meshes.push_back(quad({render::Vec3{-a,a,-a},{a,a,-a},{-a,a,a},{a,a,a}},"+Y"));
    model.meshes.push_back(quad({render::Vec3{-a,-a,a},{a,-a,a},{-a,-a,-a},{a,-a,-a}},"-Y"));
    model.meshes.push_back(quad({render::Vec3{-a,-a,a},{a,-a,a},{-a,a,a},{a,a,a}},"+Z"));
    model.meshes.push_back(quad({render::Vec3{a,-a,-a},{-a,-a,-a},{a,a,-a},{-a,a,-a}},"-Z"));
    return model;
}
class ReflectionViewer final:public Screen {
    std::unique_ptr<render::GpuModel> room_,panel_,probe_,car_;
    std::unique_ptr<render::View3D> capture_,camera_,main_;
    std::shared_ptr<render::NativeCubeTexture> cube_;
    std::shared_ptr<render::NativeTexture> feed_;
    render::MenuCarLoader loader_;
    scene::Model room_scene_;
    bool initialized_=false,paused_=false,reflections_=true,freeze_monitor_=false;
    int scene_mode_=0;
    float time_=0;
    uint64_t frames_=0;
    std::string error_;
    void initialize(App& app) {
        initialized_=true;
        if(!app.gpu() || !app.gpu()->native_renderer()) { error_="Questa prova richiede il renderer nativo"; return; }
        room_scene_=room_model();
        room_=std::make_unique<render::GpuModel>(*app.gpu(),room_scene_);
        scene::Model panel; panel.meshes.push_back(quad({render::Vec3{-1,0,0},{1,0,0},{-1,1.2f,0},{1,1.2f,0}},"monitor"));
        panel_=std::make_unique<render::GpuModel>(*app.gpu(),panel);
        scene::Model probe;
        scene::Mesh reflection; reflection.name="reflection probe";
        render::append_sphere(reflection,0,0.9f,0,0.8f,32,16,0xffffffff);
        scene::Primitive layer; layer.index_count=u32(reflection.indices.size());
        layer.flags=scene::kUnlit|scene::kDoubleSided;
        auto base=reflection;
        for(auto& vertex:base.vertices) vertex.color=0xff000000;
        base.primitives.push_back(layer); probe.meshes.push_back(std::move(base));
        layer.flags|=scene::kAdditive|scene::kEnvironmentMap|scene::kNoDepthWrite; layer.env_rows=true;
        reflection.primitives.push_back(layer); probe.meshes.push_back(std::move(reflection));
        probe_=std::make_unique<render::GpuModel>(*app.gpu(),probe);
        capture_=std::make_unique<render::View3D>(*app.gpu());
        camera_=std::make_unique<render::View3D>(*app.gpu());
        main_=std::make_unique<render::View3D>(*app.gpu());
        if(!room_->valid() || !panel_->valid() || !probe_->valid() || !app.vfs()) { error_="Scena o disco non disponibile"; return; }
        render::MenuCarRequest request; request.model="ma0095";
        loader_.start([reader=app.vfs(),context=app.gpu(),request] { return render::load_menu_car(*reader,*context,request); });
    }
public:
    ~ReflectionViewer() override { loader_.finish(); LOG_INFO("Native reflection viewer finished: frames=%llu",(unsigned long long)frames_); }
    void update(App& app,double dt) override {
        try {
            if(!initialized_) initialize(app);
            const auto& input=app.menu();
            if(input.pressed(PadButton::Start)) { app.quit(); return; }
            if(input.pressed(tool_back)) { app.pop(); return; }
            if(input.pressed(tool_confirm)) paused_=!paused_;
            if(input.pressed(PadButton::L1)) reflections_=!reflections_;
            if(input.pressed(PadButton::R1)) freeze_monitor_=!freeze_monitor_;
            if(input.pressed(PadButton::Right) || input.pressed(PadButton::Left)) {
                scene_mode_=(scene_mode_+(input.pressed(PadButton::Right) ? 1 : 4))%5;
                LOG_INFO("Native scene viewer mode=%d",scene_mode_);
            }
            if(loader_.busy()) if(auto result=loader_.take_ready()) { car_=std::move(result->model); LOG_INFO("Native reflection viewer car ready"); }
            if(!paused_) time_+=float(std::clamp(dt,0.0,0.1));
            if(room_ && error_.empty()) {
                std::vector<scene::Vertex> vertices;
                for(size_t face=0;face<room_scene_.meshes.size();++face) {
                    const unsigned r=unsigned(110+70*std::sin(time_*0.7f+float(face)));
                    const unsigned g=unsigned(110+70*std::sin(time_*0.7f+float(face)+2));
                    const unsigned b=unsigned(110+70*std::sin(time_*0.7f+float(face)+4));
                    for(auto vertex:room_scene_.meshes[face].vertices) { vertex.color=0xff000000u|r|(g<<8)|(b<<16); vertices.push_back(vertex); }
                }
                if(!room_->update_vertices(vertices)) error_=SDL_GetError();
            }
        } catch(const std::exception& e) { error_=e.what(); LOG_WARN("Native reflection viewer: %s",e.what()); }
    }
    void render(App&,Ui& ui) override {
        static const char* modes[]={"Base", "Luci per auto", "Nebbia lineare", "Nebbia GT3", "Vista specchiata"};
        ui.header("SCENA NATIVA",modes[scene_mode_]);
        if(car_ && error_.empty()) {
            render::DrawOptions capture_options; capture_options.quantize_rgb5=true;
            render::DrawItem capture_item; capture_item.model=room_.get(); capture_item.lod=0;
            cube_=capture_->render_native_env_cube({0,0,0},{capture_item},capture_options,128,1u<<unsigned(frames_%6));
            if(!cube_) { error_=SDL_GetError(); return; }
            render::DrawOptions options; options.gt3_lighting=true;
            options.native_env_cube=reflections_ ? cube_ : nullptr; options.reflection_strength=reflections_ ? 0.8f : 0;
            if(!freeze_monitor_ || !feed_) {
                const render::Vec3 eye{6,3,7};
                render::DrawItem camera_item; camera_item.model=car_.get(); camera_item.transform=render::rotate_y(-time_*0.4f); camera_item.lod=0;
                auto image=camera_->render_native_offscreen(0,320,180,render::look_at(eye,{0,0.6f,0},{0,1,0}),
                    render::perspective(0.65f,320.0f/180,0.1f,100),eye,{camera_item},options);
                if(!image) { error_=SDL_GetError(); return; }
                feed_=std::move(image);
            }
            render::DrawItem monitor; monitor.model=panel_.get(); monitor.transform=render::translate({3,0.2f,0}); monitor.native_texture=feed_;
            const SDL_FRect rect{0,32,ui.width(),ui.height()-116};
            const render::Vec3 eye{7,3,9};
            render::ItemLights lamps;
            lamps.count=2; lamps.ambient[0]=lamps.ambient[1]=lamps.ambient[2]=0.12f;
            lamps.dir[0][0]=std::sin(time_); lamps.dir[0][1]=0.6f; lamps.dir[0][2]=std::cos(time_);
            lamps.dir[1][0]=-1; lamps.dir[1][1]=0.4f; lamps.dir[1][2]=-0.5f;
            lamps.diffuse[0][0]=1.7f; lamps.diffuse[0][1]=0.25f; lamps.diffuse[0][2]=0.1f;
            lamps.diffuse[1][0]=0.1f; lamps.diffuse[1][1]=0.3f; lamps.diffuse[1][2]=1.7f;
            for(int i=0;i<2;++i) for(int c=0;c<3;++c) lamps.specular[i][c]=lamps.diffuse[i][c];
            render::DrawItem car; car.model=car_.get(); car.transform=render::rotate_y(time_*0.35f);
            if(scene_mode_==1) car.lights=&lamps;
            render::DrawItem background; background.model=room_.get(); background.background=true;
            background.transform=render::scale({20,20,20});
            monitor.single_sided=true;
            options.frustum_cull=true; options.draw_distance=100; options.far_clip=100;
            if(scene_mode_==2 || scene_mode_==3) {
                options.fog=true; options.fog_gt3=scene_mode_==3;
                options.fog_start=scene_mode_==3 ? 3 : 6; options.fog_end=16; options.fog_max=0.9f;
                options.fog_color[0]=0.6f; options.fog_color[1]=0.68f; options.fog_color[2]=0.78f;
            }
            auto projection=render::perspective(0.45f,rect.w/rect.h,0.1f,100);
            if(scene_mode_==4) { options.mirrored=true; projection=render::scale({-1,1,1})*projection; }
            render::DrawItem probe; probe.model=probe_.get(); probe.transform=render::translate({-3,0,0}); probe.lod=0;
            main_->draw(ui.renderer(),rect,render::look_at(eye,{1,0.7f,0},{0,1,0}),projection,eye,
                {background,car,probe,monitor},options);
            for(unsigned face=0;face<6;++face) {
                auto image=capture_->native_env_face(face);
                if(image && image->renderer_texture()) {
                    auto* texture=image->renderer_texture(); SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
                    SDL_FRect tile{14+float(face)*76,ui.height()-77,64,48}; SDL_RenderTexture(ui.renderer(),texture,nullptr,&tile);
                }
            }
            ++frames_;
        } else if(error_.empty()) ui.text(12,50,"Caricamento auto...",colors::kDim);
        if(!error_.empty()) ui.text(12,50,Ui::clip(error_,ui.width()-24),colors::kBad);
        ui.text(10,ui.height()-18,"Sinistra/destra: scena   A: pausa   L: riflessi   R: monitor   +: esci",colors::kDim);
    }
};
GT3R_REGISTER_SCREEN(reflection_viewer,"reflections","Reflections and secondary views","Native offscreen views and environment capture.",93,[] { return std::make_unique<ReflectionViewer>(); });
} // namespace
} // namespace gt3r
