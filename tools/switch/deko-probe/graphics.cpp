// GT3 Revisited Deko3D presentation probe.
// API usage reference: switchbrew/switch-examples, graphics/deko3d/deko_basic.
#include <switch.h>
#include <deko3d.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cmath>

#include "graphics.h"
#include "render/deko/image_array.h"
#include "render/deko/borrowed_image.h"
#include "timing.h"
#ifdef GT3R_DEKO_MODEL
#include "../deko-model/model.h"
#include "../deko-model/wheel_pose.h"
#include "race/camera_rig.h"
#include "race/interp.h"
#include "../deko-session/session.h"
#include "race/lamp_glows.h"
#include "race/occluder.h"
#include "formats/model/car_model.h"
#include "formats/course/course_file.h"
#endif

namespace gt3r::deko_probe {
constexpr unsigned width = 1280, height = 720, frames = 2;
constexpr uint32_t codeSize = 131072, commandSize = 65536;
constexpr unsigned descriptorCount = 128, samplerOffset = 4096;
FILE* diagnosticLog = nullptr;
void trace(const char* stage) {
    if (diagnosticLog) {
        std::fprintf(diagnosticLog, "%s\n", stage);
        std::fflush(diagnosticLog);
    }
}
void debugMessage(void*, const char* context, DkResult result, const char* message) {
    if (diagnosticLog) {
        std::fprintf(diagnosticLog, "Deko3D result=%u context=%s message=%s\n",
                     unsigned(result), context ? context : "?", message ? message : "?");
        std::fflush(diagnosticLog);
    }
    // Deko3D requires fatal callbacks not to return. Do not attempt to unwind
    // or submit more GPU work after a validation/device failure.
    if (result != DkResult_Success) std::_Exit(1);
}
struct GraphicsState {
    FramePhases phases;
    struct FrameTiming { TimingStats acquire, scene, ui, present, total, cadence; };
    FrameTiming frameTiming[2];
    bool materialMode=true, havePresentTime=false, vertexDemo=false;
    bool vertexDemoWasActive=false, textureDemo=false, textureDemoWasActive=false;
    bool objectDemo=true, objectDemoWasActive=false;
    unsigned previousMode=1;
    TimingClock::time_point previousPresentTime{};
    DkDevice device{};
    DkQueue queue{};
    DkSwapchain swapchain{};
    DkCmdBuf commands{};
    DkMemBlock indexMemory{}, codeMemory{}, commandsMemory{};
    DkMemBlock textureMemory{}, uploadMemory{}, descriptorMemory{};
    render::deko::ImageArray swapTargets, sceneTargets, depthTargets;
    DkImage texture{};
    DkShader shaders[9]{};
#ifdef GT3R_DEKO_MODEL
    std::unique_ptr<Model> model;
    race::CameraRig raceCamera;
    sim::VehicleState initialVehicle;
    sim::VehicleSetup vehicleSetup;
    bool followCamera=true, lampFrontView=false;
    double previousVehicleTime=0, frameSeconds=0;
    bool framePrepared=false;
    sim::VehicleState renderVehicle;
    std::array<sim::VehicleState,6> previousCars{},renderCars{};
    std::array<render::Mat4,6> bodyPoses{},bodyInverses{};
    std::array<float,6> maximumCarSpeeds{};
    race::CarLampSet carLamps;
    race::Occluder carBodyOccluder;
    uint64_t glowFrames=0, glowTriangles=0;
    uint64_t streetGlowFrames=0, streetGlowTriangles=0;
    bool streetGlowsEnabled=true;
    unsigned preparedTicks=0;
    std::unique_ptr<Session> physics;
    std::vector<uint8_t> physicsBytes;
    sim::ControlInputs drivingInput;
    sim::VehicleState previousPhysics;
    race::FixedStep physicsStep;
    bool resetPhysicsRequested=false, courseAnimations=true;
    float maximumSpeed=0;
#endif
    DkCmdList bind[frames]{}, draw{};
    DkCmdList compose[frames]{}, overlay{};
    DkCmdList upload{};
    uint32_t codeOffset{};
    bool showOverlay = true;
#ifdef GT3R_DEKO_SDL
    struct UiImage {
        DkImage image{};
        DkMemBlock memory{}, staging{}, commandMemory{};
        DkCmdBuf commands{};
        DkFence uploadFence{};
        bool uploadSubmitted = false;
        unsigned descriptor = 0;
        int w = 0, h = 0;
        ~UiImage() {
            if (commands) dkCmdBufDestroy(commands);
            if (commandMemory) dkMemBlockDestroy(commandMemory);
            if (staging) dkMemBlockDestroy(staging);
            if (memory) dkMemBlockDestroy(memory);
        }
    };
    using BorrowedImage = render::deko::BorrowedImageLease;
    struct UiTexture {
        int w, h;
        bool target = false, borrowedScene = false;
        std::shared_ptr<BorrowedImage> borrowedImage;
        std::vector<uint8_t> pixels;
        std::shared_ptr<UiImage> image;
    };
    std::vector<std::unique_ptr<UiTexture>> uiTextures;
    std::vector<std::shared_ptr<UiImage>> uiImages;
    std::vector<std::shared_ptr<UiImage>> uiFrameImages[frames];
    bool descriptorUsed[descriptorCount]{};
    size_t uiUploadCount = 0, uiRetiredCount = 0, uiPeakImages = 0, uiPeakTriangles = 0;
    std::shared_ptr<UiImage> upload_ui_texture(const UiTexture& texture) {
        // New immutable image versions never overwrite GPU-visible pixels.
        // Only completed uploads and unreferenced frame versions can be retired.
        collect_ui_images();
        auto free_id = [&] {
            unsigned id = 1 + frames;
            while (id < descriptorCount && descriptorUsed[id]) ++id;
            return id;
        };
        unsigned id = free_id();
        if (id == descriptorCount) {
            // Bounded backpressure on one frame fence, not the whole queue.
            for (unsigned slot = 0; slot < frames && id == descriptorCount; ++slot) {
                if (!uiInFlight[slot]) continue;
                if (dkFenceWait(&uiFence[slot], -1) != DkResult_Success) throw "SDL frame fence wait failed";
                collect_ui_images(); id = free_id();
            }
            for (const auto& retired : uiImages) {
                if (id != descriptorCount) break;
                if (retired.use_count() == 1 && retired->uploadSubmitted) {
                    if (dkFenceWait(&retired->uploadFence, -1) != DkResult_Success) throw "SDL upload fence wait failed";
                    // Collect after the iteration: it mutates uiImages.
                    break;
                }
            }
            collect_ui_images(); id = free_id();
        }
        if (id == descriptorCount) throw "SDL texture descriptor pool exhausted by live/pending textures";
        auto image = std::make_shared<UiImage>();
        DkImageLayoutMaker lm;
        dkImageLayoutMakerDefaults(&lm, device);
        lm.format = DkImageFormat_RGBA8_Unorm;
        lm.flags = texture.target ? DkImageFlags_UsageRender : 0;
        lm.dimensions[0] = unsigned(texture.w); lm.dimensions[1] = unsigned(texture.h);
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        auto aligned = [](uint32_t n) { return (n + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1); };
        image->memory = allocate(aligned(uint32_t(dkImageLayoutGetSize(&layout))), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        dkImageInitialize(&image->image, &layout, image->memory, 0);
        image->staging = allocate(aligned(uint32_t(texture.pixels.size())), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        std::memcpy(dkMemBlockGetCpuAddr(image->staging), texture.pixels.data(), texture.pixels.size());
        image->commandMemory = allocate(commandSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        DkCmdBufMaker cm;
        dkCmdBufMakerDefaults(&cm, device);
        image->commands = dkCmdBufCreate(&cm);
        if (!image->commands) throw "SDL texture upload command buffer failed";
        DkImageView view;
        dkImageViewDefaults(&view, &image->image);
        DkImageDescriptor descriptor;
        dkImageDescriptorInitialize(&descriptor, &view, false, false);
        auto* dest = static_cast<uint8_t*>(dkMemBlockGetCpuAddr(descriptorMemory));
        std::memcpy(dest + id * sizeof(descriptor), &descriptor, sizeof(descriptor));
        descriptorUsed[id] = true; image->descriptor = id;
        image->w = texture.w; image->h = texture.h;
        dkCmdBufAddMemory(image->commands, image->commandMemory, 0, commandSize);
        dkCmdBufBarrier(image->commands, DkBarrier_None, DkInvalidateFlags_L2Cache);
        DkCopyBuf source{dkMemBlockGetGpuAddr(image->staging), 0, 0};
        DkImageRect rect{0,0,0,unsigned(texture.w),unsigned(texture.h),1};
        dkCmdBufCopyBufferToImage(image->commands, &source, &view, &rect, 0);
        dkCmdBufBarrier(image->commands, DkBarrier_Fragments, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors);
        dkQueueSubmitCommands(queue, dkCmdBufFinishList(image->commands));
        dkQueueSignalFence(queue, &image->uploadFence, true);
        image->uploadSubmitted = true;
        uiImages.push_back(image);
        uiPeakImages = std::max(uiPeakImages, uiImages.size());
        if (++uiUploadCount <= 4) trace("SDL texture upload queued asynchronously");
        return image;
    }
    void collect_ui_images() {
        for (unsigned slot = 0; slot < frames; ++slot) {
            if (uiInFlight[slot] && dkFenceWait(&uiFence[slot], 0) == DkResult_Success) {
                uiFrameImages[slot].clear();
                uiFrameBorrowed[slot].clear();
                uiInFlight[slot] = false;
            }
        }
        for (auto it = uiImages.begin(); it != uiImages.end();) {
            auto& image = *it;
            const bool complete = !image->uploadSubmitted || dkFenceWait(&image->uploadFence, 0) == DkResult_Success;
            if (complete) {
                // Staging and upload commands are independent of sampled image lifetime.
                if (image->commands) { dkCmdBufDestroy(image->commands); image->commands = nullptr; }
                if (image->commandMemory) { dkMemBlockDestroy(image->commandMemory); image->commandMemory = nullptr; }
                if (image->staging) { dkMemBlockDestroy(image->staging); image->staging = nullptr; }
            }
            if (complete && image.use_count() == 1) {
                descriptorUsed[image->descriptor] = false;
                it = uiImages.erase(it);
                ++uiRetiredCount;
            } else ++it;
        }
    }
    struct UiCommand {
        std::shared_ptr<BorrowedImage> borrowedImage;
        std::shared_ptr<UiImage> texture, target;
        bool borrowedScene = false;
        SDL_ScaleMode scale = SDL_SCALEMODE_NEAREST;
        bool clear = false;
        SDL_FColor color{};
        std::vector<GT3R_DekoVertex> vertices;
        SDL_Rect viewport{}, clip{};
        bool clipEnabled = false;
        SDL_BlendMode blend = SDL_BLENDMODE_NONE;
    };
    std::vector<std::shared_ptr<BorrowedImage>> uiFrameBorrowed[frames];
    std::vector<UiCommand> uiCommands;
    std::shared_ptr<UiImage> uiCurrentTarget;
    DkCmdBuf uiBuffer[frames]{};
    DkMemBlock uiMemory[frames]{}, uiUniforms[frames]{};
    DkFence uiFence[frames]{};
    bool uiInFlight[frames]{};
    bool uiReported = false, uiLastVisible = false;
    static constexpr uint32_t uiBytes = 4 * 1024 * 1024;
    uint32_t uiCapacity[frames]{uiBytes,uiBytes};
    void record_ui(unsigned slot) {
        if (uiInFlight[slot] && dkFenceWait(&uiFence[slot], -1) != DkResult_Success)
            throw "SDL UI command-memory fence wait failed";
        uiFrameImages[slot].clear();
        uiFrameBorrowed[slot].clear();
        uiInFlight[slot] = false;
        collect_ui_images();
        dkCmdBufClear(uiBuffer[slot]);
        uint64_t requiredTriangles=0;
        for (const auto& operation:uiCommands) requiredTriangles+=operation.vertices.size()/3;
        if (requiredTriangles>16u*1024u*1024u/256u) throw "SDL UI frame exceeds bounded triangle capacity";
        const uint64_t requiredBytes=requiredTriangles*256;
        if (requiredBytes>uiCapacity[slot]) {
            uint32_t capacity=uiCapacity[slot];
            while (capacity<requiredBytes) capacity*=2;
            auto memory=allocate(capacity,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
            DkMemBlock uniforms{};
            try { uniforms=allocate(capacity,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached); }
            catch (...) { dkMemBlockDestroy(memory); throw; }
            dkMemBlockDestroy(uiMemory[slot]); dkMemBlockDestroy(uiUniforms[slot]);
            uiMemory[slot]=memory; uiUniforms[slot]=uniforms; uiCapacity[slot]=capacity;
            char line[128]; std::snprintf(line,sizeof(line),"SDL fenced overlay pool grew: slot=%u bytes=%u",slot,capacity); trace(line);
        }
        dkCmdBufAddMemory(uiBuffer[slot], uiMemory[slot], 0, uiCapacity[slot]);
        auto cmd = uiBuffer[slot];
        const DkShader* stages[]{&shaders[4], &shaders[5]};
        dkCmdBufBindShaders(cmd, DkStageFlag_GraphicsMask, stages, 2);
        // SDL screen coordinates use Y down and accept either winding. Never
        // inherit the native 3D pass's back-face culling for 2D geometry.
        DkRasterizerState raster;
        dkRasterizerStateDefaults(&raster);
        raster.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(cmd, &raster);
        DkDepthStencilState depth;
        dkDepthStencilStateDefaults(&depth);
        depth.depthTestEnable = depth.depthWriteEnable = false;
        dkCmdBufBindDepthStencilState(cmd, &depth);
        DkViewport viewport{0, 0, float(width), float(height), 0, 1};
        dkCmdBufSetViewports(cmd, 0, &viewport, 1);
        size_t triangle = 0;
        DkImage* previousTarget = nullptr;
        for (const auto& op : uiCommands) {
            DkImage* destination = op.target ? &op.target->image : swapTargets.image(slot);
            const int targetW = op.target ? op.target->w : int(width);
            const int targetH = op.target ? op.target->h : int(height);
            if (destination != previousTarget) {
                dkCmdBufBarrier(cmd, DkBarrier_Fragments, DkInvalidateFlags_Image);
                DkImageView targetView;
                dkImageViewDefaults(&targetView, destination);
                dkCmdBufBindRenderTarget(cmd, &targetView, nullptr);
                DkViewport targetViewport{0,0,float(targetW),float(targetH),0,1};
                dkCmdBufSetViewports(cmd, 0, &targetViewport, 1);
                previousTarget = destination;
            }
            if (op.target) uiFrameImages[slot].push_back(op.target);
            if (op.clear) {
                DkScissor full{0,0,unsigned(targetW),unsigned(targetH)};
                dkCmdBufSetScissors(cmd, 0, &full, 1);
                dkCmdBufClearColorFloat(cmd, 0, DkColorMask_RGBA, op.color.r, op.color.g, op.color.b, op.color.a);
                continue;
            }
            SDL_Rect clipped = op.viewport;
            if (op.clipEnabled) {
                SDL_Rect absolute = op.clip;
                absolute.x += op.viewport.x; absolute.y += op.viewport.y;
                if (!SDL_GetRectIntersection(&clipped, &absolute, &clipped)) continue;
            }
            SDL_Rect screen{0, 0, targetW, targetH};
            if (!SDL_GetRectIntersection(&clipped, &screen, &clipped)) continue;
            DkScissor scissor{uint32_t(clipped.x), uint32_t(clipped.y), uint32_t(clipped.w), uint32_t(clipped.h)};
            dkCmdBufSetScissors(cmd, 0, &scissor, 1);
            DkColorState color;
            dkColorStateDefaults(&color);
            color.blendEnableMask = op.blend == SDL_BLENDMODE_NONE ? 0 : 1;
            dkCmdBufBindColorState(cmd, &color);
            DkBlendState blend;
            dkBlendStateDefaults(&blend);
            blend.dstAlphaBlendFactor = DkBlendFactor_InvSrcAlpha;
            if (op.blend==SDL_BLENDMODE_ADD) {
                blend.srcColorBlendFactor=DkBlendFactor_SrcAlpha; blend.dstColorBlendFactor=DkBlendFactor_One;
                blend.srcAlphaBlendFactor=DkBlendFactor_Zero; blend.dstAlphaBlendFactor=DkBlendFactor_One;
            }
            dkCmdBufBindBlendState(cmd, 0, &blend);
            Uint32 descriptor = op.borrowedScene ? slot + 1 : (op.texture ? op.texture->descriptor : 0);
            if (op.borrowedImage) {
                const auto& image=op.borrowedImage->source;
                if (!image.descriptor(image.userdata,slot,&descriptor) || descriptor>=descriptorCount)
                    throw "Borrowed image descriptor unavailable";
                uiFrameBorrowed[slot].push_back(op.borrowedImage);
            }
            const DkResHandle handle = dkMakeTextureHandle(descriptor,
                op.scale == SDL_SCALEMODE_LINEAR ? 1 : 0);
            dkCmdBufBindTextures(cmd, DkStage_Fragment, 0, &handle, 1);
            if (op.texture) uiFrameImages[slot].push_back(op.texture);
            for (size_t first = 0; first < op.vertices.size(); first += 3) {
                if (triangle >= uiCapacity[slot] / 256) throw "SDL UI uniform pool exhausted";
                float values[28]{};
                values[24] = (op.texture || op.borrowedScene || op.borrowedImage) ? 1.0f : 0.0f;
                for (unsigned v = 0; v < 3; ++v) {
                    const auto& vertex = op.vertices[first + v];
                    values[v * 4] = 2 * (vertex.x + op.viewport.x) / targetW - 1;
                    values[v * 4 + 1] = 1 - 2 * (vertex.y + op.viewport.y) / targetH;
                    values[v * 4 + 2] = vertex.u;
                    values[v * 4 + 3] = vertex.v;
                    values[12 + v * 4] = vertex.color.r;
                    values[13 + v * 4] = vertex.color.g;
                    values[14 + v * 4] = vertex.color.b;
                    values[15 + v * 4] = vertex.color.a;
                }
                const DkGpuAddr uniform = dkMemBlockGetGpuAddr(uiUniforms[slot]) + triangle++ * 256;
                dkCmdBufPushConstants(cmd, uniform, 256, 0, sizeof(values), values);
                dkCmdBufBindUniformBuffer(cmd, DkStage_Vertex, 0, uniform, 256);
                dkCmdBufDraw(cmd, DkPrimitive_Triangles, 3, 1, 0, 0);
            }
        }
        uiPeakTriangles=std::max(uiPeakTriangles,size_t(triangle));
        const bool visible = triangle != 0;
        if (!uiReported || visible != uiLastVisible) {
            if (diagnosticLog) {
                std::fprintf(diagnosticLog, "SDL UI triangles submitted: %zu (culling disabled)\n", triangle);
                std::fflush(diagnosticLog);
            }
            uiReported = true; uiLastVisible = visible;
        }
        dkQueueSubmitCommands(queue, dkCmdBufFinishList(cmd));
        dkQueueSignalFence(queue, &uiFence[slot], false);
        uiInFlight[slot] = true;
        uiCommands.clear();
    }
    GT3R_DekoBackend backend() {
        GT3R_DekoBackend result{};
        result.version = GT3R_DEKO_BACKEND_VERSION;
        result.userdata = this;
        result.supports_blend = [](void*, SDL_BlendMode b) { return b == SDL_BLENDMODE_NONE || b == SDL_BLENDMODE_BLEND || b == SDL_BLENDMODE_ADD; };
        result.create_texture = [](void* state, int w, int h, int access, SDL_PropertiesID props, void** out) {
            if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
                return SDL_SetError("Deko SDL: unsupported texture dimensions/access");
            auto& self = *static_cast<GraphicsState*>(state);
            try {
                auto texture = std::make_unique<UiTexture>();
                texture->w = w; texture->h = h;
                texture->target = access == SDL_TEXTUREACCESS_TARGET;
                texture->borrowedScene = SDL_GetBooleanProperty(props, GT3R_DEKO_BORROW_SCENE_PROPERTY, false);
                const auto* image=static_cast<const GT3R_DekoImage*>(SDL_GetPointerProperty(props,GT3R_DEKO_BORROW_IMAGE_PROPERTY,nullptr));
                if (image) {
                    if (texture->borrowedScene || access!=SDL_TEXTUREACCESS_STATIC)
                        return SDL_SetError("Deko SDL: borrowed image requires static access");
                    texture->borrowedImage=BorrowedImage::acquire(*image,state,w,h);
                    if (!texture->borrowedImage) return false;
                } else if (texture->borrowedScene) {
                    if (access != SDL_TEXTUREACCESS_STATIC || w != int(width) || h != int(height))
                        return SDL_SetError("Deko SDL: invalid borrowed scene dimensions/access");
                } else {
                    texture->pixels.resize(size_t(w) * h * 4, 0);
                    texture->image = self.upload_ui_texture(*texture);
                }
                *out = texture.get(); self.uiTextures.push_back(std::move(texture));
                return true;
            } catch (const char* message) { return SDL_SetError("%s", message); }
        };
        result.update_texture = [](void* state, void* object, const SDL_Rect* rect, const void* pixels, int pitch) {
            auto& self = *static_cast<GraphicsState*>(state);
            auto& texture = *static_cast<UiTexture*>(object);
            if (texture.borrowedScene || texture.borrowedImage) return SDL_SetError("Deko SDL: borrowed scene cannot be CPU-updated");
            if (texture.target) return SDL_SetError("Deko SDL: use rendering commands to update render targets");
            if (!pixels || pitch < rect->w * 4 || rect->x < 0 || rect->y < 0 ||
                rect->w <= 0 || rect->h <= 0 || rect->x + rect->w > texture.w || rect->y + rect->h > texture.h)
                return SDL_SetError("Deko SDL: invalid texture update");
            auto copy = texture.pixels;
            for (int y = 0; y < rect->h; ++y)
                std::memcpy(copy.data() + (size_t(rect->y + y) * texture.w + rect->x) * 4,
                    static_cast<const uint8_t*>(pixels) + size_t(y) * pitch, size_t(rect->w) * 4);
            texture.pixels.swap(copy);
            try { texture.image = self.upload_ui_texture(texture); return true; }
            catch (const char* message) { texture.pixels.swap(copy); return SDL_SetError("%s", message); }
        };
        result.destroy_texture = [](void* state, void* object) {
            auto& self = *static_cast<GraphicsState*>(state);
            auto it = std::find_if(self.uiTextures.begin(), self.uiTextures.end(),
                [object](const auto& texture) { return texture.get() == object; });
            if (it != self.uiTextures.end()) self.uiTextures.erase(it);
            // Image versions remain owned by pending commands/frame references.
        };
        result.set_target = [](void* state, void* object) {
            auto& self = *static_cast<GraphicsState*>(state);
            auto* texture = static_cast<UiTexture*>(object);
            if (texture && !texture->target) return SDL_SetError("Deko SDL: texture is not a render target");
            self.uiCurrentTarget = texture ? texture->image : nullptr;
            return true;
        };
        result.clear = [](void* state, const SDL_FColor* c) {
            UiCommand op; op.clear = true; op.color = *c;
            op.target = static_cast<GraphicsState*>(state)->uiCurrentTarget;
            static_cast<GraphicsState*>(state)->uiCommands.push_back(std::move(op));
            return true;
        };
        result.draw = [](void* state, void* texture, const GT3R_DekoVertex* vertices, size_t count,
                const SDL_Rect* viewport, const SDL_Rect* clip, SDL_BlendMode blend,
                SDL_ScaleMode scale, SDL_TextureAddressMode addressU, SDL_TextureAddressMode addressV) {
            if (count % 3) return SDL_SetError("Deko SDL milestone: expected solid triangles");
            if (texture && (addressU == SDL_TEXTURE_ADDRESS_WRAP || addressV == SDL_TEXTURE_ADDRESS_WRAP))
                return SDL_SetError("Deko SDL: repeating textures not implemented");
            UiCommand op;
            op.target = static_cast<GraphicsState*>(state)->uiCurrentTarget;
            if (texture) {
                const auto& source = *static_cast<UiTexture*>(texture);
                op.texture = source.image; op.borrowedScene = source.borrowedScene; op.borrowedImage = source.borrowedImage;
            }
            if (op.texture && op.texture == op.target) return SDL_SetError("Deko SDL: cannot sample the active render target");
            op.scale = scale;
            op.vertices.assign(vertices, vertices + count); op.viewport = *viewport;
            op.clipEnabled = clip != nullptr; if (clip) op.clip = *clip;
            op.blend = blend;
            static_cast<GraphicsState*>(state)->uiCommands.push_back(std::move(op));
            return true;
        };
        result.present = [](void* state) { static_cast<GraphicsState*>(state)->present(); return true; };
        return result;
    }
#endif
    ~GraphicsState() {
#ifdef GT3R_DEKO_MODEL
        if (physics) {
            char line[384]; std::snprintf(line,sizeof(line),"Physics session: ticks=%llu lap_counter=%d finished=%d configured_laps=0 max_speed_kmh=%.2f last_speed_kmh=%.2f position=(%.2f %.2f %.2f)",
                (unsigned long long)physics->ticks(),physics->lap_counter(),int(physics->finished()),maximumSpeed,physics->state().speed_kmh,physics->state().position.x,physics->state().position.y,physics->state().position.z); trace(line);
            for (unsigned car=1;car<physics->car_count();++car) {
                const auto& s=physics->state(car);
                std::snprintf(line,sizeof(line),"Opponent state: car=%u max_speed_kmh=%.2f last_speed_kmh=%.2f brake=%.2f position=(%.2f %.2f %.2f)",car,maximumCarSpeeds[car],s.speed_kmh,physics->brake_factor(car),s.position.x,s.position.y,s.position.z); trace(line);
            }
        }
#endif
#ifdef GT3R_DEKO_MODEL
        if (streetGlowFrames) {
            char line[160]; std::snprintf(line,sizeof(line),"Street glows: frames=%llu triangles=%llu enabled=%d visibility_interval=2",
                (unsigned long long)streetGlowFrames,(unsigned long long)streetGlowTriangles,int(streetGlowsEnabled)); trace(line);
        }
        if (glowFrames) {
            char line[160]; std::snprintf(line,sizeof(line),"Car glows: frames=%llu triangles=%llu CPU course/body ray occlusion enabled",
                (unsigned long long)glowFrames,(unsigned long long)glowTriangles); trace(line);
        }
#endif
        trace("Frame timings are CPU wall times; cadence includes pacing, not GPU execution time");
        for (unsigned mode=0; mode<2; ++mode) {
            const char* name=mode ? "materials" : "generic";
            const auto& stats=frameTiming[mode];
            stats.acquire.report("acquire",name); stats.scene.report("scene_submit",name);
            stats.ui.report("compose_ui",name); stats.present.report("queue_present",name);
            stats.total.report("native_present_total",name); stats.cadence.report("present_cadence",name);
        }
        if (queue) {
            if (!dkQueueIsInErrorState(queue)) dkQueueWaitIdle(queue);
#ifdef GT3R_DEKO_MODEL
            model.reset();
#endif
#ifdef GT3R_DEKO_SDL
            if (diagnosticLog) {
                std::fprintf(diagnosticLog, "SDL resource summary: uploads=%zu retired=%zu live=%zu peak=%zu peak_triangles=%zu\n",
                    uiUploadCount, uiRetiredCount, uiImages.size(), uiPeakImages, uiPeakTriangles);
                std::fflush(diagnosticLog);
            }
            uiCommands.clear(); uiCurrentTarget.reset(); uiTextures.clear();
            for (auto& refs : uiFrameImages) refs.clear();
            for (auto& refs : uiFrameBorrowed) refs.clear();
            uiImages.clear();
#endif
            dkQueueDestroy(queue);
        }
#ifdef GT3R_DEKO_SDL
        for (unsigned i = 0; i < frames; ++i) {
            if (uiBuffer[i]) dkCmdBufDestroy(uiBuffer[i]);
            if (uiMemory[i]) dkMemBlockDestroy(uiMemory[i]);
            if (uiUniforms[i]) dkMemBlockDestroy(uiUniforms[i]);
        }
#endif
        if (commands) dkCmdBufDestroy(commands);
        if (swapchain) dkSwapchainDestroy(swapchain);
        if (commandsMemory) dkMemBlockDestroy(commandsMemory);
        if (codeMemory) dkMemBlockDestroy(codeMemory);
        swapTargets.reset();
        depthTargets.reset();
        if (indexMemory) dkMemBlockDestroy(indexMemory);
        if (textureMemory) dkMemBlockDestroy(textureMemory);
        if (uploadMemory) dkMemBlockDestroy(uploadMemory);
        if (descriptorMemory) dkMemBlockDestroy(descriptorMemory);
        sceneTargets.reset();
        if (device) dkDeviceDestroy(device);
    }
    DkMemBlock allocate(uint32_t size, uint32_t flags) {
        DkMemBlockMaker m;
        dkMemBlockMakerDefaults(&m, device, size);
        m.flags = flags | DkMemBlockFlags_ZeroFillInit;
        auto block = dkMemBlockCreate(&m);
        if (!block) throw "GPU allocation failed";
        return block;
    }
    void shader(unsigned index, const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) throw "Cannot open shader in romfs";
        if (std::fseek(f, 0, SEEK_END)) { std::fclose(f); throw "Cannot seek shader"; }
        const long length = std::ftell(f);
        std::rewind(f);
        if (length <= 0 || uint64_t(codeOffset) + uint64_t(length) > codeSize) {
            std::fclose(f); throw "Invalid shader size";
        }
        auto* dst = static_cast<uint8_t*>(dkMemBlockGetCpuAddr(codeMemory)) + codeOffset;
        const bool ok = std::fread(dst, 1, size_t(length), f) == size_t(length);
        std::fclose(f);
        if (!ok) throw "Incomplete shader read";
        DkShaderMaker maker;
        dkShaderMakerDefaults(&maker, codeMemory, codeOffset);
        dkShaderInitialize(&shaders[index], &maker);
        codeOffset += (uint32_t(length) + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
    }
    void init() {
        DkDeviceMaker dm;
        dkDeviceMakerDefaults(&dm);
        dm.cbDebug = debugMessage;
        device = dkDeviceCreate(&dm);
        if (!device) throw "Device creation failed";
        trace("device created");
        DkImageLayoutMaker lm;
        DkImageLayout layout;
        swapTargets.create(device,width,height,frames,DkImageFormat_RGBA8_Unorm,
            DkImageFlags_UsageRender|DkImageFlags_UsagePresent|DkImageFlags_HwCompression);
        const DkImage* swapImages[frames];
        for (unsigned i=0;i<frames;++i) swapImages[i]=swapTargets.image(i);
        DkSwapchainMaker sm;
        dkSwapchainMakerDefaults(&sm, device, nwindowGetDefault(), swapImages, frames);
        swapchain = dkSwapchainCreate(&sm);
        if (!swapchain) throw "Swapchain creation failed";
        trace("swapchain created");
        // Each swapchain slot owns a separate sampled scene target. There is no
        // CPU image copy and no resource mutation while GPU work is in flight.
        sceneTargets.create(device,width,height,frames,DkImageFormat_RGBA8_Unorm);
        // Preserve the validated reversed-depth format during extraction.
        depthTargets.create(device,width,height,frames,DkImageFormat_Z24X8);
        char imageSummary[256];
        std::snprintf(imageSummary,sizeof(imageSummary),
            "Native targets: size=%ux%u slots=%u swap_bytes=%llu scene_bytes=%llu depth_bytes=%llu",
            sceneTargets.width(),sceneTargets.height(),unsigned(sceneTargets.count()),
            (unsigned long long)swapTargets.bytes(),(unsigned long long)sceneTargets.bytes(),
            (unsigned long long)depthTargets.bytes());
        trace(imageSummary);
        indexMemory = allocate(DK_MEMBLOCK_ALIGNMENT, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        // Non-sequential vertex IDs exercise the actual index buffer.
        const uint16_t indices[]{2, 0, 1, 5, 3, 4, 6, 7, 8, 8, 7, 9};
        std::memcpy(dkMemBlockGetCpuAddr(indexMemory), indices, sizeof(indices));
        codeMemory = allocate(codeSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
        shader(0, "romfs:/shaders/probe.vert.dksh");
        shader(1, "romfs:/shaders/probe.frag.dksh");
        shader(2, "romfs:/shaders/compose.vert.dksh");
        shader(3, "romfs:/shaders/compose.frag.dksh");
#ifdef GT3R_DEKO_SDL
        shader(4, "romfs:/shaders/ui.vert.dksh");
        shader(5, "romfs:/shaders/ui.frag.dksh");
        for (unsigned i = 0; i < frames; ++i) {
            uiMemory[i] = allocate(uiBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            uiUniforms[i] = allocate(uiBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            DkCmdBufMaker maker;
            dkCmdBufMakerDefaults(&maker, device);
            uiBuffer[i] = dkCmdBufCreate(&maker);
            if (!uiBuffer[i]) throw "SDL command buffer creation failed";
        }
#endif
#ifdef GT3R_DEKO_MODEL
        shader(6, "romfs:/shaders/model.vert.dksh");
        shader(7, "romfs:/shaders/model.frag.dksh");
        shader(8, "romfs:/shaders/capture.frag.dksh");
#endif
        trace("shaders loaded");
        commandsMemory = allocate(commandSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        DkCmdBufMaker cm;
        dkCmdBufMakerDefaults(&cm, device);
        commands = dkCmdBufCreate(&cm);
        if (!commands) throw "Command buffer creation failed";
        dkCmdBufAddMemory(commands, commandsMemory, 0, commandSize);
        // Upload a genuine linear RGBA image into a GPU block-linear texture.
        constexpr uint32_t textureSize = 64;
        dkImageLayoutMakerDefaults(&lm, device);
        lm.format = DkImageFormat_RGBA8_Unorm;
        lm.dimensions[0] = lm.dimensions[1] = textureSize;
        dkImageLayoutInitialize(&layout, &lm);
        const uint32_t textureBytes = uint32_t(dkImageLayoutGetSize(&layout));
        textureMemory = allocate((textureBytes + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1),
                                 DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        dkImageInitialize(&texture, &layout, textureMemory, 0);
        uploadMemory = allocate(textureSize * textureSize * 4, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        auto* pixels = static_cast<uint8_t*>(dkMemBlockGetCpuAddr(uploadMemory));
        for (unsigned y = 0; y < textureSize; ++y) {
            for (unsigned x = 0; x < textureSize; ++x) {
                uint8_t* p = pixels + 4 * (y * textureSize + x);
                const uint8_t checker = ((x / 8 + y / 8) % 2) ? 230 : 65;
                p[0] = p[1] = p[2] = checker; p[3] = 255;
                // Asymmetric corner markers reveal flips and channel mistakes.
                if ((x < 12 || x >= 52) && (y < 12 || y >= 52)) {
                    p[0] = y < 12 ? (x < 12 ? 255 : 0) : (x < 12 ? 0 : 255);
                    p[1] = x >= 52 ? 255 : 0;
                    p[2] = y >= 52 && x < 12 ? 255 : 0;
                }
            }
        }
        DkImageView textureView;
        dkImageViewDefaults(&textureView, &texture);
        dkCmdBufBarrier(commands, DkBarrier_None, DkInvalidateFlags_L2Cache);
        // Zero pitch/layer stride requests tightly packed data. DkCopyBuf uses
        // BYTE strides (not pixel counts): this RGBA row contains 64 * 4 bytes.
        DkCopyBuf source{dkMemBlockGetGpuAddr(uploadMemory), 0, 0};
        DkImageRect area{0, 0, 0, textureSize, textureSize, 1};
        dkCmdBufCopyBufferToImage(commands, &source, &textureView, &area, 0);
        dkCmdBufBarrier(commands, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors);
        upload = dkCmdBufFinishList(commands);
        trace("texture upload recorded (RGBA8, 64x64, tightly packed)");
        descriptorMemory = allocate(2 * DK_MEMBLOCK_ALIGNMENT, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        auto* descriptors = static_cast<uint8_t*>(dkMemBlockGetCpuAddr(descriptorMemory));
        DkImageDescriptor imageDescriptor;
        dkImageDescriptorInitialize(&imageDescriptor, &textureView, false, false);
        std::memcpy(descriptors, &imageDescriptor, sizeof(imageDescriptor));
        for (unsigned i = 0; i < frames; ++i) {
            DkImageView sceneView;
            dkImageViewDefaults(&sceneView, sceneTargets.image(i));
            dkImageDescriptorInitialize(&imageDescriptor, &sceneView, false, false);
            std::memcpy(descriptors + (i + 1) * sizeof(imageDescriptor), &imageDescriptor, sizeof(imageDescriptor));
        }
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        sampler.minFilter = sampler.magFilter = DkFilter_Nearest;
        sampler.wrapMode[0] = sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
        DkSamplerDescriptor samplerDescriptor;
        dkSamplerDescriptorInitialize(&samplerDescriptor, &sampler);
        std::memcpy(descriptors + samplerOffset, &samplerDescriptor, sizeof(samplerDescriptor));
        sampler.minFilter = sampler.magFilter = DkFilter_Linear;
        sampler.wrapMode[0] = sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
        dkSamplerDescriptorInitialize(&samplerDescriptor, &sampler);
        std::memcpy(descriptors + samplerOffset + sizeof(samplerDescriptor), &samplerDescriptor, sizeof(samplerDescriptor));
        trace("texture descriptors initialized");
        for (unsigned i = 0; i < frames; ++i) {
            DkImageView view;
            DkImageView depthView;
            dkImageViewDefaults(&view, sceneTargets.image(i));
            dkImageViewDefaults(&depthView, depthTargets.image(i));
            dkCmdBufBindRenderTarget(commands, &view, &depthView);
            bind[i] = dkCmdBufFinishList(commands);
        }
        DkViewport viewport{0, 0, float(width), float(height), 0, 1};
        DkScissor scissor{0, 0, width, height};
        DkRasterizerState raster;
        DkColorState color;
        DkColorWriteState write;
        DkDepthStencilState depth;
        dkRasterizerStateDefaults(&raster);
        dkColorStateDefaults(&color);
        dkColorWriteStateDefaults(&write);
        dkDepthStencilStateDefaults(&depth);
        depth.depthCompareOp = DkCompareOp_Gequal;
        const DkShader* stages[]{&shaders[0], &shaders[1]};
        dkCmdBufSetViewports(commands, 0, &viewport, 1);
        dkCmdBufSetScissors(commands, 0, &scissor, 1);
        dkCmdBufClearColorFloat(commands, 0, DkColorMask_RGBA, 0.025f, 0.04f, 0.08f, 1);
        dkCmdBufClearDepthStencil(commands, true, 0.0f, 0, 0);
        dkCmdBufBindShaders(commands, DkStageFlag_GraphicsMask, stages, 2);
        dkCmdBufBindRasterizerState(commands, &raster);
        dkCmdBufBindColorState(commands, &color);
        dkCmdBufBindColorWriteState(commands, &write);
        dkCmdBufBindDepthStencilState(commands, &depth);
        dkCmdBufBindIdxBuffer(commands, DkIdxFormat_Uint16, dkMemBlockGetGpuAddr(indexMemory));
        dkCmdBufBindImageDescriptorSet(commands, dkMemBlockGetGpuAddr(descriptorMemory), descriptorCount);
        dkCmdBufBindSamplerDescriptorSet(commands, dkMemBlockGetGpuAddr(descriptorMemory) + samplerOffset, 2);
        const DkResHandle tex = dkMakeTextureHandle(0, 0);
        dkCmdBufBindTextures(commands, DkStage_Fragment, 0, &tex, 1);
        // Near coloured triangle first, larger yellow triangle behind it second.
        dkCmdBufDrawIndexed(commands, DkPrimitive_Triangles, 12, 1, 0, 0, 0);
        draw = dkCmdBufFinishList(commands);
        trace("offscreen scene commands recorded");
        const DkShader* compositeStages[]{&shaders[2], &shaders[3]};
        depth.depthTestEnable = false;
        depth.depthWriteEnable = false;
        // Command lists share GPU state. In particular, the model's final
        // reflection pass leaves additive blending enabled. Presentation must
        // overwrite the swapchain rather than add its diagnostic clear colour.
        DkColorState compositeColor;
        dkColorStateDefaults(&compositeColor);
        compositeColor.blendEnableMask = 0;
        DkBlendState compositeBlend;
        dkBlendStateDefaults(&compositeBlend);
        DkRasterizerState compositeRaster;
        dkRasterizerStateDefaults(&compositeRaster);
        compositeRaster.cullMode = DkFace_None;
        for (unsigned i = 0; i < frames; ++i) {
            // GPU-only dependency: finish scene fragments, invalidate the texture
            // cache before reading that render target through the sampler.
            dkCmdBufBarrier(commands, DkBarrier_Fragments, DkInvalidateFlags_Image);
            DkImageView target;
            dkImageViewDefaults(&target, swapTargets.image(i));
            dkCmdBufBindRenderTarget(commands, &target, nullptr);
            dkCmdBufClearColorFloat(commands, 0, DkColorMask_RGBA, 0.12f, 0.03f, 0.18f, 1);
            dkCmdBufSetViewports(commands, 0, &viewport, 1);
            dkCmdBufSetScissors(commands, 0, &scissor, 1);
            dkCmdBufBindRasterizerState(commands, &compositeRaster);
            dkCmdBufBindColorState(commands, &compositeColor);
            dkCmdBufBindBlendState(commands, 0, &compositeBlend);
            dkCmdBufBindColorWriteState(commands, &write);
            dkCmdBufBindDepthStencilState(commands, &depth);
            dkCmdBufBindShaders(commands, DkStageFlag_GraphicsMask, compositeStages, 2);
            dkCmdBufBindImageDescriptorSet(commands, dkMemBlockGetGpuAddr(descriptorMemory), descriptorCount);
            dkCmdBufBindSamplerDescriptorSet(commands, dkMemBlockGetGpuAddr(descriptorMemory) + samplerOffset, 2);
            const DkResHandle sceneHandle = dkMakeTextureHandle(i + 1, 0);
            dkCmdBufBindTextures(commands, DkStage_Fragment, 0, &sceneHandle, 1);
            dkCmdBufDraw(commands, DkPrimitive_Triangles, 6, 1, 0, 0);
            compose[i] = dkCmdBufFinishList(commands);
        }
        DkBlendState blend;
        dkBlendStateDefaults(&blend);
        // Straight alpha for colour, standard coverage accumulation for alpha.
        blend.dstAlphaBlendFactor = DkBlendFactor_InvSrcAlpha;
        color.blendEnableMask = 1;
        dkCmdBufBindColorState(commands, &color);
        dkCmdBufBindBlendState(commands, 0, &blend);
        dkCmdBufDraw(commands, DkPrimitive_Triangles, 6, 1, 6, 0);
        overlay = dkCmdBufFinishList(commands);
        trace("scene composition and transparent overlay commands recorded");
        DkQueueMaker qm;
        dkQueueMakerDefaults(&qm, device);
        qm.flags = DkQueueFlags_Graphics;
        queue = dkQueueCreate(&qm);
        if (!queue) throw "Queue creation failed";
        trace("graphics queue created");
        dkQueueSubmitCommands(queue, upload);
        // Next batch invalidates texture/L2 caches before sampling the uploaded image.
        dkQueueFlush(queue);
        trace("texture upload submitted");
#ifdef GT3R_DEKO_MODEL
        model = std::make_unique<Model>(device, queue, &shaders[6], &shaders[7], &shaders[8]);
        FILE* file=std::fopen("sdmc:/switch/gt3r-deko-sdl-probe/route5-physics.gtds","rb");
        if (!file) throw "Missing private route5-physics.gtds bundle";
        if (std::fseek(file,0,SEEK_END)) { std::fclose(file); throw "Physics bundle seek failed"; }
        const long size=std::ftell(file); std::rewind(file);
        if (size<=0 || size>128*1024*1024) { std::fclose(file); throw "Invalid physics bundle size"; }
        physicsBytes.resize(size_t(size));
        const bool read=std::fread(physicsBytes.data(),1,physicsBytes.size(),file)==physicsBytes.size(); std::fclose(file);
        if (!read) throw "Physics bundle read failed";
        trace("ExactRace initialization begin");
        physics=std::make_unique<Session>(physicsBytes,6);
        model->load("sdmc:/switch/gt3r-deko-sdl-probe/route5-061.gtdk",physics->course_bytes());
        model->load_course_animations(physics->course_bytes());
        model->configure_cars(physics->car_count());
        model->prepare_dynamic_resources();
        carLamps=race::CarLampSet::parse(CarFile::parse(physics->car_bytes()).info);
        CarModelOptions bodyOptions; bodyOptions.wheels=false; bodyOptions.reflection=false;
        carBodyOccluder.add_opaque_body(load_car_model(physics->car_bytes(),bodyOptions,"lamp_blocker")); carBodyOccluder.build(1);
        initialVehicle=previousPhysics=physics->state(); vehicleSetup=physics->setup();
        for (unsigned car=0;car<physics->car_count();++car) previousCars[car]=physics->state(car);
        model->set_environment_origin({initialVehicle.position.x,initialVehicle.position.y+1,initialVehicle.position.z});
        raceCamera.set_mode(race::CameraMode::ChaseFar); raceCamera.set_shake(false); raceCamera.snap(initialVehicle);
        trace("ExactRace initialized: 1 player + 5 AI Mazda 787B, player grid slot 5, fixed 60 Hz");
        trace("Full Route 5 and Mazda 787B loaded (base geometry/textures)");
#endif
    }
    void prepare_frame() {
        phases={};
        const auto prepareBegin=TimingClock::now();
#ifdef GT3R_DEKO_MODEL
        const double seconds = double(armTicksToNs(armGetSystemTick())) / 1000000000.0;
        const double dt=previousVehicleTime>0 ? std::clamp(seconds-previousVehicleTime,0.0,0.05) : 1.0/60;
        previousVehicleTime=seconds;
        if (resetPhysicsRequested) {
            phases.reset=true;
            physics=std::make_unique<Session>(physicsBytes,6); vehicleSetup=physics->setup();
            for (unsigned car=0;car<physics->car_count();++car) previousCars[car]=physics->state(car);
            previousPhysics=physics->state(); physicsStep.reset(); model->reset_course_animations(); raceCamera.snap(previousPhysics);
            resetPhysicsRequested=false; trace("Physics reset to standing grid");
        }
        const int ticks=physicsStep.advance(dt,3); preparedTicks=unsigned(ticks);
        phases.ticks=unsigned(ticks);
        for (int tick=0;tick<ticks;++tick) {
            const auto begin=TimingClock::now();
            previousPhysics=physics->state();
            for (unsigned car=0;car<physics->car_count();++car) previousCars[car]=physics->state(car);
            physics->tick(drivingInput);
            const auto physicsEnd=TimingClock::now();
            if (courseAnimations) model->animate_course(float(physicsStep.step()));
            phases.physics+=elapsed_ms(begin,physicsEnd);
            phases.animation+=elapsed_ms(physicsEnd,TimingClock::now());
        }
        const auto vehicleBegin=TimingClock::now();
        for (unsigned car=0;car<physics->car_count();++car) {
            const auto& setup=physics->setup(car);
            auto& state=renderCars[car]; state=race::interpolate(previousCars[car],physics->state(car),physicsStep.alpha());
            maximumCarSpeeds[car]=std::max(maximumCarSpeeds[car],physics->state(car).speed_kmh);
            model->set_brake_factor(physics->brake_factor(car),physics->brake_now(car),car);
            model->update_car_lights(state,physics->lamp_hint(car),car);
            if (!model->apply_vehicle_state(state,setup,car)) throw "VehicleState instance transform bridge failed";
            const auto& q=state.orientation;
            bodyPoses[car]=render::translate({state.position.x,state.position.y,state.position.z})*render::rotate_quat(q.w,q.x,q.y,q.z)*render::translate({0,car_body_lift(setup),0});
            bodyInverses[car]=render::inverse(bodyPoses[car]);
        }
        const auto& vehicle=renderCars[0];
        maximumSpeed=std::max(maximumSpeed,physics->state().speed_kmh);
        if (followCamera) {
            const gt3r::PadState cameraPad{};
            raceCamera.update(vehicle,vehicleSetup,cameraPad,dt);
            if (!model->set_camera(raceCamera.view(),raceCamera.projection(float(width)/height),raceCamera.eye())) throw "CameraRig bridge failed";
        } else model->clear_external_camera();
        objectDemoWasActive=objectDemo;
        if (vertexDemo || vertexDemoWasActive) model->demo_vertex_offset(vertexDemo ? 0.4f*float(std::sin(seconds*2.0)) : 0.0f);
        vertexDemoWasActive=vertexDemo;
        if (textureDemo || textureDemoWasActive) model->demo_texture_color(textureDemo ? float(std::fmod(seconds,100.0)) : -1.0f);
        textureDemoWasActive=textureDemo;
        renderVehicle=vehicle; frameSeconds=seconds; framePrepared=true;
        phases.vehicle=elapsed_ms(vehicleBegin,TimingClock::now());
#endif
        phases.prepare=elapsed_ms(prepareBegin,TimingClock::now());
    }
    void present() {
        const auto begin=TimingClock::now();
        const int slot = dkQueueAcquireImage(queue, swapchain);
        if (slot < 0 || slot >= int(frames)) throw "Invalid swapchain slot";
        const auto acquired=TimingClock::now();
#ifdef GT3R_DEKO_MODEL
        if (!framePrepared) prepare_frame();
        const auto captureBegin=TimingClock::now();
        model->update_environment(unsigned(slot),{renderVehicle.position.x,renderVehicle.position.y+1,renderVehicle.position.z});
        phases.capture=elapsed_ms(captureBegin,TimingClock::now());
#endif
        dkQueueSubmitCommands(queue, bind[slot]);
#ifdef GT3R_DEKO_MODEL
        model->submit(unsigned(slot), float(std::fmod(frameSeconds * 0.25, 6.283185307179586)), width, height);
        const auto submission=model->submission_timing();
        phases.modelWait=submission.wait; phases.modelRecord=submission.record;
        framePrepared=false;
#else
        dkQueueSubmitCommands(queue, draw);
#endif
        const auto sceneEnd=TimingClock::now();
        dkQueueSubmitCommands(queue, compose[slot]);
        if (showOverlay) dkQueueSubmitCommands(queue, overlay);
#ifdef GT3R_DEKO_SDL
        record_ui(unsigned(slot));
#endif
        const auto uiEnd=TimingClock::now();
        dkQueuePresentImage(queue, swapchain, slot);
        const auto end=TimingClock::now();
        phases.acquire=elapsed_ms(begin,acquired);
        phases.scene=elapsed_ms(acquired,sceneEnd);
        phases.ui=elapsed_ms(sceneEnd,uiEnd);
        phases.present=elapsed_ms(uiEnd,end);
        const unsigned mode=materialMode ? 1 : 0;
        auto& stats=frameTiming[mode];
        stats.acquire.add(elapsed_ms(begin,acquired)); stats.scene.add(elapsed_ms(acquired,sceneEnd));
        stats.ui.add(elapsed_ms(sceneEnd,uiEnd)); stats.present.add(elapsed_ms(uiEnd,end));
        stats.total.add(elapsed_ms(begin,end));
        // Attribute each return-to-return interval to the preceding frame mode.
        // Exclude the transition interval, which may include toggle logging.
        if (havePresentTime && previousMode==mode) stats.cadence.add(elapsed_ms(previousPresentTime,end));
        previousPresentTime=end; previousMode=mode; havePresentTime=true;
    }
};
struct Graphics::Impl : GraphicsState {};
Graphics::Graphics() : impl_(std::make_unique<Impl>()) {}
Graphics::~Graphics() = default;
void Graphics::init() { impl_->init(); }
void Graphics::present() { impl_->present(); }
FramePhases Graphics::frame_phases() const { return impl_->phases; }
#ifdef GT3R_DEKO_SDL
void Graphics::move_camera(float f,float s,float t,float p,float l,float dt) {
#ifdef GT3R_DEKO_MODEL
    impl_->model->move_camera(f,s,t,p,l,dt);
#endif
}
void Graphics::capture_environment() {
#ifdef GT3R_DEKO_MODEL
    impl_->model->capture_environment();
    impl_->havePresentTime=false; // report capture pause separately from normal cadence
#endif
}
void Graphics::prepare_frame() { impl_->prepare_frame(); }
void Graphics::look_back(bool on) {
#ifdef GT3R_DEKO_MODEL
    if (impl_->lampFrontView!=on) {
        impl_->lampFrontView=on;
        impl_->raceCamera.set_mode(on ? race::CameraMode::Chase : race::CameraMode::ChaseFar);
        if (on) impl_->followCamera=true;
    }
    impl_->raceCamera.set_look_back(on);
#endif
}
bool Graphics::toggle_street_glows() {
#ifdef GT3R_DEKO_MODEL
    return impl_->streetGlowsEnabled=!impl_->streetGlowsEnabled;
#else
    return false;
#endif
}
bool Graphics::toggle_course_culling() {
#ifdef GT3R_DEKO_MODEL
    return impl_->model->toggle_course_culling();
#else
    return false;
#endif
}
void Graphics::inspect_course_textures() {
#ifdef GT3R_DEKO_MODEL
    impl_->model->inspect_course_textures();
    impl_->havePresentTime=false;
#endif
}
unsigned Graphics::cycle_course_mask_view() {
#ifdef GT3R_DEKO_MODEL
    return impl_->model->cycle_course_mask_view();
#else
    return 0;
#endif
}
bool Graphics::toggle_course_fog() {
#ifdef GT3R_DEKO_MODEL
    return impl_->model->toggle_course_fog();
#else
    return false;
#endif
}
void Graphics::init_street_glows(race::LampGlows& glows,SDL_Renderer* renderer) {
#ifdef GT3R_DEKO_MODEL
    const auto bytes=impl_->physics->course_bytes(); const auto file=course::CourseFile::parse(bytes);
    glows.load(renderer,nullptr,file.section(bytes,29),file.section(bytes,32),file.section(bytes,2),impl_->physics->flare_bytes());
    glows.set_visibility_interval(2);
    char line[128]; std::snprintf(line,sizeof(line),"Street glows initialized: lamps=%zu visibility_interval=2",glows.count()); trace(line);
#endif
}
void Graphics::draw_street_glows(race::LampGlows& glows,SDL_Renderer* renderer) {
#ifdef GT3R_DEKO_MODEL
    // Keep blink time advancing even while the drawing is disabled.
    glows.tick(double(impl_->preparedTicks)/60.0);
    if (!impl_->followCamera || !impl_->streetGlowsEnabled || !glows.count()) return;
    const SDL_FRect rect{float(width)*0.04f,float(height)*0.04f,float(width)*0.92f,float(height)*0.92f};
    render::ScreenBatch batch; batch.rect=rect;
    glows.add(batch,rect,impl_->raceCamera.view(),impl_->raceCamera.eye(),impl_->raceCamera.frustum(rect.w/rect.h),
        [&](const render::Vec3& a,const render::Vec3& b){
            if (impl_->model->segment_blocked(a,b)) return true;
            for (unsigned car=0;car<impl_->physics->car_count();++car) {
                const auto& inverse=impl_->bodyInverses[car];
                if (impl_->carBodyOccluder.segment_blocked(inverse.transform_point(a),inverse.transform_point(b))) return true;
            }
            return false;
        });
    render::submit_2d(renderer,batch); ++impl_->streetGlowFrames; impl_->streetGlowTriangles+=batch.idx.size()/3;
#endif
}
void Graphics::init_car_glows(race::CarLampGlows& glows,SDL_Renderer* renderer) {
#ifdef GT3R_DEKO_MODEL
    const auto bytes=impl_->physics->course_bytes(); const auto file=course::CourseFile::parse(bytes);
    glows.load(renderer,nullptr,file.section(bytes,32),impl_->physics->flare_bytes());
    char line[192]; std::snprintf(line,sizeof(line),"Car glows initialized: active=%d head=%zu tail=%zu brake=%zu body_blocker=%zu triangles",
        int(glows.active()),impl_->carLamps.head.size(),impl_->carLamps.tail.size(),impl_->carLamps.brake.size(),impl_->carBodyOccluder.triangles()); trace(line);
#endif
}
void Graphics::draw_car_glows(race::CarLampGlows& glows,SDL_Renderer* renderer) {
#ifdef GT3R_DEKO_MODEL
    if (!impl_->followCamera || !glows.active()) return;
    const SDL_FRect rect{float(width)*0.04f,float(height)*0.04f,float(width)*0.92f,float(height)*0.92f};
    render::ScreenBatch batch; batch.rect=rect;
    std::vector<race::CarLampGlows::Car> cars; cars.reserve(impl_->physics->car_count());
    for (unsigned car=0;car<impl_->physics->car_count();++car)
        cars.push_back({&impl_->carLamps,impl_->bodyPoses[car],impl_->physics->brake_factor(car),impl_->physics->brake_now(car),&impl_->carBodyOccluder});
    glows.add(batch,rect,impl_->raceCamera.view(),impl_->raceCamera.eye(),impl_->raceCamera.frustum(rect.w/rect.h),cars,
        [&](const render::Vec3& a,const render::Vec3& b){
            if (impl_->model->segment_blocked(a,b)) return true;
            for (unsigned car=0;car<impl_->physics->car_count();++car) {
                const auto& inverse=impl_->bodyInverses[car];
                if (impl_->carBodyOccluder.segment_blocked(inverse.transform_point(a),inverse.transform_point(b))) return true;
            }
            return false;
        });
    render::submit_2d(renderer,batch); ++impl_->glowFrames; impl_->glowTriangles+=batch.idx.size()/3;
#endif
}
void Graphics::driving_input(float throttle,float brake,float steer,bool reset) {
#ifdef GT3R_DEKO_MODEL
    impl_->drivingInput.throttle=throttle; impl_->drivingInput.brake=brake;
    impl_->drivingInput.steer=impl_->followCamera ? steer : 0; impl_->resetPhysicsRequested|=reset;
#endif
}
float Graphics::speed_kmh() const {
#ifdef GT3R_DEKO_MODEL
    return impl_->physics ? impl_->physics->state().speed_kmh : 0;
#else
    return 0;
#endif
}
bool Graphics::toggle_wheel_motion() {
#ifdef GT3R_DEKO_MODEL
    return impl_->model->toggle_wheel_motion();
#else
    return false;
#endif
}
bool Graphics::toggle_point_lighting() {
#ifdef GT3R_DEKO_MODEL
    return impl_->model->toggle_point_lighting();
#else
    return false;
#endif
}
bool Graphics::toggle_course_animations() {
#ifdef GT3R_DEKO_MODEL
    return impl_->courseAnimations=!impl_->courseAnimations;
#else
    return false;
#endif
}
bool Graphics::toggle_race_camera() {
#ifdef GT3R_DEKO_MODEL
    impl_->followCamera=!impl_->followCamera;
    return impl_->followCamera;
#else
    return false;
#endif
}
bool Graphics::toggle_object_demo() { return impl_->objectDemo=!impl_->objectDemo; }
bool Graphics::toggle_texture_demo() { return impl_->textureDemo=!impl_->textureDemo; }
bool Graphics::toggle_vertex_demo() { return impl_->vertexDemo=!impl_->vertexDemo; }
bool Graphics::toggle_reflections() {
#ifdef GT3R_DEKO_MODEL
    return impl_->model->toggle_reflections();
#else
    return false;
#endif
}
bool Graphics::toggle_materials() {
#ifdef GT3R_DEKO_MODEL
    impl_->materialMode=impl_->model->toggle_materials();
    return impl_->materialMode;
#else
    return false;
#endif
}
GT3R_DekoBackend Graphics::sdl_backend() { return impl_->backend(); }
GT3R_DekoImage Graphics::scene_image() {
    return {GT3R_DEKO_IMAGE_VERSION,impl_.get(),impl_.get(),int(width),int(height),
        [](void*) {}, [](void*) {}, // context outlives renderer and frame fences
        [](void*,unsigned slot,Uint32* descriptor) { if(slot>=frames)return false; *descriptor=slot+1; return true; }};
}
#endif
bool Graphics::toggle_overlay() {
    impl_->showOverlay = !impl_->showOverlay;
    return impl_->showOverlay;
}
bool open_log() {
    diagnosticLog = std::fopen("sdmc:/switch/gt3r-deko-probe.log", "w");
    return diagnosticLog != nullptr;
}
void close_log() {
    if (diagnosticLog) { std::fclose(diagnosticLog); diagnosticLog = nullptr; }
}
} // namespace gt3r::deko_probe
