#include "../deko-probe/graphics.h"
#include "../deko-probe/timing.h"
#include "race/lamp_glows.h"
#include "render/view3d.h"
#include "render/test_models.h"
#include "render/renderer_abi.h"
#include <switch.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cmath>
using namespace gt3r::deko_probe;

int main() {
    open_log();
    trace("SDL3 / Deko3D shared-device geometry probe");
    trace("Demo comparison build: read-only clock polling and warmup-excluded CPU/geometry counters");
    if (R_FAILED(romfsInit())) { trace("romfs failed"); close_log(); return 1; }
    int result = 0;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    try {
        SDL_SetMainReady();
        if (!SDL_Init(SDL_INIT_VIDEO)) throw SDL_GetError();
        if (!gt3r::render::verify_renderer_abi(gt3r::render::local_renderer_abi())) throw SDL_GetError();
        trace("Renderer client/engine ABI verified before creating graphics objects");
        window = SDL_CreateWindow("GT3 SDL Deko3D Probe", 1280, 720, 0);
        if (!window) throw SDL_GetError();
        trace("SDL window created without OpenGL");
        Graphics graphics;
        graphics.init();
        graphics.toggle_overlay(); // SDL supplies the overlay for this milestone.
        // Backend owns SDL; the observer clears the outer pointer on unwinding
        // so catch handlers never destroy an already released renderer.
        struct RendererObserver { SDL_Renderer*& renderer; ~RendererObserver() { renderer=nullptr; } } observer{renderer};
        auto nativeBackend=graphics.create_native_renderer(window);
        if(!nativeBackend) throw SDL_GetError();
        renderer=nativeBackend->renderer2d();
        if(!SDL_FlushRenderer(renderer)) throw SDL_GetError();
        trace("SDL flush/invalidate callback verified before frontend model creation");
        auto context=gt3r::render::GpuContext::adopt_native(nativeBackend);
        if(!context) throw SDL_GetError();
        auto frontendSource=gt3r::render::make_test_model(0);
        frontendSource.meshes.resize(1); // textured cube; independent of course packets
        auto frontendModel=std::make_unique<gt3r::render::GpuModel>(*context,frontendSource);
        if(!frontendModel->valid()) throw SDL_GetError();
        auto frontendView=std::make_unique<gt3r::render::View3D>(*context);
        trace("Real Deko frontend adapter adopted by GpuContext; GpuModel/View3D ready");
        if (!renderer) throw SDL_GetError();
        trace("SDL gt3r-deko renderer created");
        auto carGlows=std::make_unique<gt3r::race::CarLampGlows>();
        graphics.init_car_glows(*carGlows,renderer);
        auto streetGlows=std::make_unique<gt3r::race::LampGlows>();
        graphics.init_street_glows(*streetGlows,renderer);
        SDL_Texture* badge = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 32, 32);
        if (!badge) throw SDL_GetError();
        Uint8 badgePixels[32 * 32 * 4];
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            auto* p = badgePixels + 4 * (y * 32 + x);
            p[0] = x < 16 ? 255 : 0; p[1] = y < 16 ? 200 : 40;
            p[2] = x < 16 ? 20 : 255; p[3] = y < 16 ? 255 : 128;
        }
        void* lockedPixels = nullptr;
        int lockedPitch = 0;
        if (!SDL_LockTexture(badge, nullptr, &lockedPixels, &lockedPitch)) throw SDL_GetError();
        for (int y = 0; y < 32; ++y)
            SDL_memcpy(static_cast<Uint8*>(lockedPixels) + y * lockedPitch, badgePixels + y * 32 * 4, 32 * 4);
        SDL_UnlockTexture(badge);
        if (!SDL_SetTextureBlendMode(badge, SDL_BLENDMODE_BLEND)) throw SDL_GetError();
        if (!SDL_SetTextureScaleMode(badge, SDL_SCALEMODE_NEAREST)) throw SDL_GetError();
        trace("SDL badge texture created and uploaded");
        bool patchGreen = false, rotateCard = true;
        SDL_Texture* card = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, 256, 96);
        if (!card) throw SDL_GetError();
        if (!SDL_SetTextureBlendMode(card, SDL_BLENDMODE_BLEND)) throw SDL_GetError();
        if (!SDL_SetRenderTarget(renderer, card)) throw SDL_GetError();
        SDL_SetRenderDrawColor(renderer, 10, 25, 65, 255);
        if (!SDL_RenderClear(renderer)) throw SDL_GetError();
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        if (!SDL_RenderDebugText(renderer, 12, 12, "SDL RENDER TARGET")) throw SDL_GetError();
        if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)) throw SDL_GetError();
        SDL_SetRenderDrawColor(renderer, 255, 140, 20, 190);
        const SDL_FRect accent{12,40,230,35};
        if (!SDL_RenderFillRect(renderer, &accent)) throw SDL_GetError();
        if (!SDL_SetRenderTarget(renderer, nullptr)) throw SDL_GetError();
        trace("SDL target card commands queued; window target restored");
        const auto sceneProps = SDL_CreateProperties();
        SDL_SetNumberProperty(sceneProps, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
        SDL_SetNumberProperty(sceneProps, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
        SDL_SetNumberProperty(sceneProps, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, 1280);
        SDL_SetNumberProperty(sceneProps, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, 720);
        auto sceneImage = graphics.scene_image();
        SDL_SetPointerProperty(sceneProps, GT3R_DEKO_BORROW_IMAGE_PROPERTY, &sceneImage);
        SDL_Texture* scenePreview = SDL_CreateTextureWithProperties(renderer, sceneProps);
        SDL_DestroyProperties(sceneProps);
        if (!scenePreview) throw SDL_GetError();
        if (!SDL_SetTextureScaleMode(scenePreview, SDL_SCALEMODE_LINEAR)) throw SDL_GetError();
        trace("SDL borrowed scene wrapper created (no image copy)");
        bool animateBadge = false;


        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState pad;
        padInitializeDefault(&pad);
        bool running = true, overlay = false, first = true;
        Uint64 lastCameraTick=SDL_GetTicks();
        bool materialMode=true, streetGlowsEnabled=true;
        unsigned completedFrames=0;
        TimingStats capturePauses, startupLoops;
        SlowFrames slowFrames;
        TimingStats loopTimes[2], sdlPresentTimes[2], streetLoops[2];
        while (running) {
            const auto loopBegin=TimingClock::now();
            bool capturedThisFrame=false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_QUIT) running = false;
            if (!running) break;
            padUpdate(&pad);
            const auto buttons = padGetButtonsDown(&pad);
            const auto held=padGetButtons(&pad);
            const auto left=padGetStickPos(&pad,0), right=padGetStickPos(&pad,1);
            const auto axis=[](int v) { const float x=float(v)/32767.0f; return std::abs(x)<0.15f ? 0.0f : x; };
            graphics.driving_input((held&HidNpadButton_ZR) ? 1.0f : 0.0f,(held&HidNpadButton_ZL) ? 1.0f : 0.0f,axis(left.x),buttons&HidNpadButton_StickR);
            const auto now=SDL_GetTicks();
            graphics.move_camera(axis(left.y),axis(left.x),axis(right.x),axis(right.y),
                float(bool(held&HidNpadButton_R))-float(bool(held&HidNpadButton_L)),float(now-lastCameraTick)/1000.0f);
            lastCameraTick=now;
            if (buttons & HidNpadButton_Plus) break;
            if (buttons & HidNpadButton_StickL) trace(graphics.toggle_race_camera() ? "engine chase camera on" : "free camera on");
            if (buttons & HidNpadButton_Down) trace(graphics.toggle_course_animations() ? "original course palette and node animations on" : "original course palette and node animations frozen");
            if (buttons & HidNpadButton_Right) trace(graphics.toggle_point_lighting() ? "original course point lighting on" : "generic preview car lighting on");
            if (buttons & HidNpadButton_Up) trace(graphics.toggle_wheel_motion() ? "original wheel motion on" : "wheel motion off (static bind pose)");
            if (buttons & HidNpadButton_R) {
                trace(graphics.toggle_course_fog() ? "original course fog on" : "original course fog off");
            }
            if (buttons & HidNpadButton_Left) { streetGlowsEnabled=graphics.toggle_street_glows(); trace(streetGlowsEnabled ? "street lamp glows on" : "street lamp glows off"); }
            const bool manualCapture=buttons & HidNpadButton_Minus;
            if (manualCapture) {
                trace("Manual environment capture requested");
                const auto captureBegin=TimingClock::now();
                graphics.capture_environment();
                capturePauses.add(elapsed_ms(captureBegin,TimingClock::now()));
                capturedThisFrame=true;
                lastCameraTick=SDL_GetTicks();
            }


            if (buttons & HidNpadButton_A) { overlay = !overlay; trace(overlay ? "SDL overlay on" : "SDL overlay off"); }
            if (buttons & HidNpadButton_B) {
                patchGreen = !patchGreen;
                const SDL_Rect rect{12,12,8,8};
                if (!SDL_LockTexture(badge, &rect, &lockedPixels, &lockedPitch)) throw SDL_GetError();
                for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                    auto* p = static_cast<Uint8*>(lockedPixels) + y * lockedPitch + x * 4;
                    p[0] = patchGreen ? 0 : 255; p[1] = patchGreen ? 255 : 0;
                    p[2] = 0; p[3] = 255;
                }
                SDL_UnlockTexture(badge);
                trace(patchGreen ? "SDL badge patch green" : "SDL badge patch red");
            }
            if (buttons & HidNpadButton_Y) {
                rotateCard = !rotateCard;
                trace(rotateCard ? "SDL target card rotated" : "SDL target card straight");
            }
            if (buttons & HidNpadButton_X) {
                animateBadge = !animateBadge;
                trace(animateBadge ? "continuous SDL uploads on" : "continuous SDL uploads off");
            }
            if (overlay && animateBadge) {
                const SDL_Rect rect{12,12,8,8};
                if (!SDL_LockTexture(badge, &rect, &lockedPixels, &lockedPitch)) throw SDL_GetError();
                const Uint8 shade = Uint8((SDL_GetTicks() / 5) % 256);
                for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                    auto* p = static_cast<Uint8*>(lockedPixels) + y * lockedPitch + x * 4;
                    p[0] = shade; p[1] = Uint8(255 - shade); p[2] = 130; p[3] = 255;
                }
                SDL_UnlockTexture(badge);
            }
            graphics.look_back(held&HidNpadButton_L);
            graphics.prepare_frame();
            const auto streetBegin=TimingClock::now();
            graphics.draw_street_glows(*streetGlows,renderer);
            const auto carBegin=TimingClock::now();
            graphics.draw_car_glows(*carGlows,renderer);
            const auto glowsEnd=TimingClock::now();
            if (overlay) {
                if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)) throw SDL_GetError();
                SDL_SetRenderDrawColor(renderer, 15, 180, 255, 128);
                SDL_FRect panel{90, 150, 1100, 125};
                if (!SDL_RenderFillRect(renderer, &panel)) throw SDL_GetError();
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                if (!SDL_SetRenderScale(renderer, 2, 2)) throw SDL_GetError();
                if (!SDL_RenderDebugText(renderer, 58, 87, "ROUTE 5 / ZR: ACCEL  ZL: BRAKE  R-CLICK: RESET  +: EXIT")) throw SDL_GetError();
                if (!SDL_SetRenderScale(renderer, 1, 1)) throw SDL_GetError();
                SDL_FRect badgeRect{530, 490, 220, 140};
                if (!SDL_RenderTexture(renderer, badge, nullptr, &badgeRect)) throw SDL_GetError();
                const SDL_FRect cardRect{115,485,360,135};
                if (!SDL_RenderTextureRotated(renderer, card, nullptr, &cardRect,
                        rotateCard ? 12.0 : 0.0, nullptr, SDL_FLIP_NONE)) throw SDL_GetError();
                const auto center=gt3r::render::Vec3{-1.3f,0.75f,0};
                gt3r::render::DrawItem item{}; item.model=frontendModel.get();
                item.transform=gt3r::render::translate(center)*gt3r::render::rotate_y(float(now)*0.001f)*
                    gt3r::render::translate(center*-1.0f);
                gt3r::render::DrawOptions viewOptions;
                const auto eye=center+gt3r::render::Vec3{3,2,4};
                frontendView->draw(renderer,{820,245,350,197},gt3r::render::look_at(eye,center,{0,1,0}),
                    gt3r::render::perspective(0.785398f,350.0f/197.0f,0.1f,100),eye,{item},viewOptions);
                SDL_SetRenderDrawColor(renderer,255,255,255,255);
                SDL_RenderDebugText(renderer,820,225,"GpuContext / GpuModel / View3D");
                const SDL_FRect previewRect{820,470,350,197};
                if (!SDL_RenderTexture(renderer, scenePreview, nullptr, &previewRect)) throw SDL_GetError();
                const float offset = float(std::sin(double(SDL_GetTicks()) / 600.0) * 65.0);
                SDL_Vertex vertices[3] = {
                    {{540 + offset, 310}, {1,0.2f,0.1f,0.75f}, {0,0}},
                    {{700 + offset, 310}, {0.1f,1,0.3f,0.75f}, {0,0}},
                    {{620 + offset, 450}, {0.1f,0.3f,1,0.75f}, {0,0}}
                };
                if (!SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0)) throw SDL_GetError();
            }
            char speedLabel[160]; std::snprintf(speedLabel,sizeof(speedLabel),"%.0f km/h | ZR: accelerate  ZL: brake  L-stick: steer  R-click: reset",graphics.speed_kmh());
            SDL_SetRenderDrawColor(renderer,255,255,255,255);
            if (!SDL_RenderDebugText(renderer,20,20,speedLabel)) throw SDL_GetError();
            const auto presentBegin=TimingClock::now();
            if (!SDL_RenderPresent(renderer)) throw SDL_GetError();
            const auto frameEnd=TimingClock::now();
            auto phases=graphics.frame_phases();
            phases.streetGlows=elapsed_ms(streetBegin,carBegin); phases.carGlows=elapsed_ms(carBegin,glowsEnd);
            slowFrames.add({completedFrames,buttons,elapsed_ms(loopBegin,frameEnd),elapsed_ms(loopBegin,presentBegin),phases});
            if (completedFrames<120 && !capturedThisFrame) startupLoops.add(elapsed_ms(loopBegin,frameEnd));
            // Do not include startup or toggle logging in the steady comparison.
            if (!first && !capturedThisFrame && !(buttons & (HidNpadButton_Minus|HidNpadButton_ZR|HidNpadButton_ZL|HidNpadButton_A|HidNpadButton_B|HidNpadButton_X|HidNpadButton_Y))) {
                streetLoops[streetGlowsEnabled ? 1 : 0].add(elapsed_ms(loopBegin,frameEnd));
                loopTimes[materialMode ? 1 : 0].add(elapsed_ms(loopBegin,frameEnd));
                sdlPresentTimes[materialMode ? 1 : 0].add(elapsed_ms(presentBegin,frameEnd));
            }
            ++completedFrames;
            if (first) { trace("first frame through SDL_RenderPresent completed"); first = false; }
        }
        streetLoops[0].report("app_loop_street_glows","off");
        slowFrames.report();
        streetLoops[1].report("app_loop_street_glows","on");
        capturePauses.report("explicit_capture_pause","setup");
        startupLoops.report("startup_app_loop","first_120_frames");
        for (unsigned mode=0; mode<2; ++mode) {
            loopTimes[mode].report("app_loop",mode ? "materials" : "generic");
            sdlPresentTimes[mode].report("sdl_present",mode ? "materials" : "generic");
        }
        streetGlows.reset();
        carGlows.reset();
        frontendView.reset(); frontendModel.reset(); context.reset();
        nativeBackend.reset(); renderer=nullptr;
        trace("SDL renderer released before native device");
    } catch (const std::exception& error) {
        trace(error.what()); result=1;
        if (renderer) { SDL_DestroyRenderer(renderer); renderer=nullptr; }
    } catch (const char* message) {
        trace(message); result = 1;
        if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    romfsExit();
    trace("SDL probe shutdown complete");
    close_log();
    return result;
}
