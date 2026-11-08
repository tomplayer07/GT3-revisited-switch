// Exercises the same GpuModel update/sharing API used by race animations.
#include "app/app.h"
#include "app/tool_buttons.h"
#include "app/screen_registry.h"
#include "base/log.h"
#include "render/menu_car_load.h"
#include "render/view3d.h"
#include <cmath>
#include <limits>

namespace gt3r {
namespace {
scene::Mesh flag_mesh(unsigned strips,float phase) {
    scene::Mesh mesh; mesh.name="flag";
    for(unsigned i=0;i<=strips;++i) for(unsigned row=0;row<2;++row) {
        scene::Vertex vertex;
        const float u=float(i)/strips;
        vertex.pos[0]=(u-0.5f)*1.5f; vertex.pos[1]=float(row);
        vertex.pos[2]=0.18f*std::sin(phase+u*6)*u;
        vertex.normal[2]=1; vertex.uv[0]=u; vertex.uv[1]=1-float(row);
        mesh.vertices.push_back(vertex);
    }
    for(unsigned i=0;i<strips;++i) { const u32 a=2*i; for(u32 index:{a,a+2,a+1,a+1,a+2,a+3}) mesh.indices.push_back(index); }
    scene::Primitive primitive; primitive.index_count=u32(mesh.indices.size()); primitive.texture=0;
    primitive.flags=scene::kUnlit|scene::kDoubleSided; mesh.primitives.push_back(primitive);
    return mesh;
}
class DynamicViewer final:public Screen {
    std::unique_ptr<render::GpuModel> source_,stable_,variable_,car_;
    std::unique_ptr<render::View3D> view_;
    render::MenuCarLoader car_loader_;
    bool initialized_=false,paused_=false,colours_=true;
    unsigned topology_=0;
    float time_=0,colour_time_=1;
    uint64_t frames_=0;
    std::string error_;
    std::vector<u32> pixels_;
    void initialize(App& app) {
        initialized_=true;
        if(!app.gpu()) { error_="Renderer 3D non disponibile"; return; }
        scene::Model scene; scene.meshes.push_back(flag_mesh(8,0));
        scene::Texture texture; texture.width=32; texture.height=4; texture.no_mips=true;
        texture.clamp_u=texture.clamp_v=true;
        texture.rgba.resize(128,0xff40a0ff); pixels_=texture.rgba; scene.textures.push_back(std::move(texture));
        source_=std::make_unique<render::GpuModel>(*app.gpu(),scene,true);
        stable_=std::make_unique<render::GpuModel>(*app.gpu(),*source_);
        variable_=std::make_unique<render::GpuModel>(*app.gpu(),*source_);
        if(!source_->valid() || !stable_->update_geometry({flag_mesh(8,0)}) || !variable_->update_geometry({flag_mesh(4,0)}))
            { error_="Creazione delle bandiere fallita"; return; }
        if(app.gpu()->native_renderer()) {
            auto invalid=scene.meshes[0].vertices;
            invalid[0].pos[0]=1000; invalid.back().pos[0]=std::numeric_limits<float>::quiet_NaN();
            const bool rejected_vertices=!source_->update_vertices(invalid);
            const render::GpuModel::TextureUpdate bad[]={{0,&pixels_},{99,&pixels_}};
            const bool rejected_textures=!source_->update_textures({bad[0],bad[1]});
            LOG_INFO("Native dynamic validation: malformed_vertices_rejected=%d malformed_texture_batch_rejected=%d",int(rejected_vertices),int(rejected_textures));
            SDL_ClearError();
        }
        if(app.vfs() && app.gpu()->native_renderer() && app.gpu()->native_renderer()->background_model_upload()) {
            render::MenuCarRequest request; request.model="ma0095";
            car_loader_.start([reader=app.vfs(),context=app.gpu(),request] { return render::load_menu_car(*reader,*context,request); });
        }
        view_=std::make_unique<render::View3D>(*app.gpu());
    }
public:
    ~DynamicViewer() override {
        car_loader_.finish();
        LOG_INFO("Dynamic model viewer finished: frames=%llu",(unsigned long long)frames_);
    }
    void update(App& app,double dt) override {
        try {
            if(!initialized_) initialize(app);
            const auto& input=app.menu();
            if(input.pressed(PadButton::Start)) { app.quit(); return; }
            if(input.pressed(tool_back)) { app.pop(); return; }
            if(input.pressed(tool_confirm)) paused_=!paused_;
            if(input.pressed(PadButton::L1)) colours_=!colours_;
            if(input.pressed(PadButton::R1)) { topology_=(topology_+1)%3; LOG_INFO("Dynamic model topology mode: %u",topology_); }
            if(car_loader_.busy()) {
                try {
                    if(auto ready=car_loader_.take_ready()) { car_=std::move(ready->model); LOG_INFO("Dynamic viewer comparison car ready"); }
                } catch(const std::exception& e) { LOG_WARN("Dynamic viewer comparison car unavailable: %s",e.what()); }
            }
            if(!view_ || !error_.empty()) return;
            if(!paused_) time_+=float(std::clamp(dt,0.0,0.1));
            const auto mesh=flag_mesh(8,time_*3);
            if(!source_->update_vertices(mesh.vertices) || !stable_->update_geometry({flag_mesh(8,time_*3+1)}))
                { error_=SDL_GetError(); return; }
            const bool changed=topology_==2 ? variable_->update_geometry({}) : variable_->update_geometry({flag_mesh(topology_==0 ? 4 : 12,time_*3+2)});
            if(!changed) { error_=SDL_GetError(); return; }
            if(colours_ && !paused_) colour_time_+=float(dt);
            if(colour_time_>=0.2f) {
                colour_time_=0;
                const unsigned r=unsigned(120+100*std::sin(time_));
                const unsigned g=unsigned(120+100*std::sin(time_+2));
                const unsigned b=unsigned(120+100*std::sin(time_+4));
                for(unsigned y=0;y<4;++y) for(unsigned x=0;x<32;++x)
                    pixels_[y*32+x]=((x/4+y)%2) ? 0xffffffff : 0xff000000u|r|(g<<8)|(b<<16);
                if(!source_->update_textures({{0,&pixels_}})) error_=SDL_GetError();
            }
            ++frames_;
        } catch(const std::exception& e) { error_=e.what(); LOG_WARN("Dynamic viewer: %s",e.what()); }
    }
    void render(App&,Ui& ui) override {
        ui.header("ANIMAZIONI",car_ ? "Colori condivisi tra le bandiere; l'auto deve restare invariata" : "Tre bandiere con colori condivisi");
        if(view_ && error_.empty()) {
            const SDL_FRect rect{0,32,ui.width(),ui.height()-64};
            render::DrawOptions options; options.gt3_lighting=true;
            const render::Vec3 eye{8,5,10};
            const auto view=render::look_at(eye,{0,1,0},{0,1,0});
            const auto projection=render::perspective(0.7f,rect.w/rect.h,0.1f,100);
            std::vector<render::DrawItem> items;
            items.push_back({source_.get(),render::translate({-2.2f,1.5f,0}),0});
            items.push_back({stable_.get(),render::translate({0,1.5f,0}),0});
            items.push_back({variable_.get(),render::translate({2.2f,1.5f,0}),0});
            if(car_) items.push_back({car_.get(),{},0});
            view_->draw(ui.renderer(),rect,view,projection,eye,items,options);
        }
        if(!error_.empty()) ui.text(10,50,Ui::clip(error_,ui.width()-20),colors::kBad);
        ui.text(10,ui.height()-20,"A: pausa   L: colori   R: cambia terza bandiera   +: esci",colors::kDim);
    }
};
GT3R_REGISTER_SCREEN_NO_DISC(dynamic_viewer,"dynamic","Animated models","Geometry and shared-texture animation viewer.",92,[] { return std::make_unique<DynamicViewer>(); });
} // namespace
} // namespace gt3r
