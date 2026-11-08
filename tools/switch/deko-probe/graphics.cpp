// GT3 Revisited Deko3D presentation probe.
// API usage reference: switchbrew/switch-examples, graphics/deko3d/deko_basic.
#include <switch.h>
#include <deko3d.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <mutex>
#include <cmath>

#include "graphics.h"
#ifdef GT3R_DEKO_SDL
#include "render/view3d.h"
#include "render/deko/backend.h"
#endif
#include "render/deko/image_array.h"
#include "render/deko/submission_frame.h"
#include "render/deko/dynamic_model.h"
#include "render/deko/instance_transform.h"
#include "render/deko/scene_state.h"
#include "render/deko/draw_schedule.h"
#include "render/deko/primitive_state.h"
#include "render/deko/read_lease.h"
#include "render/deko/post_grade_lut.h"
#include "render/deko/borrowed_image.h"
#include "timing.h"
#ifdef GT3R_DEKO_MODEL
#include "../deko-model/model.h"
#include "../deko-model/frame_arena.h"
#include "../deko-model/race_profile.h"
#include "../deko-model/clock_reader.h"
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
constexpr unsigned descriptorCount = 8192, samplerOffset = descriptorCount*sizeof(DkImageDescriptor);
FILE* diagnosticLog = nullptr;
bool bufferedNativeTrace=false;
std::mutex nativeTraceMutex;
std::string nativeTraceBuffer;
void flush_native_trace() {
    std::lock_guard lock(nativeTraceMutex);
    if(!nativeTraceBuffer.empty()) {
        auto* output=diagnosticLog ? diagnosticLog : stdout;
        std::fwrite(nativeTraceBuffer.data(),1,nativeTraceBuffer.size(),output);
        std::fflush(output); nativeTraceBuffer.clear();
    }
}
void trace(const char* stage) {
    if(bufferedNativeTrace) {
        std::lock_guard lock(nativeTraceMutex);
        if(nativeTraceBuffer.size()>4*1024*1024) nativeTraceBuffer="[Native diagnostics buffer truncated]\n";
        nativeTraceBuffer+=stage; nativeTraceBuffer+='\n'; return;
    }
    if (diagnosticLog) {
        std::fprintf(diagnosticLog, "%s\n", stage);
        std::fflush(diagnosticLog);
    } else { std::printf("%s\n",stage); std::fflush(stdout); }
}
void debugMessage(void*, const char* context, DkResult result, const char* message) {
    flush_native_trace();
    if (diagnosticLog) {
        std::fprintf(diagnosticLog, "Deko3D result=%u context=%s message=%s\n",
                     unsigned(result), context ? context : "?", message ? message : "?");
        std::fflush(diagnosticLog);
    }
    // Deko3D requires fatal callbacks not to return. Do not attempt to unwind
    // or submit more GPU work after a validation/device failure.
    if (!diagnosticLog) { std::fprintf(stderr,"Deko3D result=%u context=%s message=%s\n",unsigned(result),context ? context : "?",message ? message : "?"); std::fflush(stderr); }
    if (result != DkResult_Success) std::_Exit(1);
}
#ifdef GT3R_DEKO_SDL
bool native_blend(SDL_BlendMode mode,DkBlendState& state) {
    GT3R_DekoBlend b{};
    if(!GT3R_DekoDescribeBlend(mode,&b)) return false;
    auto factor=[](SDL_BlendFactor f) -> DkBlendFactor {
        switch(f) {
        case SDL_BLENDFACTOR_ZERO:return DkBlendFactor_Zero;
        case SDL_BLENDFACTOR_ONE:return DkBlendFactor_One;
        case SDL_BLENDFACTOR_SRC_COLOR:return DkBlendFactor_SrcColor;
        case SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR:return DkBlendFactor_InvSrcColor;
        case SDL_BLENDFACTOR_SRC_ALPHA:return DkBlendFactor_SrcAlpha;
        case SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA:return DkBlendFactor_InvSrcAlpha;
        case SDL_BLENDFACTOR_DST_COLOR:return DkBlendFactor_DstColor;
        case SDL_BLENDFACTOR_ONE_MINUS_DST_COLOR:return DkBlendFactor_InvDstColor;
        case SDL_BLENDFACTOR_DST_ALPHA:return DkBlendFactor_DstAlpha;
        case SDL_BLENDFACTOR_ONE_MINUS_DST_ALPHA:return DkBlendFactor_InvDstAlpha;
        default:return DkBlendFactor(0);
        }
    };
    auto operation=[](SDL_BlendOperation op) -> DkBlendOp {
        switch(op) {
        case SDL_BLENDOPERATION_ADD:return DkBlendOp_Add;
        case SDL_BLENDOPERATION_SUBTRACT:return DkBlendOp_Sub;
        case SDL_BLENDOPERATION_REV_SUBTRACT:return DkBlendOp_RevSub;
        case SDL_BLENDOPERATION_MINIMUM:return DkBlendOp_Min;
        case SDL_BLENDOPERATION_MAXIMUM:return DkBlendOp_Max;
        default:return DkBlendOp(0);
        }
    };
    dkBlendStateDefaults(&state);
    state.srcColorBlendFactor=factor(b.src_color); state.dstColorBlendFactor=factor(b.dst_color);
    state.srcAlphaBlendFactor=factor(b.src_alpha); state.dstAlphaBlendFactor=factor(b.dst_alpha);
    state.colorBlendOp=operation(b.color_op); state.alphaBlendOp=operation(b.alpha_op);
    return state.srcColorBlendFactor && state.dstColorBlendFactor && state.srcAlphaBlendFactor &&
        state.dstAlphaBlendFactor && state.colorBlendOp && state.alphaBlendOp;
}
#endif
struct GraphicsState {
    bool demoScene=true;
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
    DkMemBlock textureMemory{}, uploadMemory{}, descriptorMemory{}, modelSamplerMemory{};
    DkGpuAddr modelSamplerDescriptors{};
    int modelAnisotropy=4;
    render::deko::ImageArray swapTargets, sceneTargets, depthTargets;
    DkImage texture{};
    DkShader shaders[10]{};
#ifdef GT3R_DEKO_MODEL
    RaceProfile raceProfile;
    ClockReader clockReader;
    // Diagnostic prototype baseline, kept separate from the real race profile:
    // the prototype submits a combined scene and has no per-item role counters.
    struct DemoTiming {
        TimingStats cadence, physics, prepare, capture, record, wait;
        uint64_t frames=0,draws=0,triangles=0;
    };
    DemoTiming demoTiming[2];
    uint64_t demoFrames=0;
    RaceProfile::ClockState demoClocks;
    bool haveDemoClocks=false,haveDemoSample=false;
    std::unique_ptr<FrameArena> nativeArena;
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
    using NativeReadState=render::deko::FencedReadList<DkFence>;
    std::vector<std::weak_ptr<NativeReadState>> nativeReaders;
    uint64_t nativeReadersRetired=0;
    render::deko::FrameResourceSweep viewResourceSweep;
    uint64_t resourceFrame=0;
    void collect_view_resources(bool force=false) {
        viewResourceSweep.collect(resourceFrame,force,[&] { collect_native_readers(); collect_ui_images(); });
    }
    void collect_native_readers() {
        for(auto it=nativeReaders.begin();it!=nativeReaders.end();) {
            if(auto state=it->lock()) {
                nativeReadersRetired+=state->collect([](DkFence& fence) { return dkFenceWait(&fence,0)==DkResult_Success; });
                ++it;
            } else it=nativeReaders.erase(it);
        }
    }
    std::vector<std::shared_ptr<UiImage>> uiImages;
    std::vector<std::shared_ptr<UiImage>> uiFrameImages[frames];
    bool descriptorUsed[descriptorCount]{};
    size_t uiUploadCount = 0, uiRetiredCount = 0, uiPeakImages = 0, uiPeakTriangles = 0;
    std::shared_ptr<UiImage> upload_ui_texture(const UiTexture& texture) {
        // New immutable image versions never overwrite GPU-visible pixels.
        // Only completed uploads and unreferenced frame versions can be retired.
        // Retiring an old upload can unmap GPU memory. Dispatch the graphics
        // queue before that unmap/allocation path: on Switch it may wait for
        // submitted work even when that work has not yet reached PresentImage.
        // This path only runs for a new image, not ordinary scene submissions.
        if(texture.target) trace("Native target upload: dispatch queued graphics before resource retirement");
        dkQueueFlush(queue);
        collect_ui_images();
        if(texture.target) trace("Native target upload: retired completed resources");
        auto free_id = [&] {
            unsigned id = demoScene ? 1+frames : 1;
            while (id < descriptorCount && descriptorUsed[id]) ++id;
            return id;
        };
        unsigned id = free_id();
        if (id == descriptorCount) {
            // Bounded backpressure on one frame fence, not the whole queue.
            for (unsigned slot = 0; slot < frames && id == descriptorCount; ++slot) {
                if (!uiInFlight[slot]) continue;
                dkQueueFlush(queue);
                if (dkFenceWait(&uiFence[slot], -1) != DkResult_Success) throw "SDL frame fence wait failed";
                collect_ui_images(); id = free_id();
            }
            for (const auto& retired : uiImages) {
                if (id != descriptorCount) break;
                if (retired.use_count() == 1 && retired->uploadSubmitted) {
                    dkQueueFlush(queue);
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
        const unsigned physicalHeight=texture.target ? unsigned(texture.h) : std::max(16u,unsigned(texture.h));
        lm.dimensions[0] = unsigned(texture.w); lm.dimensions[1] = physicalHeight;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &lm);
        auto aligned = [](uint32_t n) { return (n + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1); };
        if(texture.target) trace("Native target upload: colour allocation begin");
        image->memory = allocate(aligned(uint32_t(dkImageLayoutGetSize(&layout))), DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
        dkImageInitialize(&image->image, &layout, image->memory, 0);
        if(texture.target) trace("Native target upload: colour allocation complete");
        const size_t rowBytes=size_t(texture.w)*4, uploadBytes=rowBytes*physicalHeight;
        image->staging = allocate(aligned(uint32_t(uploadBytes)), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        auto* pixels=static_cast<uint8_t*>(dkMemBlockGetCpuAddr(image->staging));
        if(physicalHeight==unsigned(texture.h)) std::memcpy(pixels,texture.pixels.data(),uploadBytes);
        else for(unsigned row=0;row<physicalHeight;++row)
            std::memcpy(pixels+row*rowBytes,texture.pixels.data()+(row*unsigned(texture.h)/physicalHeight)*rowBytes,rowBytes);
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
        DkImageRect rect{0,0,0,unsigned(texture.w),physicalHeight,1};
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
                // This image version uploads only once. A completed upload
                // never needs another fence poll; later target writes have
                // separate view/UI fences and strong-reference leases.
                image->uploadSubmitted=false;
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
        bool wrapU=false,wrapV=false;
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
    render::deko::SubmissionFrame<frames> nativeUiFrame;
    uint64_t uiEarlyBatches=0, uiReaderWaits=0;
    void record_ui(unsigned slot, unsigned imageSlot) {
        if (uiInFlight[slot]) {
            dkQueueFlush(queue); // dispatch pending work before a CPU wait within the frame
            if (dkFenceWait(&uiFence[slot], -1) != DkResult_Success)
                throw "SDL UI command-memory fence wait failed";
        }
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
        // Standalone UI cannot depend on the diagnostic compose pass's bindings.
        dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
        dkCmdBufBindImageDescriptorSet(cmd,dkMemBlockGetGpuAddr(descriptorMemory),descriptorCount);
        dkCmdBufBindSamplerDescriptorSet(cmd,dkMemBlockGetGpuAddr(descriptorMemory)+samplerOffset,8);
        DkColorWriteState writes; dkColorWriteStateDefaults(&writes);
        dkCmdBufBindColorWriteState(cmd,&writes);
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
            DkImage* destination = op.target ? &op.target->image : swapTargets.image(imageSlot);
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
            if(!native_blend(op.blend,blend)) throw "Unsupported SDL blend state";
            dkCmdBufBindBlendState(cmd, 0, &blend);
            Uint32 descriptor = op.borrowedScene ? imageSlot + 1 : (op.texture ? op.texture->descriptor : 0);
            if (op.borrowedImage) {
                const auto& image=op.borrowedImage->source;
                if (!image.descriptor(image.userdata,imageSlot,&descriptor) || descriptor>=descriptorCount)
                    throw "Borrowed image descriptor unavailable";
                uiFrameBorrowed[slot].push_back(op.borrowedImage);
            }
            const DkResHandle handle = dkMakeTextureHandle(descriptor,
                (op.scale == SDL_SCALEMODE_LINEAR ? 1 : 0) | (op.wrapU ? 2 : 0) | (op.wrapV ? 4 : 0));
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
    int acquire_native_image() {
        return nativeUiFrame.acquire([&] {
            const int image=dkQueueAcquireImage(queue,swapchain);
            if (image<0 || image>=int(frames)) throw "Invalid native swapchain image";
            return image;
        });
    }
    void submit_pending_native_ui() {
        if (demoScene || uiCommands.empty()) return;
        const unsigned image=unsigned(acquire_native_image());
        record_ui(nativeUiFrame.next_submission(),image);
        ++uiEarlyBatches;
        dkQueueFlush(queue);
    }
    void finish_submitted_ui_readers() {
        // Only submitted UI batches are waited, never a queue-wide idle.
        dkQueueFlush(queue);
        for (unsigned slot=0;slot<frames;++slot) if (uiInFlight[slot]) {
            if (dkFenceWait(&uiFence[slot],-1)!=DkResult_Success)
                throw "Native view UI-reader fence wait failed";
            ++uiReaderWaits;
        }
        collect_ui_images();
    }
    GT3R_DekoBackend backend() {
        GT3R_DekoBackend result{};
        result.version = GT3R_DEKO_BACKEND_VERSION;
        result.userdata = this;
        result.supports_blend = [](void*, SDL_BlendMode b) { DkBlendState state; return native_blend(b,state); };
        result.create_texture = [](void* state, int w, int h, int access, SDL_PropertiesID props, void** out) {
            if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
                return SDL_SetError("Deko SDL: unsupported texture dimensions/access");
            auto& self = *static_cast<GraphicsState*>(state);
            try {
                auto texture = std::make_unique<UiTexture>();
                texture->w = w; texture->h = h;
                texture->target = access == SDL_TEXTUREACCESS_TARGET;
                texture->borrowedScene = SDL_GetBooleanProperty(props, GT3R_DEKO_BORROW_SCENE_PROPERTY, false);
                if(texture->borrowedScene && !self.demoScene)
                    return SDL_SetError("Native-only context has no diagnostic scene image");
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
            UiCommand op;
            op.target = static_cast<GraphicsState*>(state)->uiCurrentTarget;
            if (texture) {
                const auto& source = *static_cast<UiTexture*>(texture);
                op.texture = source.image; op.borrowedScene = source.borrowedScene; op.borrowedImage = source.borrowedImage;
            }
            if (op.texture && op.texture == op.target) return SDL_SetError("Deko SDL: cannot sample the active render target");
            op.scale = scale; op.wrapU=addressU==SDL_TEXTURE_ADDRESS_WRAP; op.wrapV=addressV==SDL_TEXTURE_ADDRESS_WRAP;
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
        if(demoScene) {
            trace("Demo comparison: combined scene; first 120 frames excluded; CPU wall times, no GPU timestamp queries. Compare clocks, camera, settings and emitted geometry before attributing a difference to the backend.");
            for(unsigned mode=0;mode<2;++mode) {
                const auto& d=demoTiming[mode];
                const char* name=mode ? "materials" : "generic";
                d.cadence.report("demo_cadence",name);
                d.physics.report("demo_simulation_cpu",name);
                d.prepare.report("demo_prepare_cpu_including_simulation",name);
                d.capture.report("demo_environment_cpu",name);
                d.record.report("demo_main_record_cpu",name);
                d.wait.report("demo_main_wait_cpu",name);
                if(d.frames) {
                    char line[224];
                    std::snprintf(line,sizeof(line),"Demo work mode=%s frames=%llu main_draws_per_frame=%.2f main_triangles_per_frame=%.2f",name,
                        (unsigned long long)d.frames,double(d.draws)/d.frames,double(d.triangles)/d.frames);
                    trace(line);
                }
            }
        }
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
        #ifdef GT3R_DEKO_SDL
        char pressureSummary[192];
        std::snprintf(pressureSummary,sizeof(pressureSummary),"Native UI backpressure: early_batches=%llu reader_fence_waits=%llu",(unsigned long long)uiEarlyBatches,(unsigned long long)uiReaderWaits);
        trace(pressureSummary);
        char collectionSummary[224]; std::snprintf(collectionSummary,sizeof(collectionSummary),"Native view resource collection: scheduled_sweeps=%llu skipped_views=%llu pressure_sweeps=%llu",
            (unsigned long long)viewResourceSweep.scheduled,(unsigned long long)viewResourceSweep.skipped,(unsigned long long)viewResourceSweep.forced); trace(collectionSummary);
        char readsSummary[128]; std::snprintf(readsSummary,sizeof(readsSummary),"Native sampled-image leases retired by fence: %llu",(unsigned long long)nativeReadersRetired); trace(readsSummary);
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
            if(nativeArena) {
                char arenaLog[224]; std::snprintf(arenaLog,sizeof(arenaLog),"Native frame arena: bytes=%llu bank_waits=%llu cpu_read_invalidations=%llu",(unsigned long long)nativeArena->bytes(),(unsigned long long)nativeArena->waits(),(unsigned long long)nativeArena->cpu_invalidations()); trace(arenaLog);
                nativeArena.reset();
                raceProfile.report();
            }
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
        if (modelSamplerMemory) dkMemBlockDestroy(modelSamplerMemory);
        sceneTargets.reset();
        if (device) dkDeviceDestroy(device);
        if(bufferedNativeTrace) flush_native_trace();
    }
    DkMemBlock allocate(uint32_t size, uint32_t flags) {
        DkMemBlockMaker m;
        dkMemBlockMakerDefaults(&m, device, size);
        m.flags = flags | DkMemBlockFlags_ZeroFillInit;
        auto block = dkMemBlockCreate(&m);
        if (!block) throw "GPU allocation failed";
        return block;
    }
    bool init_model_samplers() {
        modelSamplerMemory = allocate(DK_MEMBLOCK_ALIGNMENT, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        if (!modelSamplerMemory) return false;
        modelSamplerDescriptors = dkMemBlockGetGpuAddr(modelSamplerMemory);
        return write_model_samplers(modelAnisotropy, false);
    }
    bool write_model_samplers(int level, bool wait) {
        if (!modelSamplerMemory) return false;
        level = level >= 16 ? 16 : level >= 8 ? 8 : level >= 4 ? 4 : level >= 2 ? 2 : 1;
        if (wait && queue && !dkQueueIsInErrorState(queue)) dkQueueWaitIdle(queue);
        auto* descriptors = static_cast<uint8_t*>(dkMemBlockGetCpuAddr(modelSamplerMemory));
        for (unsigned i = 0; i < 4; ++i) {
            DkSampler sampler; dkSamplerDefaults(&sampler);
            sampler.minFilter = sampler.magFilter = DkFilter_Linear;
            sampler.maxAnisotropy = float(level);
            sampler.wrapMode[0] = (i & 1) ? DkWrapMode_ClampToEdge : DkWrapMode_Repeat;
            sampler.wrapMode[1] = (i & 2) ? DkWrapMode_ClampToEdge : DkWrapMode_Repeat;
            DkSamplerDescriptor descriptor; dkSamplerDescriptorInitialize(&descriptor, &sampler);
            std::memcpy(descriptors + i * sizeof(descriptor), &descriptor, sizeof(descriptor));
        }
        modelAnisotropy = level;
        return true;
    }
    bool set_model_anisotropy(int level) {
        level = level >= 16 ? 16 : level >= 8 ? 8 : level >= 4 ? 4 : level >= 2 ? 2 : 1;
        return level == modelAnisotropy || write_model_samplers(level, true);
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
    void init(bool loadDemo=true) {
        demoScene=loadDemo;
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
        if(demoScene) {
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
        }
        codeMemory = allocate(codeSize, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
        if(demoScene) {
            shader(0, "romfs:/shaders/probe.vert.dksh");
            shader(1, "romfs:/shaders/probe.frag.dksh");
            shader(2, "romfs:/shaders/compose.vert.dksh");
            shader(3, "romfs:/shaders/compose.frag.dksh");
        }
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
        if(!demoScene) shader(9, "romfs:/shaders/common.frag.dksh");
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
                if(!demoScene) { p[0]=p[1]=p[2]=p[3]=255; continue; }
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
        descriptorMemory = allocate((samplerOffset+8*sizeof(DkSamplerDescriptor)+DK_MEMBLOCK_ALIGNMENT-1)&~(DK_MEMBLOCK_ALIGNMENT-1), DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        auto* descriptors = static_cast<uint8_t*>(dkMemBlockGetCpuAddr(descriptorMemory));
        DkImageDescriptor imageDescriptor;
        dkImageDescriptorInitialize(&imageDescriptor, &textureView, false, false);
        std::memcpy(descriptors, &imageDescriptor, sizeof(imageDescriptor));
        if(demoScene) {
            for (unsigned i = 0; i < frames; ++i) {
                DkImageView sceneView;
                dkImageViewDefaults(&sceneView, sceneTargets.image(i));
                dkImageDescriptorInitialize(&imageDescriptor, &sceneView, false, false);
                std::memcpy(descriptors + (i + 1) * sizeof(imageDescriptor), &imageDescriptor, sizeof(imageDescriptor));
            }
        }
        DkSampler sampler;
        dkSamplerDefaults(&sampler);
        for(unsigned index=0;index<8;++index) {
            sampler.minFilter=sampler.magFilter=(index&1) ? DkFilter_Linear : DkFilter_Nearest;
            sampler.wrapMode[0]=(index&2) ? DkWrapMode_Repeat : DkWrapMode_ClampToEdge;
            sampler.wrapMode[1]=(index&4) ? DkWrapMode_Repeat : DkWrapMode_ClampToEdge;
            DkSamplerDescriptor descriptor; dkSamplerDescriptorInitialize(&descriptor,&sampler);
            std::memcpy(descriptors+samplerOffset+index*sizeof(descriptor),&descriptor,sizeof(descriptor));
        }
        trace("texture descriptors initialized");
        if(!demoScene) {
            create_queue();
            trace("Native context ready: no demo scene, physics bundle or model packet loaded");
            return;
        }
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
        create_queue();
#ifdef GT3R_DEKO_MODEL
        load_demo_scene();
#endif
    }
    void create_queue() {
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
    }
#ifdef GT3R_DEKO_MODEL
    void load_demo_scene() {
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
    }
#endif

    void prepare_frame() {
        phases={};
        if(!demoScene) return;
        const auto prepareBegin=TimingClock::now();
#ifdef GT3R_DEKO_MODEL
        ++demoFrames;
        const auto clocks=clockReader.poll(armTicksToNs(armGetSystemTick()));
        if(!haveDemoClocks || clocks!=demoClocks) {
            char line[224];
            std::snprintf(line,sizeof(line),"Demo clocks frame=%llu cpu_hz=%u gpu_hz=%u ram_hz=%u known_mask=%u (read-only service rates, polled once per second)",
                (unsigned long long)demoFrames,clocks.hz[0],clocks.hz[1],clocks.hz[2],unsigned(clocks.known));
            trace(line); demoClocks=clocks; haveDemoClocks=true;
        }
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
#if defined(GT3R_DEKO_MODEL) && defined(GT3R_DEKO_SDL)
        if(nativeArena) nativeArena->end();
#endif
        const auto begin=TimingClock::now();
        const int slot =
#ifdef GT3R_DEKO_SDL
            !demoScene ? acquire_native_image() :
#endif
            dkQueueAcquireImage(queue, swapchain);
        if (slot < 0 || slot >= int(frames)) throw "Invalid swapchain slot";
        const auto acquired=TimingClock::now();
        if(demoScene) {
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
            if(demoFrames>120) {
                auto& stats=demoTiming[materialMode ? 1 : 0];
                ++stats.frames; stats.draws+=submission.draws; stats.triangles+=submission.triangles;
                stats.physics.add(phases.physics); stats.prepare.add(phases.prepare);
                stats.capture.add(phases.capture); stats.record.add(submission.record); stats.wait.add(submission.wait);
            }
            framePrepared=false;
#else
            dkQueueSubmitCommands(queue, draw);
#endif
            dkQueueSubmitCommands(queue, compose[slot]);
            if (showOverlay) dkQueueSubmitCommands(queue, overlay);
        }
        const auto sceneEnd=TimingClock::now();
#ifdef GT3R_DEKO_SDL
        record_ui(demoScene ? unsigned(slot) : nativeUiFrame.next_submission(),unsigned(slot));
#endif
        const auto uiEnd=TimingClock::now();
        dkQueuePresentImage(queue, swapchain, slot);
#ifdef GT3R_DEKO_SDL
        if (!demoScene) nativeUiFrame.presented();
        ++resourceFrame; // UI batch flushes alone do not advance the visible frame.
#endif
        const auto end=TimingClock::now();
        phases.acquire=elapsed_ms(begin,acquired);
        phases.scene=elapsed_ms(acquired,sceneEnd);
        phases.ui=elapsed_ms(sceneEnd,uiEnd);
        phases.present=elapsed_ms(uiEnd,end);
        const unsigned mode=materialMode ? 1 : 0;
#ifdef GT3R_DEKO_MODEL
        if(demoScene) {
            if(demoFrames>120 && haveDemoSample && previousMode==mode)
                demoTiming[mode].cadence.add(elapsed_ms(previousPresentTime,end));
            haveDemoSample=demoFrames>120;
        }
#endif
        auto& stats=frameTiming[mode];
        stats.acquire.add(elapsed_ms(begin,acquired)); stats.scene.add(elapsed_ms(acquired,sceneEnd));
        stats.ui.add(elapsed_ms(sceneEnd,uiEnd)); stats.present.add(elapsed_ms(uiEnd,end));
        stats.total.add(elapsed_ms(begin,end));
        // Attribute each return-to-return interval to the preceding frame mode.
        // Exclude the transition interval, which may include toggle logging.
        if (havePresentTime && previousMode==mode) stats.cadence.add(elapsed_ms(previousPresentTime,end));
        previousPresentTime=end; previousMode=mode; havePresentTime=true;
#if defined(GT3R_DEKO_MODEL) && defined(GT3R_DEKO_SDL)
        raceProfile.present(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end.time_since_epoch()).count()),phases.acquire,phases.ui,phases.present);
#endif
    }
};
#if defined(GT3R_DEKO_SDL) && defined(GT3R_DEKO_MODEL)
// Transitional adapter: uses the verified uploader while device ownership moves
// out of the probe. Static showroom models include their lights and sphere map.
struct FrontendVersion;
class FrontendTexture final : public render::NativeTexture {
public:
    std::shared_ptr<GraphicsState> owner;
    std::shared_ptr<GraphicsState::UiImage> image;
    DkImageDescriptor descriptor{};
    std::shared_ptr<FrontendVersion> version;
    SDL_Texture* renderer_texture() const override;
};
class FrontendCube final:public render::NativeCubeTexture {
public:
    std::shared_ptr<GraphicsState> owner;
    std::array<std::shared_ptr<FrontendTexture>,6> faces;
    std::array<DkImageDescriptor,6> descriptors;
};
struct SampledFrontendImages {
    std::shared_ptr<FrontendTexture> sphere, image;
    std::shared_ptr<FrontendCube> cube;
};
struct FrontendUploads {
    std::shared_ptr<GraphicsState> owner;
    DkQueue queue{};
    std::mutex mutex;
    explicit FrontendUploads(std::shared_ptr<GraphicsState> state):owner(std::move(state)) {
        DkQueueMaker maker; dkQueueMakerDefaults(&maker,owner->device);
        maker.flags=DkQueueFlags_Graphics;
        queue=dkQueueCreate(&maker);
        if(!queue) throw "Native loader queue creation failed";
    }
    ~FrontendUploads() { dkQueueDestroy(queue); }
};
struct FrontendTextureFamily {
    std::shared_ptr<Model> original;
    render::deko::TextureUpdates updates;
    FrontendTextureFamily(std::shared_ptr<Model> base,const std::vector<scene::Texture>& textures)
        :original(std::move(base)),updates(textures) {}
    ~FrontendTextureFamily() {
        if(!updates.serial()) return;
        char line[192]; std::snprintf(line,sizeof(line),"Native animation pixel storage: revisions=%llu allocations=%llu reuses=%llu",
            (unsigned long long)updates.serial(),(unsigned long long)updates.allocations(),(unsigned long long)updates.reuses()); trace(line);
    }
};
class FrontendModel final : public render::NativeModel {
    std::vector<scene::Mesh> layout;
    std::vector<uint64_t> textureVersions;
    render::deko::InstanceTransformCache instanceTransform;
    uint64_t syncedSerial=0,textureScans=0,textureScanSkips=0;
    bool geometrySet=false, empty=true;
    uint64_t vertexUpdates=0, geometryUpdates=0, rebuilds=0, textureSyncs=0;
    void retire_submission() {
        if(!submitted) return;
        // The family may keep the old Model alive after a topology replacement.
        // Complete its reads before releasing this wrapper's sphere-image leases.
        render_model()->wait_submissions();
        submitted=false;
    }
    void remember(const std::vector<scene::Mesh>& meshes) {
        instanceTransform.invalidate();
        layout=meshes;
        for(const auto& mesh:meshes) transforms.push_back(render::Mat4::from(mesh.transform));
    }
public:
    std::shared_ptr<GraphicsState> owner;
    std::shared_ptr<FrontendUploads> uploads;
    std::shared_ptr<FrontendTextureFamily> family;
    std::shared_ptr<GraphicsState::NativeReadState> reads=std::make_shared<GraphicsState::NativeReadState>();
    bool readsRegistered=false;
    unsigned nextSlot=0;
    bool submitted=false;
    std::shared_ptr<Model> model;
    std::vector<render::Mat4> transforms;
    FrontendModel(std::shared_ptr<GraphicsState> state,const scene::Model& source,std::shared_ptr<FrontendUploads> loader)
        :owner(std::move(state)),uploads(std::move(loader)) {
        std::lock_guard lock(uploads->mutex);
        model=std::make_shared<Model>(owner->device,owner->queue,&owner->shaders[6],&owner->shaders[7],&owner->shaders[8],
            owner->demoScene ? nullptr : &owner->shaders[9], owner->modelSamplerDescriptors);
        SceneOptions options; options.frontend=true; options.frameArena=owner->nativeArena.get(); options.uploadQueue=uploads->queue;
        options.meshRoles.assign(source.meshes.size(),1);
        model->load_scene(source,options);
        family=std::make_shared<FrontendTextureFamily>(model,source.textures);
        textureVersions.resize(source.textures.size());
        remember(source.meshes); geometrySet=true; empty=false;
        trace("Native frontend model uploaded through GpuModel");
    }
    FrontendModel(const FrontendModel& source,int)
        :geometrySet(true),owner(source.owner),uploads(source.uploads),family(source.family) {
        // Dynamic GpuModels start as texture-sharing shells and receive their
        // first geometry in update_geometry(). They are valid scene items before
        // that update too: needs_phase() skips their intentionally empty geometry.
        textureVersions.resize(family->updates.metadata().size());
        trace("Native derived model shares original texture images and family updates");
    }
    FrontendModel(const FrontendModel&)=delete;
    FrontendModel& operator=(const FrontendModel&)=delete;
    ~FrontendModel() override {
        retire_submission();
        char cacheLog[224]; std::snprintf(cacheLog,sizeof(cacheLog),"Native CPU cache: world_products=%llu placements_reused=%llu texture_scans=%llu texture_scan_skips=%llu",(unsigned long long)instanceTransform.products,(unsigned long long)instanceTransform.skips,(unsigned long long)textureScans,(unsigned long long)textureScanSkips); trace(cacheLog);
        if(vertexUpdates || geometryUpdates || textureSyncs) {
            char line[224]; std::snprintf(line,sizeof(line),"Native dynamic model: vertex_updates=%llu geometry_updates=%llu topology_rebuilds=%llu texture_syncs=%llu",(unsigned long long)vertexUpdates,(unsigned long long)geometryUpdates,(unsigned long long)rebuilds,(unsigned long long)textureSyncs); trace(line);
        }
    }
    bool valid() const override { return geometrySet; }
    bool has_geometry() const { return !empty; }
    bool needs_phase(unsigned phase,const render::DrawItem& item,const render::DrawOptions& options,bool overrideImage) const {
        if(empty) return false;
        if(!phase) return true;
        const int lod=item.lod!=-2 ? item.lod : options.lod;
        for(const auto& mesh:layout) {
            if(lod>=0 && mesh.lod!=lod) continue;
            for(const auto& primitive:mesh.primitives) {
                if(!primitive.index_count) continue;
                auto state=render::deko::primitive_state(primitive.flags,overrideImage || primitive.texture>=0,item.sphere_map,true,item.background,true,item.frame_alpha,item.dest_alpha_blend);
                if(state.skip) continue;
                if(item.alpha<1 && state.blend==render::deko::NativeBlend::Opaque && !state.depth_only) state.transparent=true;
                if((phase==1 && !state.transparent) || (phase==2 && state.transparent)) return true;
            }
        }
        return false;
    }
    Model* render_model() const { return model ? model.get() : family->original.get(); }
    bool place(const render::Mat4& world) {
        if(empty) return true;
        return instanceTransform.apply(world,transforms,[&](size_t i,const render::Mat4& pose) { return render_model()->set_mesh_transform(i,pose); });
    }
    bool sync_textures() {
        if(empty) return true;
        if(syncedSerial==family->updates.serial()) { ++textureScanSkips; return true; }
        ++textureScans;
        const auto& revisions=family->updates.revisions();
        for(size_t i=0;i<revisions.size();++i) if(revisions[i].version!=textureVersions[i]) {
            if(!model->update_texture(i,*revisions[i].pixels)) return SDL_SetError("Native texture family layout mismatch");
            textureVersions[i]=revisions[i].version; ++textureSyncs;
        }
        syncedSerial=family->updates.serial();
        return true;
    }
    bool update_vertices(std::span<const scene::Vertex> vertices) override {
        size_t count=0; for(const auto& mesh:layout) count+=mesh.vertices.size();
        if(!geometrySet || vertices.size()!=count || !render::deko::valid_dynamic_vertices(vertices))
            return SDL_SetError("Invalid native vertex update");
        if(!empty) {
            size_t first=0;
            for(size_t i=0;i<layout.size();++i) {
                if(!model->update_vertices(i,vertices.subspan(first,layout[i].vertices.size()))) return SDL_SetError("Native vertex layout mismatch");
                first+=layout[i].vertices.size();
            }
        }
        ++vertexUpdates; return true;
    }
    bool update_geometry(const std::vector<scene::Mesh>& meshes) override {
        try {
            if(!render::deko::valid_dynamic_geometry(meshes,family->updates.metadata().size()))
                return SDL_SetError("Invalid native geometry update");
            instanceTransform.invalidate();
            std::vector<render::Mat4> poses;
            for(const auto& mesh:meshes) poses.push_back(render::Mat4::from(mesh.transform));
            bool hasTriangles=false;
            for(const auto& mesh:meshes) for(const auto& primitive:mesh.primitives) hasTriangles|=primitive.index_count!=0;
            if(!hasTriangles) {
                auto prepared=meshes;
                retire_submission();
                model.reset(); layout=std::move(prepared); transforms=std::move(poses);
                geometrySet=true; empty=true; ++geometryUpdates; return true;
            }
            if(model && !empty && render::same_model_geometry_layout(layout,meshes)) {
                for(size_t i=0;i<meshes.size();++i)
                    if(!model->update_vertices(i,meshes[i].vertices)) return SDL_SetError("Native stable topology update failed");
                transforms=std::move(poses); ++geometryUpdates; return true;
            }
            scene::Model scene; scene.meshes=meshes; scene.textures=family->updates.metadata();
            std::vector<uint64_t> unseen(scene.textures.size(),0);
            auto replacement=std::make_shared<Model>(owner->device,owner->queue,&owner->shaders[6],&owner->shaders[7],&owner->shaders[8],owner->demoScene ? nullptr : &owner->shaders[9]);
            SceneOptions options; options.frontend=true; options.frameArena=owner->nativeArena.get(); options.uploadQueue=uploads->queue;
            options.textureSource=family->original.get(); options.meshRoles.assign(meshes.size(),1);
            {
                std::lock_guard lock(uploads->mutex);
                replacement->load_scene(scene,options);
            }
            retire_submission();
            model=std::move(replacement); layout=std::move(scene.meshes); transforms=std::move(poses);
            textureVersions=std::move(unseen); syncedSerial=0; geometrySet=true; empty=false;
            ++rebuilds; ++geometryUpdates;
            if(rebuilds==1) trace("Native derived geometry ready: shared GPU originals, fenced mutable storage");
            return true;
        } catch(const char* error) { return SDL_SetError("%s",error); }
          catch(const std::exception& error) { return SDL_SetError("%s",error.what()); }
    }
    bool update_textures(std::span<const render::ModelTextureUpdate> updates) override {
        try { return family->updates.apply(updates); }
        catch(const std::exception& error) { return SDL_SetError("%s",error.what()); }
    }
};
struct FrontendImage {
    DkQueue queue{};
    unsigned refs=1;
    std::shared_ptr<GraphicsState::UiImage> image;
    render::deko::ImageArray depth;
    std::weak_ptr<render::deko::BorrowedImageLease> readers;
    DkFence written{};
    bool inFlight=false;
    ~FrontendImage() {
        if (inFlight) {
            dkQueueFlush(queue);
            if (dkFenceWait(&written,-1)!=DkResult_Success)
                std::_Exit(1); // do not free a target whose GPU completion is unknown
        }
    }
    void release() { if (!--refs) delete this; }
};
struct FrontendVersion {
    FrontendImage* image=nullptr; // one owner reference, independent of SDL's lease
    SDL_Texture* wrapper=nullptr;
    ~FrontendVersion() { SDL_DestroyTexture(wrapper); if(image) image->release(); }
};
SDL_Texture* FrontendTexture::renderer_texture() const { return version ? version->wrapper : nullptr; }
class FrontendView final : public render::NativeView {
    std::shared_ptr<GraphicsState> owner;
    SDL_Renderer* renderer;
    std::vector<std::shared_ptr<FrontendVersion>> versions;
    std::shared_ptr<FrontendUploads> uploads;
    std::unique_ptr<FrontendModel> postModel;
    std::shared_ptr<FrontendTexture> postLut;
    std::optional<std::array<float,6>> postGradeKey;
    uint64_t draws=0, pressureDrains=0, pressureWaits=0;
    size_t maxItems=0;
    int width=0,height=0;
    bool errorReported=false;
    std::shared_ptr<FrontendVersion> fail(const char* message) {
        const std::string copy(message);
        SDL_SetError("%s",copy.c_str());
        if(!errorReported) { trace(copy.c_str()); errorReported=true; }
        return nullptr;
    }
    void prepare(int w,int h) {
        trace("Native frontend target preparation begin");
        dkQueueFlush(owner->queue);
        trace("Native frontend target preparation: prior graphics dispatched");
        std::vector<std::shared_ptr<FrontendVersion>> prepared;
        // Two draws (floor mirror and body) per view, two SDL frames in flight, plus one writer.
        for(unsigned slot=0;slot<frames*2+1;++slot) {
            auto version=std::make_shared<FrontendVersion>();
            version->image=new FrontendImage();
            auto& target=*version->image;
            target.queue=owner->queue;
            GraphicsState::UiTexture description;
            description.w=w; description.h=h; description.target=true;
            description.pixels.resize(size_t(w)*h*4);
            trace("Native frontend target preparation: CPU initialization buffer ready");
            target.image=owner->upload_ui_texture(description);
            trace("Native frontend target colour upload queued");
            target.depth.create(owner->device,unsigned(w),unsigned(h),1,DkImageFormat_Z24X8);
            trace("Native frontend target depth allocated");
            GT3R_DekoImage source{GT3R_DEKO_IMAGE_VERSION,owner.get(),&target,w,h,
                [](void* p) { ++static_cast<FrontendImage*>(p)->refs; },
                [](void* p) { static_cast<FrontendImage*>(p)->release(); },
                [](void* p,unsigned,Uint32* descriptor) { *descriptor=static_cast<FrontendImage*>(p)->image->descriptor; return true; }};
            const auto props=SDL_CreateProperties();
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_STATIC);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,w);
            SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,h);
            SDL_SetPointerProperty(props,GT3R_DEKO_BORROW_IMAGE_PROPERTY,&source);
            version->wrapper=SDL_CreateTextureWithProperties(renderer,props);
            SDL_DestroyProperties(props);
            if(!version->wrapper) throw SDL_GetError();
            SDL_SetTextureScaleMode(version->wrapper,SDL_SCALEMODE_LINEAR);
            for(const auto& texture:owner->uiTextures)
                if(texture->borrowedImage && texture->borrowedImage->source.userdata==&target)
                    target.readers=texture->borrowedImage;
            if(target.readers.expired()) throw "Native view image lease was not registered";
            trace("Native frontend target SDL wrapper registered");
            prepared.push_back(std::move(version));
        }
        versions=std::move(prepared); width=w; height=h;
        char line[160]; std::snprintf(line,sizeof(line),"Native frontend view prepared: size=%dx%d versions=%zu",w,h,versions.size()); trace(line);
    }
public:
    FrontendView(std::shared_ptr<GraphicsState> state,SDL_Renderer* r,std::shared_ptr<FrontendUploads> loader):owner(std::move(state)),renderer(r),uploads(std::move(loader)) {}
    ~FrontendView() override {
        char line[224]; std::snprintf(line,sizeof(line),"Native frontend view released: draws=%llu versions=%zu pressure_drains=%llu pressure_waits=%llu",(unsigned long long)draws,versions.size(),(unsigned long long)pressureDrains,(unsigned long long)pressureWaits); trace(line);
    }
    std::shared_ptr<FrontendVersion> render(int w,int h,const render::Mat4& view,const render::Mat4& projection,
        const render::Vec3& eye,const std::vector<render::DrawItem>& items,const render::DrawOptions& options,bool post=false) {
        try {
            auto* cpu=owner->raceProfile.active ? &owner->raceProfile.frontend[size_t(post ? render::ProfilePass::Post : options.profile_pass)] : nullptr;
            const auto stamp=[&] { return cpu ? TimingClock::now() : TimingClock::time_point{}; };
            const auto validationBegin=stamp();
            if(w<=0 || h<=0 || w>4096 || h>4096 || items.empty() || items.size()>256 ||
                options.wire!=render::WireMode::Off ||
                options.env_cube || options.env_sphere || options.overlay || options.vignette)
                return fail("Native view: unsupported dimensions, item count or effect");
            for(float value:{options.fog_start,options.fog_end,options.fog_max,options.far_clip,options.draw_distance})
                if(!std::isfinite(value)) return fail("Native view: invalid scene distance or fog");
            for(float value:options.fog_color) if(!std::isfinite(value)) return fail("Native view: invalid fog colour");
            std::array<FrontendModel*,256> models{};
            std::array<std::shared_ptr<FrontendTexture>,256> images{};
            for(size_t i=0;i<items.size();++i) {
                const auto& item=items[i];
                auto* model=post ? postModel.get() : item.model ? dynamic_cast<FrontendModel*>(item.model->native_model()) : nullptr;
                if(!model || !model->valid() || model->owner.get()!=owner.get()) {
                    char message[256];
                    auto* native=item.model ? item.model->native_model() : nullptr;
                    std::snprintf(message,sizeof(message),
                        "Native view invalid model: item=%zu/%zu gpu=%p native=%p frontend=%d valid=%d owner_match=%d role=%u pass=%u",
                        i,items.size(),static_cast<const void*>(item.model),static_cast<const void*>(native),model!=nullptr,
                        model ? int(model->valid()) : 0,model ? int(model->owner.get()==owner.get()) : 0,
                        unsigned(item.profile_role),unsigned(options.profile_pass));
                    return fail(message);
                }
                if(item.texture || item.wheel_blur || item.spin_turn!=0 || item.dest_alpha_blend>2)
                    return fail("Native view: item requires an unsupported resource or effect");
                if(item.lights && !render::deko::valid_scene_lights(*item.lights)) return fail("Native view: invalid per-item lights");
                if(!std::isfinite(item.modelview_scale) || item.modelview_scale<=0) return fail("Native view: invalid modelview scale");
                if(!std::isfinite(item.alpha) || item.alpha<0 || !std::isfinite(item.tint) || !std::isfinite(item.reflection_scale))
                    return fail("Native view: invalid item colour parameters");
                for(float value:item.transform.m) if(!std::isfinite(value)) return fail("Native view: invalid item transform");
                for(float value:item.light_scale) if(!std::isfinite(value)) return fail("Native view: invalid item lighting");
                if(item.native_texture) {
                    images[i]=std::dynamic_pointer_cast<FrontendTexture>(item.native_texture);
                    if(!images[i] || images[i]->owner.get()!=owner.get()) return fail("Native view texture belongs to another backend");
                }
                models[i]=model;
            }
            const auto validationEnd=stamp();
            if(width!=w || height!=h || versions.empty()) prepare(w,h);
            owner->collect_view_resources();
            auto find_available=[&]() -> std::shared_ptr<FrontendVersion> {
                for(auto& version:versions) {
                    // lock adds one reference; the other must be SDL's own texture.
                    // Queued commands and in-flight UI batches keep additional refs.
                    auto readers=version->image->readers.lock();
                    if(version.use_count()==1 && readers && readers.use_count()==2 && (!version->image->inFlight ||
                        dkFenceWait(&version->image->written,0)==DkResult_Success)) return version;
                }
                return nullptr;
            };
            auto available=find_available();
            if(!available && !owner->demoScene) {
                ++pressureDrains;
                // View3D already flushed SDL's command queue. Drain its backend
                // copies too, otherwise their image leases cannot ever retire.
                owner->submit_pending_native_ui();
                owner->collect_view_resources(true);
                available=find_available();
                if(!available) {
                    ++pressureWaits;
                    owner->finish_submitted_ui_readers();
                    owner->collect_native_readers();
                    available=find_available();
                }
                // An unconsumed native write may remain after UI readers retire.
                if(!available) for(auto& version:versions) {
                    auto readers=version->image->readers.lock();
                    if(version.use_count()==1 && readers && readers.use_count()==2 && version->image->inFlight) {
                        dkQueueFlush(owner->queue);
                        if(dkFenceWait(&version->image->written,-1)!=DkResult_Success)
                            return fail("Native view write fence wait failed");
                        available=version; break;
                    }
                }
                if(pressureDrains==1) trace("Native view backpressure: pending SDL batches dispatched; image reuse remains fenced");
            }
            if(!available) return fail("Native view images still retained by UI or native texture consumers");
            auto sphere=std::dynamic_pointer_cast<FrontendTexture>(options.native_env_sphere);
            if(options.native_env_sphere && (!sphere || sphere->owner.get()!=owner.get()))
                return fail("Native reflection image belongs to another backend");
            auto cube=std::dynamic_pointer_cast<FrontendCube>(options.native_env_cube);
            if(options.native_env_cube && (!cube || cube->owner.get()!=owner.get()))
                return fail("Native environment cube belongs to another backend");
            auto& target=*available->image;
            struct GpuProfileScope {
                GraphicsState& owner;
                std::optional<size_t> index;
                render::GpuProfileRange kind=render::GpuProfileRange::Main;
                bool selected=false;
                void start() {
                    if(index || !selected) return;
                    owner.raceProfile.usedGpu=true;
                    try { index=owner.nativeArena->begin_gpu(kind,owner.raceProfile.info,owner.raceProfile.frames); }
                    catch(...) { ++owner.raceProfile.badGpu; selected=false; }
                }
                void stop() noexcept {
                    if(index) { try { owner.nativeArena->end_gpu(*index); } catch(...) { ++owner.raceProfile.badGpu; } index.reset(); }
                    selected=false;
                }
                ~GpuProfileScope() { stop(); }
            } gpu_profile{*owner};
            gpu_profile.selected=owner->raceProfile.gpu_matches(options.profile_pass,post);
            if(gpu_profile.selected) {
                gpu_profile.kind=*owner->raceProfile.target;
                if(gpu_profile.kind!=render::GpuProfileRange::MainCarsEffects) gpu_profile.start();
            }
            bool reachedCars=false;
            struct WriteCompletion {
                FrontendImage& target; bool submitted=false;
                ~WriteCompletion() {
                    if(submitted) { dkQueueSignalFence(target.queue,&target.written,true); target.inFlight=true; }
                }
            } completion{target};
            const auto targetEnd=stamp();
            const auto schedule=render::deko::draw_schedule(items,eye,options.sort_blended);
            const auto scheduleEnd=stamp();
            bool first=true;
            for(const auto& entry:schedule) {
                const size_t i=entry.item;
                auto item=items[i]; item.depth_reset=entry.reset_depth;
                if(!reachedCars && item.profile_role==render::ProfileRole::Car) {
                    reachedCars=true;
                    if(gpu_profile.kind==render::GpuProfileRange::MainScenery) gpu_profile.stop();
                    else if(gpu_profile.kind==render::GpuProfileRange::MainCarsEffects) gpu_profile.start();
                }
                auto* model=models[i];
                const auto phaseBegin=stamp();
                const bool drawGeometry=model->needs_phase(entry.phase,item,options,bool(images[i]));
                if(cpu) cpu->phase+=elapsed_ms(phaseBegin,stamp());
                if(!drawGeometry && !first && !entry.reset_depth) continue;
                auto* geometry=model->render_model();
                const auto texturesBegin=stamp();
                if(!model->sync_textures()) return fail(SDL_GetError());
                const auto texturesEnd=stamp();
                if(!model->place(item.transform)) return fail("Invalid native view model transform");
                if(!geometry->set_camera(view,projection,eye)) return fail("Invalid native view camera");
                const auto placementEnd=stamp();
                if(cpu) { cpu->textures+=elapsed_ms(texturesBegin,texturesEnd); cpu->placement+=elapsed_ms(texturesEnd,placementEnd); }
                const unsigned slot=owner->nativeArena->begin(); // one bank for every model/view in this frame
                geometry->submit_to(slot,unsigned(w),unsigned(h),&target.image->image,target.depth.image(0),
                    {options.clear[0],options.clear[1],options.clear[2],options.clear[3]},&options,sphere ? &sphere->descriptor : nullptr,&item,first,drawGeometry,
                    cube ? cube->descriptors.data() : nullptr,images[i] ? &images[i]->descriptor : nullptr,entry.phase,post);
                const auto timing=geometry->submission_timing();
                owner->raceProfile.model(post ? render::ProfilePass::Post : options.profile_pass,item.profile_role,
                    timing.record,timing.wait,timing.draws,timing.triangles,item.car_surface,entry.phase,timing.setup,timing.cull,timing.packets,timing.submit);
                if(cpu) cpu->model+=timing.record+timing.wait;
                first=false;
                model->submitted=true;
                completion.submitted=true;
                // submit_to waits for this model slot before replacing its uniforms.
                if(!model->readsRegistered) { owner->nativeReaders.push_back(model->reads); model->readsRegistered=true; }
                std::shared_ptr<void> resources;
                if(sphere || images[i] || cube) resources=std::make_shared<SampledFrontendImages>(SampledFrontendImages{sphere,images[i],cube});
                model->reads->publish(geometry->submission_fence(slot),std::move(resources));
            }
            if(cpu) {
                ++cpu->views;
                cpu->validation+=elapsed_ms(validationBegin,validationEnd);
                cpu->target+=elapsed_ms(validationEnd,targetEnd);
                cpu->schedule+=elapsed_ms(targetEnd,scheduleEnd);
                cpu->dispatch+=elapsed_ms(scheduleEnd,stamp());
            }
            if(items.size()>maxItems) {
                maxItems=items.size();
                char line[128]; std::snprintf(line,sizeof(line),"Native view composition: items=%zu clear_once=1 shared_depth=1",maxItems); trace(line);
            }
            if(!draws) trace("Native frontend first model submission queued");
            ++draws;
            return available;
        } catch(const char* error) { return fail(error); }
          catch(const std::exception& error) { return fail(error.what()); }
    }
    SDL_Texture* draw(int w,int h,const render::Mat4& view,const render::Mat4& projection,
        const render::Vec3& eye,const std::vector<render::DrawItem>& items,const render::DrawOptions& options) override {
        auto image=render(w,h,view,projection,eye,items,options);
        return image ? image->wrapper : nullptr;
    }
    std::shared_ptr<render::NativeTexture> render_texture(int w,int h,const render::Mat4& view,const render::Mat4& projection,
        const render::Vec3& eye,const std::vector<render::DrawItem>& items,const render::DrawOptions& options) override {
        auto image=render(w,h,view,projection,eye,items,options);
        if(!image) return {};
        auto texture=std::make_shared<FrontendTexture>(); texture->owner=owner;
        texture->version=std::move(image); texture->image=texture->version->image->image;
        DkImageView source; dkImageViewDefaults(&source,&texture->image->image);
        dkImageDescriptorInitialize(&texture->descriptor,&source,false,false);
        return texture;
    }
    std::shared_ptr<render::NativeTexture> process_texture(int w,int h,const std::shared_ptr<render::NativeTexture>& source,
        const render::DrawOptions& options) override {
        try {
            if(!source) { fail("Native postprocess requires an input image"); return {}; }
            const auto key=render::deko::post_grade_key(options.post_grade);
            if(!postLut || !postGradeKey || *postGradeKey!=key) {
                const auto pixels=render::deko::make_post_grade_lut(options.post_grade);
                GraphicsState::UiTexture upload; upload.w=256; upload.h=16;
                upload.pixels.resize(sizeof(pixels)); std::memcpy(upload.pixels.data(),pixels.data(),sizeof(pixels));
                auto lut=std::make_shared<FrontendTexture>(); lut->owner=owner;
                lut->image=owner->upload_ui_texture(upload);
                DkImageView view; dkImageViewDefaults(&view,&lut->image->image);
                dkImageDescriptorInitialize(&lut->descriptor,&view,false,false);
                postLut=std::move(lut); postGradeKey=key;
                trace("Native colour grade LUT rebuilt: 256x16 RGBA8, three contribution rows");
            }
            if(!postModel) {
                scene::Model quad; scene::Mesh mesh; mesh.name="native postprocess";
                for(unsigned i=0;i<4;++i) {
                    scene::Vertex vertex; vertex.pos[0]=(i&1) ? 1 : -1; vertex.pos[1]=(i&2) ? -1 : 1;
                    vertex.uv[0]=(i&1) ? 1 : 0; vertex.uv[1]=(i&2) ? 1 : 0; mesh.vertices.push_back(vertex);
                }
                mesh.indices={0,1,2,2,1,3}; scene::Primitive primitive;
                primitive.index_count=6; primitive.flags=scene::kUnlit|scene::kDoubleSided|scene::kNoDepthWrite;
                mesh.primitives.push_back(primitive); quad.meshes.push_back(std::move(mesh));
                postModel=std::make_unique<FrontendModel>(owner,quad,uploads);
            }
            render::DrawItem item; item.native_texture=source;
            auto postOptions=options;
            // Post uses the otherwise-unused sphere slot. The normal sampled
            // resource lease retains each LUT until that submission retires.
            postOptions.native_env_sphere=postLut;
            auto version=render(w,h,{},{},{},{item},postOptions,true);
            if(!version) return {};
            auto texture=std::make_shared<FrontendTexture>(); texture->owner=owner;
            texture->version=std::move(version); texture->image=texture->version->image->image;
            DkImageView view; dkImageViewDefaults(&view,&texture->image->image);
            dkImageDescriptorInitialize(&texture->descriptor,&view,false,false);
            return texture;
        } catch(const char* error) { fail(error); return {}; }
          catch(const std::exception& error) { fail(error.what()); return {}; }
    }

};
class FrontendRenderer final : public render::NativeRenderer {
    std::shared_ptr<GraphicsState> owner;
    SDL_Renderer* renderer;
    std::shared_ptr<FrontendUploads> uploads;
public:
    FrontendRenderer(std::shared_ptr<GraphicsState> state,SDL_Renderer* r):owner(std::move(state)),renderer(r) {
        if (!owner->init_model_samplers()) throw "Deko3D model sampler allocation failed";
        owner->nativeArena=std::make_unique<FrameArena>(owner->device,owner->queue);
        owner->nativeArena->gpu_sink(owner.get(),[](void* state,render::GpuProfileRange kind,double ms,const render::RaceProfileInfo& info,uint64_t frame) {
            static_cast<GraphicsState*>(state)->raceProfile.gpu_result(kind,ms,info,frame);
        });
        uploads=std::make_shared<FrontendUploads>(owner); // created before jobs start
    }
    ~FrontendRenderer() override { SDL_DestroyRenderer(renderer); }
    bool background_model_upload() const override { return true; }
    bool anisotropy_supported() const override { return bool(owner->modelSamplerMemory); }
    int anisotropy() const override { return owner->modelAnisotropy; }
    bool set_anisotropy(int level) override { return owner->set_model_anisotropy(level); }
    SDL_Renderer* renderer2d() const override { return renderer; }
    const char* name() const override { return "Deko3D native frontend (experimental)"; }
    void profile_race_frame(const render::RaceProfileInfo& info) override {
        owner->raceProfile.begin(info);
        owner->raceProfile.clock_state(owner->clockReader.poll(SDL_GetTicksNS()));
    }
    void profile_race_cpu(const render::RaceProfileCpu& cpu) override { if(owner->raceProfile.active) owner->raceProfile.cpu=cpu; }
    std::shared_ptr<render::NativeTexture> upload_texture(int width,int height,std::span<const u32> pixels) override {
        try {
            if(width<=0 || height<=0 || width>4096 || height>4096 || pixels.size()!=size_t(width)*height)
                { SDL_SetError("Invalid native reflection texture"); return {}; }
            GraphicsState::UiTexture upload; upload.w=width; upload.h=height;
            upload.pixels.resize(pixels.size_bytes());
            std::memcpy(upload.pixels.data(),pixels.data(),pixels.size_bytes());
            auto texture=std::make_shared<FrontendTexture>(); texture->owner=owner;
            texture->image=owner->upload_ui_texture(upload);
            DkImageView view; dkImageViewDefaults(&view,&texture->image->image);
            dkImageDescriptorInitialize(&texture->descriptor,&view,false,false);
            trace("Native showroom reflection texture uploaded");
            return texture;
        } catch(const char* error) { SDL_SetError("%s",error); return {}; }
          catch(const std::exception& error) { SDL_SetError("%s",error.what()); return {}; }
    }
    std::unique_ptr<render::NativeModel> upload_model(const scene::Model& source) override {
        auto report_failure=[&](const char* error) {
            uint64_t vertices=0,indices=0,primitives=0;
            for(const auto& mesh:source.meshes) {
                vertices+=mesh.vertices.size(); indices+=mesh.indices.size(); primitives+=mesh.primitives.size();
            }
            char line[384];
            std::snprintf(line,sizeof(line),"Native model upload failed: name=%s meshes=%zu vertices=%llu indices=%llu primitives=%llu textures=%zu reason=%s",
                source.name.c_str(),source.meshes.size(),(unsigned long long)vertices,(unsigned long long)indices,
                (unsigned long long)primitives,source.textures.size(),error);
            trace(line);
            SDL_SetError("%s",error);
        };
        try {
            return std::make_unique<FrontendModel>(owner,source,uploads);
        }
        catch(const char* error) { report_failure(error); return nullptr; }
        catch(const std::exception& error) { report_failure(error.what()); return nullptr; }
    }
    std::unique_ptr<render::NativeModel> share_model_textures(const render::NativeModel& source) override {
        auto* native=dynamic_cast<const FrontendModel*>(&source);
        if(!native || native->owner.get()!=owner.get()) { SDL_SetError("Native texture source belongs to another backend"); return {}; }
        try { return std::make_unique<FrontendModel>(*native,0); }
        catch(const std::exception& error) { SDL_SetError("%s",error.what()); return {}; }
    }
    std::unique_ptr<render::NativeView> create_view() override { return std::make_unique<FrontendView>(owner,renderer,uploads); }
    std::shared_ptr<render::NativeCubeTexture> create_cube(const std::array<std::shared_ptr<render::NativeTexture>,6>& faces) override {
        auto cube=std::make_shared<FrontendCube>(); cube->owner=owner;
        for(size_t i=0;i<6;++i) {
            auto face=std::dynamic_pointer_cast<FrontendTexture>(faces[i]);
            if(!face || face->owner.get()!=owner.get() || face->image->w!=face->image->h ||
               (i && face->image->w!=cube->faces[0]->image->w)) {
                SDL_SetError("Invalid native cube faces/backend/dimensions"); return {};
            }
            cube->faces[i]=face; cube->descriptors[i]=face->descriptor;
        }
        return cube;
    }
};
#endif

struct Graphics::Impl : GraphicsState {};
Graphics::Graphics() : impl_(std::make_shared<Impl>()) {}
Graphics::~Graphics() = default;
void Graphics::init() { impl_->init(); }
void Graphics::init_native() {
    bufferedNativeTrace=std::getenv("GT3R_DEKO_SYNC_LOG")==nullptr;
#ifdef GT3R_DEKO_SDL
    impl_->init(false);
#else
    throw "Native presentation requires the SDL adapter";
#endif
}
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
std::shared_ptr<render::NativeRenderer> Graphics::create_native_renderer(SDL_Window* window) {
#ifdef GT3R_DEKO_MODEL
    auto api=impl_->backend();
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD,"3"); // geometry path; this driver has no line-list callback
    const auto props=SDL_CreateProperties();
    SDL_SetPointerProperty(props,SDL_PROP_RENDERER_CREATE_WINDOW_POINTER,window);
    SDL_SetStringProperty(props,SDL_PROP_RENDERER_CREATE_NAME_STRING,"gt3r-deko");
    SDL_SetPointerProperty(props,GT3R_DEKO_BACKEND_PROPERTY,&api);
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRendererWithProperties(props),SDL_DestroyRenderer);
    SDL_DestroyProperties(props);
    if(!renderer) return {};
    auto result=std::make_shared<FrontendRenderer>(impl_,renderer.get());
    renderer.release();
    return result;
#else
    (void)window;
    SDL_SetError("Native model adapter is not included"); return {};
#endif
}
GT3R_DekoBackend Graphics::sdl_backend() { return impl_->backend(); }
GT3R_DekoImage Graphics::scene_image() {
    if(!impl_->demoScene) { SDL_SetError("Native-only context has no diagnostic scene"); return {}; }
    return {GT3R_DEKO_IMAGE_VERSION,impl_.get(),impl_.get(),int(width),int(height),
        [](void*) {}, [](void*) {}, // context outlives renderer and frame fences
        [](void*,unsigned slot,Uint32* descriptor) { if(slot>=frames)return false; *descriptor=slot+1; return true; }};
}
#endif
bool Graphics::toggle_overlay() {
    impl_->showOverlay = !impl_->showOverlay;
    return impl_->showOverlay;
}
bool open_log(const char* path) {
    diagnosticLog = std::fopen(path ? path : "sdmc:/switch/gt3r-deko-probe.log", "w");
    return diagnosticLog != nullptr;
}
void close_log() {
    if (diagnosticLog) { std::fclose(diagnosticLog); diagnosticLog = nullptr; }
}
} // namespace gt3r::deko_probe

#if defined(GT3R_DEKO_SDL) && defined(GT3R_DEKO_MODEL)
namespace gt3r::render::deko {
std::shared_ptr<NativeRenderer> create_renderer(SDL_Window* window) {
    try {
        gt3r::deko_probe::Graphics device;
        device.init_native();
        // The backend shares native state ownership, so it remains valid when
        // this local initialization wrapper is destroyed.
        return device.create_native_renderer(window);
    } catch(const char* error) {
        const std::string copy(error); SDL_SetError("%s",copy.c_str());
    } catch(const std::exception& error) { SDL_SetError("%s",error.what()); }
    return {};
}
} // namespace gt3r::render::deko
#endif
