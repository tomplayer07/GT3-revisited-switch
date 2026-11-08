// Scene-independent native frontend smoke application. No GTDK/GTDS/ISO input.
#include "../deko-probe/graphics.h"
#include "render/renderer_abi.h"
#include "render/deko/backend.h"
#include "render/test_models.h"
#include <switch.h>
#include <SDL3/SDL_main.h>
#include <cstdio>

using namespace gt3r::deko_probe;
int main() {
    open_log("sdmc:/switch/gt3r-deko-frontend.log");
    trace("Scene-independent Deko3D frontend harness");
    if(R_FAILED(romfsInit())) { trace("romfs failed"); close_log(); return 1; }
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    int result=0;
    try {
        SDL_SetMainReady();
        if(!SDL_Init(SDL_INIT_VIDEO)) throw SDL_GetError();
        if(!gt3r::render::verify_renderer_abi(gt3r::render::local_renderer_abi())) throw SDL_GetError();
        trace("Renderer client/engine ABI verified");
        window=SDL_CreateWindow("GT3 Deko3D Frontend",1280,720,0);
        if(!window) throw SDL_GetError();
        struct Observer { SDL_Renderer*& renderer; ~Observer() { renderer=nullptr; } } observer{renderer};
        auto backend=gt3r::render::deko::create_renderer(window);
        if(!backend) throw SDL_GetError();
        renderer=backend->renderer2d();
        if(!SDL_FlushRenderer(renderer)) throw SDL_GetError();
        trace("SDL flush/invalidate callback verified");
        auto context=gt3r::render::GpuContext::adopt_native(backend);
        if(!context) throw SDL_GetError();
        auto source=gt3r::render::make_test_model(0);
        source.meshes.resize(1);
        auto model=std::make_unique<gt3r::render::GpuModel>(*context,source);
        if(!model->valid()) throw SDL_GetError();
        auto view=std::make_unique<gt3r::render::View3D>(*context);
        trace("Procedural model ready without race scene or external assets");
        padConfigureInput(1,HidNpadStyleSet_NpadStandard);
        ::PadState pad; padInitializeDefault(&pad);
        bool wide=true,running=true;
        unsigned frames=0;
        while(running) {
            SDL_Event event;
            while(SDL_PollEvent(&event)) if(event.type==SDL_EVENT_QUIT) running=false;
            padUpdate(&pad);
            const auto buttons=padGetButtonsDown(&pad);
            if(!running || (buttons&HidNpadButton_Plus)) break;
            if(buttons&HidNpadButton_A) { wide=!wide; trace(wide ? "Frontend layout wide" : "Frontend layout compact"); }
            SDL_SetRenderDrawColor(renderer,12,18,28,255);
            if(!SDL_RenderClear(renderer)) throw SDL_GetError();
            SDL_SetRenderDrawColor(renderer,255,255,255,255);
            SDL_RenderDebugText(renderer,30,30,"DEKO3D / GAME FRONTEND / NO RACE ASSETS");
            SDL_RenderDebugText(renderer,30,55,"A: resize view   +: exit");
            const SDL_FRect rect=wide ? SDL_FRect{320,170,640,360} : SDL_FRect{440,190,400,300};
            const gt3r::render::Vec3 center{-1.3f,0.75f,0},eye=center+gt3r::render::Vec3{3,2,4};
            gt3r::render::DrawItem item{}; item.model=model.get();
            item.transform=gt3r::render::translate(center)*gt3r::render::rotate_y(float(SDL_GetTicks())*0.001f)*
                gt3r::render::translate(center*-1.0f);
            gt3r::render::DrawOptions options;
            view->draw(renderer,rect,gt3r::render::look_at(eye,center,{0,1,0}),
                gt3r::render::perspective(0.785398f,rect.w/rect.h,0.1f,100),eye,{item},options);
            if(!SDL_RenderPresent(renderer)) throw SDL_GetError();
            context->frame_presented();
            ++frames;
        }
        char summary[96]; std::snprintf(summary,sizeof(summary),"Frontend harness completed: frames=%u",frames); trace(summary);
        view.reset(); model.reset(); context.reset(); backend.reset(); renderer=nullptr;
        trace("Native frontend resources released");
    } catch(const std::exception& error) { trace(error.what()); result=1; }
      catch(const char* error) { trace(error); result=1; }
    if(window) SDL_DestroyWindow(window);
    SDL_Quit(); romfsExit();
    trace("Frontend harness shutdown complete");
    close_log();
    return result;
}
