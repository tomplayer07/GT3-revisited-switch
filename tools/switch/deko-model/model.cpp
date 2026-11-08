#include "render/model_geometry.h"
#include "model.h"
#include "frame_arena.h"
#include "render/view3d.h"
#include "render/deko/model_allocation.h"
#include "render/deko/primitive_state.h"
#include "visibility.h"
#include "wheel_pose.h"
#include "course_motion.h"
#include "../deko-probe/graphics.h"
#include <chrono>
#include "render/math3d.h"
#include "scene/model.h"
#include "formats/course/course_model.h"
#include "sim/sim_types.h"
#include "race/point_lights.h"
#include "render/deko/scene_state.h"
#include "race/occluder.h"
#include "race/sun_glare.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace gt3r::deko_probe {
namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point begin,Clock::time_point end) { return std::chrono::duration<double,std::milli>(end-begin).count(); }
constexpr unsigned sceneSlots = 2, captureSlot = sceneSlots, slots = sceneSlots + 1, maxPrimitives = 16384;
constexpr unsigned externalImages=8; // sphere, six cube faces, per-item image
constexpr uint32_t commandBytes = 4 * 1024 * 1024;
uint32_t aligned(uint32_t n) { return (n + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1); }
struct Reader {
    std::vector<uint8_t> data;
    size_t at = 0;
    uint32_t word() {
        if (at > data.size() || data.size() - at < 4) throw "Truncated model packet";
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(data[at++]) << (8*i);
        return value;
    }
    float real() {
        uint32_t bits = word(); float f;
        std::memcpy(&f, &bits, 4);
        if (!std::isfinite(f)) throw "Nonfinite model packet value";
        return f;
    }
    uint32_t count(uint32_t limit) { auto n = word(); if (n > limit) throw "Model packet count limit exceeded"; return n; }
};
struct Vertex { float pos[3], normal[3], uv[2]; uint32_t rgba; };
static_assert(sizeof(Vertex) == 36);
struct Primitive { uint32_t first, count, texture, flags; float alpha; float material[16]{}; float env[8]{1,1,1,0,0,0,0,0}; float brakeEmission[3]{}; MeshBounds bounds{}; bool bounded=false; uint8_t envKind=0; bool envPoint=false; float envPointPos[3]{}, envAlpha[2]{1,0}; };
struct Mesh { int lod=0; render::Mat4 transform; uint32_t vertexBase, vertexCount, firstPrimitive, primitiveCount; bool car=false, backdrop=false, brakeVariant=false, sky=false; unsigned carIndex=0; MeshBounds bounds; WheelBinding wheel; };
struct Texture { DkImage image{}; DkMemBlock memory{}; uint32_t flags{}, width{}, height{}; };
}
struct Model::Impl {
    FrameArena* frameArena=nullptr;
    uint64_t arenaGenerations[slots]{};
    FrameArena::Slice arenaVertices[slots]{};
    DkDevice device;
    DkQueue queue;
    DkGpuAddr sharedSamplerDescriptors{};
    const DkShader *vertexShader, *fragmentShader, *captureShader, *commonFragmentShader;
    DkMemBlock vertexMemory{}, indexMemory{}, descriptorMemory{}, uploadCommandsMemory{}, staticModelMemory{};
    uint32_t indexOffset=0,descriptorOffset=0;
    std::vector<uint8_t> descriptorShadow,frameDescriptorShadows[slots];
    DkCmdBuf uploadCommands{};
    DkMemBlock dynamicVertices[slots]{};
    std::vector<Vertex> vertexShadow, initialVertices;
    std::vector<uint64_t> vertexVersions, slotVersions[slots];
    std::vector<scene::Vertex> demoScratch;
    uint64_t updateCalls=0, vertexCopyBytes=0, vertexSlotAllocations=0;
    struct TextureSlot { DkImage image{}; DkMemBlock memory{}, upload{}; uint64_t version=0; };
    struct TextureUpdate { std::vector<uint32_t> pixels; uint64_t version=0; TextureSlot slots[sceneSlots + 1]; };
    std::vector<TextureUpdate> textureUpdates;
    std::vector<gtm1::ClutSet> courseCluts;
    std::unique_ptr<CourseMotion> courseMotion;
    uint64_t movingUpdates=0;
    uint64_t paletteUpdates=0;
    race::CoursePointLights courseLights;
    race::Occluder sceneOccluder;
    race::SunParams environment;
    struct CarFrame { std::array<float,52> lights{}; float brake=0; bool brakeNow=false; };
    std::array<CarFrame,6> carFrames{};
    unsigned carCount=1;
    DkMemBlock lightUniforms[slots]{};
    DkMemBlock fogUniforms[slots]{};
    bool frontendScene=false;
    render::deko::ModelBufferBudget frameBudget{commandBytes,maxPrimitives,slots};
    struct StaticTextureSlice { DkImageLayout layout{}; uint32_t imageOffset=0,uploadOffset=0; };
    std::vector<StaticTextureSlice> staticTextureSlices;
    DkMemBlock staticTextureArena{}, staticTextureUpload{};
    bool courseFog=true;
    bool pointLighting=true;
    uint64_t lightUpdates=0;
    unsigned maximumLights=0;
    float brakeFactor=0, maximumBrakeFactor=0;
    bool brakeNow=false, hasBrakeVariant=false, wheelMotion=true, hasWheelBindings=false;
    uint64_t wheelUpdates=0;
    float maximumSteer=0, maximumSuspensionTravel=0, maximumSpinStep=0;
    uint64_t brakeFrames=0;
    DkMemBlock frameDescriptors[slots]{};
    uint32_t descriptorBytes=0;
    int demoTexture=-1;
    std::vector<uint32_t> demoOriginal, demoPixels;
    uint64_t textureUpdateCalls=0, textureCopyBytes=0, textureImageAllocations=0, textureStagingAllocations=0, textureDescriptorAllocations=0;
    DkImage captureDepth{};
    DkMemBlock captureDepthMemory{};
    unsigned environmentBase=0;
    bool environmentReady=false;
    unsigned environmentFrame=0, environmentFace=0;
    uint64_t liveCaptureFaces=0;
    uint64_t liveCaptureSkipped=0;
    double liveCaptureTotal=0, liveCaptureMax=0;
    render::Vec3 capturePosition{};
    std::vector<DkMemBlock> staging;
    std::vector<Texture> textures;
    std::vector<Mesh> meshes;
    std::vector<uint8_t> visibleMeshes;
    uint64_t culledMeshes=0, testedMeshes=0,primitiveTests=0,primitiveCulled=0;
    uint64_t bodyBackfacePackets=0,bodyTwoSidedPackets=0,commonPackets=0,generalPackets=0,shaderSwitches=0;
    std::vector<render::Mat4> originalTransforms;
    bool externalCamera=false;
    render::Mat4 frameView, frameProjection;
    render::Vec3 frameEye{};
    uint64_t transformUpdates=0, cameraUpdates=0;
    std::vector<Primitive> primitives;
    DkCmdBuf commands[slots]{};
    DkMemBlock commandMemory[slots]{}, uniforms[slots]{};
    DkFence fences[slots]{};
    bool inFlight[slots]{};
    uint32_t vertexBytes{}, indexBytes{};
    render::Vec3 center{};
    float radius = 1;
    bool freeCamera=false, materialLighting=true, reflections=true;
    bool courseCulling=true;
    unsigned courseMaskView=0;
    render::Vec3 eyePosition{};
    float yaw=0, pitch=0, objectHeading=0;
    render::Mat4 vehicleBindPose;
    sim::VehicleState initialVehicle;
    uint64_t measuredFrames=0, measuredDraws=0, measuredTriangles=0;
    double recordTotal=0, recordMax=0, waitTotal=0, waitMax=0;
    SubmissionTiming lastSubmission;
    struct ModeStats { uint64_t frames=0; double total=0, maximum=0; };
    ModeStats modes[2];
    uint64_t lastUniformBytes=0, lastStateChanges=0;
    uint64_t allocatedBytes=0;
    uint64_t preparedAllocationBytes=0;
    Impl(DkDevice d, DkQueue q, const DkShader* v, const DkShader* f, const DkShader* c,const DkShader* common,
         DkGpuAddr sharedSamplers)
        : device(d), queue(q), sharedSamplerDescriptors(sharedSamplers), vertexShader(v), fragmentShader(f),
          captureShader(c), commonFragmentShader(common) {}
    ~Impl() {
        if(commonPackets || generalPackets) {
            char line[192]; std::snprintf(line,sizeof(line),"Native fragment variants: common_packets=%llu general_packets=%llu switches=%llu",
                (unsigned long long)commonPackets,(unsigned long long)generalPackets,(unsigned long long)shaderSwitches); trace(line);
        }
        if(bodyBackfacePackets || bodyTwoSidedPackets) {
            char line[192]; std::snprintf(line,sizeof(line),"Native body raster policy: backface_enabled_packets=%llu two_sided_packets=%llu (submitted packets, not measured GPU rejection counts)",
                (unsigned long long)bodyBackfacePackets,(unsigned long long)bodyTwoSidedPackets); trace(line);
        }
        // A never-drawn frontend model has already completed its isolated upload.
        // Do not touch the render queue when construction fails on a loader thread.
        const bool rendered=std::any_of(std::begin(inFlight),std::end(inFlight),[](bool value) { return value; });
        if(frameArena && rendered) wait_submissions();
        else if ((!frontendScene || rendered) && !dkQueueIsInErrorState(queue)) dkQueueWaitIdle(queue);
        if (!frontendScene && !dkQueueIsInErrorState(queue) && commands[0] && commandMemory[0] && descriptorMemory && textures.size() > 228) {
            auto readback = allocate(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_ZeroFillInit);
            for (size_t index : {1u, 55u, 147u, 209u, 227u, 269u, 365u, 385u, 432u}) {
                if (index + 1 >= textures.size()) continue;
                const auto& target = textures[index + 1];
                const size_t count = size_t(target.width) * target.height;
                if (!count || count > 1024) continue;
                std::memset(dkMemBlockGetCpuAddr(readback), 0xcd, count * 4);
                auto cmd = commands[0];
                dkCmdBufClear(cmd);
                dkCmdBufAddMemory(cmd, commandMemory[0], 0, commandBytes);
                dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache | DkInvalidateFlags_Image);
                DkImageView view; dkImageViewDefaults(&view, &target.image);
                DkImageRect rect{0, 0, 0, target.width, target.height, 1};
                DkCopyBuf dst{dkMemBlockGetGpuAddr(readback), 0, 0};
                dkCmdBufCopyImageToBuffer(cmd, &view, &rect, &dst, 0);
                dkCmdBufBarrier(cmd, DkBarrier_Full, DkInvalidateFlags_L2Cache);
                dkQueueSubmitCommands(queue, dkCmdBufFinishList(cmd));
                dkQueueWaitIdle(queue);
                const auto* actual = static_cast<const uint32_t*>(dkMemBlockGetCpuAddr(readback));
                size_t nonGrey = 0, firstBad = count;
                uint32_t badGpu = 0;
                for (size_t p = 0; p < count; ++p) if (actual[p] != actual[0]) {
                    if (!nonGrey) { firstBad = p; badGpu = actual[p]; }
                    ++nonGrey;
                }
                DkImageDescriptor expectedDesc;
                dkImageDescriptorInitialize(&expectedDesc, &view, false, false);
                const auto* baseTable = static_cast<const uint8_t*>(dkMemBlockGetCpuAddr(descriptorMemory));
                const bool baseMatch = std::memcmp(baseTable + (index + 1) * sizeof(expectedDesc), &expectedDesc, sizeof(expectedDesc)) == 0;
                bool slotMatch[slots]{true, true, true};
                for (unsigned s = 0; s < slots; ++s) if (frameDescriptors[s]) {
                    const auto* slotTable = static_cast<const uint8_t*>(dkMemBlockGetCpuAddr(frameDescriptors[s]));
                    slotMatch[s] = std::memcmp(slotTable + (index + 1) * sizeof(expectedDesc), &expectedDesc, sizeof(expectedDesc)) == 0;
                }
                char line[320];
                std::snprintf(line, sizeof(line), "Shutdown texture check: id=%zu size=%ux%u non_uniform=%zu first_diff=%zu row0=%08x row8=%08x diff_val=%08x desc=%d/%d/%d/%d",
                    index, target.width, target.height, nonGrey, firstBad, actual[0], count > 256 ? actual[256] : 0u, badGpu,
                    int(baseMatch), int(slotMatch[0]), int(slotMatch[1]), int(slotMatch[2]));
                trace(line);
            }
            dkMemBlockDestroy(readback);
            allocatedBytes -= 4096;
        }
        if (preparedAllocationBytes) {
            char line[128]; std::snprintf(line,sizeof(line),"Model allocations after preparation: bytes=%llu",(unsigned long long)(allocatedBytes-preparedAllocationBytes)); trace(line);
        }
        if(primitiveTests) {
            char cullLog[160]; std::snprintf(cullLog,sizeof(cullLog),"Native static primitive culling: tested=%llu rejected=%llu",(unsigned long long)primitiveTests,(unsigned long long)primitiveCulled); trace(cullLog);
        }
        if (measuredFrames) {
            trace(courseCulling ? "Course material backface culling enabled" : "Course material backface culling disabled");
            char summary[384];
            std::snprintf(summary,sizeof(summary),"Model CPU summary: frames=%llu record_avg_ms=%.3f record_max_ms=%.3f slot_wait_avg_ms=%.3f slot_wait_max_ms=%.3f last_draws=%llu last_triangles=%llu (CPU recording only, not GPU time or FPS)",
                (unsigned long long)measuredFrames,recordTotal/measuredFrames,recordMax,waitTotal/measuredFrames,waitMax,
                (unsigned long long)measuredDraws,(unsigned long long)measuredTriangles);
            trace(summary);
        }
        for (unsigned mode=0; mode<2; ++mode) if (modes[mode].frames) {
            char summary[256]; const auto& stats=modes[mode];
            std::snprintf(summary,sizeof(summary),"Model CPU mode=%s frames=%llu avg_ms=%.3f max_ms=%.3f last_uniform_bytes=%llu last_state_changes=%llu (last counts shared across modes)",
                mode ? "materials" : "generic",(unsigned long long)stats.frames,stats.total/stats.frames,stats.maximum,
                (unsigned long long)lastUniformBytes,(unsigned long long)lastStateChanges);
            trace(summary);
        }
        for (unsigned i = 0; i < slots; ++i) {
            if (commands[i]) dkCmdBufDestroy(commands[i]);
            if (commandMemory[i]) dkMemBlockDestroy(commandMemory[i]);
            if (uniforms[i]) dkMemBlockDestroy(uniforms[i]);
        }
        if (updateCalls) {
            char line[192];
            std::snprintf(line,sizeof(line),"Dynamic vertices: updates=%llu gpu_copy_bytes=%llu slot_allocations=%llu (independent fenced scene/capture slots)",
                (unsigned long long)updateCalls,(unsigned long long)vertexCopyBytes,(unsigned long long)vertexSlotAllocations); trace(line);
        }
        if (liveCaptureFaces) {
            char line[256]; std::snprintf(line,sizeof(line),"Live environment: faces=%llu skipped_busy=%llu interval=10 separate_slot=1 submit_avg_ms=%.3f submit_max_ms=%.3f (CPU submission only)",
                (unsigned long long)liveCaptureFaces,(unsigned long long)liveCaptureSkipped,liveCaptureTotal/liveCaptureFaces,liveCaptureMax); trace(line);
        }
        if (testedMeshes) {
            char line[160]; std::snprintf(line,sizeof(line),"Visibility: tested=%llu culled=%llu draw_distance=600m course meshes only",
                (unsigned long long)testedMeshes,(unsigned long long)culledMeshes); trace(line);
        }
        if (transformUpdates || cameraUpdates) {
            char line[160]; std::snprintf(line,sizeof(line),"Frame state: transform_updates=%llu camera_updates=%llu (no geometry reupload)",
                (unsigned long long)transformUpdates,(unsigned long long)cameraUpdates); trace(line);
        }
        if (wheelUpdates) {
            char line[224]; std::snprintf(line,sizeof(line),"Wheel motion: mesh_updates=%llu max_steer_rad=%.3f max_suspension_m=%.3f max_spin_step_rad=%.3f enabled=%d",
                (unsigned long long)wheelUpdates,maximumSteer,maximumSuspensionTravel,maximumSpinStep,int(wheelMotion)); trace(line);
        }
        if (brakeFrames) {
            char line[160]; std::snprintf(line,sizeof(line),"Brake emission: lit_frames=%llu max_factor=%.2f",(unsigned long long)brakeFrames,maximumBrakeFactor); trace(line);
        }
        if (lightUpdates) {
            char line[160]; std::snprintf(line,sizeof(line),"Course car lighting: updates=%llu max_selected=%u enabled=%d",(unsigned long long)lightUpdates,maximumLights,int(pointLighting)); trace(line);
        }
        for (auto memory:lightUniforms) if (memory) dkMemBlockDestroy(memory);
        for (auto memory:fogUniforms) if (memory) dkMemBlockDestroy(memory);
        if (courseMotion) {
            char line[160]; std::snprintf(line,sizeof(line),"Course moving nodes: groups=%zu mesh_updates=%llu topology=validated",courseMotion->groups.size(),(unsigned long long)movingUpdates); trace(line);
        }
        if (paletteUpdates) {
            char line[128]; std::snprintf(line,sizeof(line),"Original course palette uploads=%llu",(unsigned long long)paletteUpdates); trace(line);
        }
        if (textureUpdateCalls) {
            char line[256];
            std::snprintf(line,sizeof(line),"Dynamic textures: updates=%llu copy_bytes=%llu images=%llu staging=%llu descriptor_tables=%llu (persistent per-slot resources)",
                (unsigned long long)textureUpdateCalls,(unsigned long long)textureCopyBytes,(unsigned long long)textureImageAllocations,(unsigned long long)textureStagingAllocations,(unsigned long long)textureDescriptorAllocations); trace(line);
        }
        for (auto& update : textureUpdates) for (auto& slot : update.slots) {
            if (slot.upload) dkMemBlockDestroy(slot.upload);
            if (slot.memory) dkMemBlockDestroy(slot.memory);
        }
        for (auto memory : frameDescriptors) if (memory) dkMemBlockDestroy(memory);
        for (auto memory : dynamicVertices) if (memory) dkMemBlockDestroy(memory);
        if (uploadCommands) dkCmdBufDestroy(uploadCommands);
        if (uploadCommandsMemory) dkMemBlockDestroy(uploadCommandsMemory);
        for (auto memory : staging) dkMemBlockDestroy(memory);
        for (auto& texture : textures) if (texture.memory) dkMemBlockDestroy(texture.memory);
        if (staticTextureArena) dkMemBlockDestroy(staticTextureArena);
        if(staticModelMemory) dkMemBlockDestroy(staticModelMemory);
        else {
            if (vertexMemory) dkMemBlockDestroy(vertexMemory);
            if (indexMemory) dkMemBlockDestroy(indexMemory);
            if (descriptorMemory) dkMemBlockDestroy(descriptorMemory);
        }
        if (captureDepthMemory) dkMemBlockDestroy(captureDepthMemory);
    }
    void wait_submissions() {
        if(dkQueueIsInErrorState(queue)) return;
        bool dispatched=false;
        for(unsigned slot=0;slot<slots;++slot) if(inFlight[slot]) {
            if(dkFenceWait(&fences[slot],0)!=DkResult_Success) {
                if(!dispatched) { dkQueueFlush(queue); dispatched=true; }
                if(dkFenceWait(&fences[slot],-1)!=DkResult_Success) throw "Native model retirement failed";
            }
            inFlight[slot]=false;
        }
    }
    DkMemBlock allocate(uint32_t bytes, uint32_t flags) {
        DkMemBlockMaker maker;
        dkMemBlockMakerDefaults(&maker, device, aligned(bytes)); maker.flags = flags | DkMemBlockFlags_ZeroFillInit;
        auto memory = dkMemBlockCreate(&maker);
        if (!memory) throw "Model GPU allocation failed";
        allocatedBytes+=aligned(bytes);
        return memory;
    }
    void prepare_static_texture_arena(const scene::Model& source) {
        render::deko::AllocationSlices images, uploads;
        auto reserve=[&](unsigned w,unsigned h) {
            StaticTextureSlice slice;
            DkImageLayoutMaker maker; dkImageLayoutMakerDefaults(&maker,device);
            maker.format=DkImageFormat_RGBA8_Unorm;
            maker.dimensions[0]=w; maker.dimensions[1]=std::max(16u,h);
            dkImageLayoutInitialize(&slice.layout,&maker);
            slice.imageOffset=images.reserve(dkImageLayoutGetSize(&slice.layout),dkImageLayoutGetAlignment(&slice.layout));
            slice.uploadOffset=uploads.reserve(uint64_t(w)*std::max(16u,h)*4,256);
            staticTextureSlices.push_back(slice);
        };
        reserve(1,1); // same untextured fallback as the ordinary uploader
        for(const auto& texture:source.textures) reserve(texture.width,texture.height);
        staticTextureArena=allocate(images.size(),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
        staticTextureUpload=allocate(uploads.size(),DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
        staging.push_back(staticTextureUpload);
        char line[192]; std::snprintf(line,sizeof(line),"Native static texture arena: images=%zu image_bytes=%u upload_bytes=%u memory_blocks=2",staticTextureSlices.size(),images.size(),uploads.size()); trace(line);
    }
    void add_texture(uint32_t w, uint32_t h, uint32_t flags, const std::vector<uint32_t>& pixels) {
        const uint32_t uploadH = h < 16 ? 16u : h;
        std::vector<uint32_t> expanded;
        const std::vector<uint32_t>* uploadPixels = &pixels;
        if (uploadH != h) {
            expanded.resize(size_t(w) * uploadH);
            for (uint32_t y = 0; y < uploadH; ++y) {
                const uint32_t srcY = (y * h) / uploadH;
                std::memcpy(expanded.data() + size_t(y) * w, pixels.data() + size_t(srcY) * w, size_t(w) * 4);
            }
            uploadPixels = &expanded;
        }
        DkImageLayoutMaker maker;
        dkImageLayoutMakerDefaults(&maker, device);
        maker.format = DkImageFormat_RGBA8_Unorm;
        maker.dimensions[0] = w; maker.dimensions[1] = uploadH;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout, &maker);
        textures.emplace_back();
        auto& texture = textures.back(); texture.flags = flags; texture.width = w; texture.height = uploadH;
        DkMemBlock memory{}; uint32_t uploadOffset=0;
        if (staticTextureArena) {
            const auto& slice=staticTextureSlices.at(textures.size()-1);
            dkImageInitialize(&texture.image,&slice.layout,staticTextureArena,slice.imageOffset);
            memory=staticTextureUpload; uploadOffset=slice.uploadOffset;
        } else {
            texture.memory=allocate(uint32_t(dkImageLayoutGetSize(&layout)),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
            dkImageInitialize(&texture.image,&layout,texture.memory,0);
            memory=allocate(uint32_t(uploadPixels->size()*4),DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
            staging.push_back(memory);
        }
        std::memcpy(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(memory))+uploadOffset,uploadPixels->data(),uploadPixels->size()*4);
        DkImageView view;
        dkImageViewDefaults(&view, &texture.image);
        DkCopyBuf source{dkMemBlockGetGpuAddr(memory)+uploadOffset, 0, 0};
        DkImageRect rect{0, 0, 0, w, uploadH, 1};
        dkCmdBufCopyBufferToImage(uploadCommands, &source, &view, &rect, 0);
    }
    void load(const char* path, std::span<const uint8_t> courseBytes) {
        trace("Model packet read begin");
        const auto begin=Clock::now();
        FILE* file=std::fopen(path,"rb");
        if (!file) throw "Cannot open scene packet in the probe SD folder";
        if (std::fseek(file,0,SEEK_END)) { std::fclose(file); throw "Model seek failed"; }
        const long size=std::ftell(file); std::rewind(file);
        if (size<28 || size>64*1024*1024) { std::fclose(file); throw "Invalid model packet size"; }
        Reader r; r.data.resize(size_t(size));
        const bool read=std::fread(r.data.data(),1,r.data.size(),file)==r.data.size(); std::fclose(file);
        if (!read) throw "Incomplete model packet read";
        const auto readEnd=Clock::now(); trace("Model packet read complete");
        if (r.word()!=0x4B445447) throw "Unsupported model packet";
        const auto version=r.word(); if (version<1 || version>9) throw "Unsupported model packet version";
        const auto nm=r.count(1024), nv=r.count(1000000), ni=r.count(3000000), np=r.count(maxPrimitives), nt=r.count(4096);
        if (!nm || !nv || !ni || !np) throw "Empty model packet";
        SceneOptions options; options.freeCamera=version==2;
        if (version>=3) { const auto flags=r.word(); if (flags>1) throw "Invalid camera flags"; options.freeCamera=flags!=0; }
        if (options.freeCamera) for (auto& value : options.camera) value=r.real();
        options.captureEnvironment=version>=5;
        scene::Model source; source.meshes.reserve(nm); source.textures.reserve(nt);
        size_t vertices=0, indices=0, primitives=0;
        for (unsigned m=0;m<nm;++m) {
            scene::Mesh mesh;
            for (auto& value : mesh.transform) value=r.real();
            unsigned role=0; if (version>=5) { role=r.word(); if (role>(version>=8 ? 3u : version>=6 ? 2u : 1u)) throw "Invalid mesh role"; }
            options.meshRoles.push_back(uint8_t(role));
            WheelBinding wheel;
            if (version>=9) {
                const auto index=r.word(), spin=r.word();
                if ((index!=UINT32_MAX && index>3) || spin>1 || (index!=UINT32_MAX && role!=1 && role!=3)) throw "Invalid wheel binding";
                wheel.index=index==UINT32_MAX ? -1 : int(index); wheel.spin=spin!=0;
                wheel.center={r.real(),r.real(),r.real()};
            }
            options.wheels.push_back(wheel);
            const auto vc=r.count(nv), ic=r.count(ni), pc=r.count(np);
            vertices+=vc; indices+=ic; primitives+=pc;
            if (vertices>nv || indices>ni || primitives>np) throw "Model totals mismatch";
            mesh.vertices.resize(vc); mesh.indices.resize(ic); mesh.primitives.resize(pc);
            for (auto& v : mesh.vertices) {
                for (auto& x : v.pos) x=r.real();
                for (auto& x : v.normal) x=r.real();
                for (auto& x : v.uv) x=r.real();
                v.color=r.word();
            }
            for (auto& i : mesh.indices) i=r.word();
            for (auto& p : mesh.primitives) {
                p.first_index=r.word(); p.index_count=r.word();
                const auto texture=r.word();
                if (texture!=UINT32_MAX && texture>=nt) throw "Invalid texture ID";
                p.texture=texture==UINT32_MAX ? -1 : int(texture);
                p.flags=r.word(); p.alpha_ref=r.real();
                if (version>=3) {
                    for (auto& x : p.material.ambient) x=r.real();
                    const auto valid=r.real(); if (valid!=0 && valid!=1) throw "Invalid material validity";
                    p.material.valid=valid!=0;
                    for (auto& x : p.material.diffuse) x=r.real();
                    p.material.scale=r.real();
                    for (auto& x : p.material.specular) x=r.real();
                    p.material.shininess=r.real();
                    for (auto& x : p.material.emission) x=r.real();
                    (void)r.real();
                    if (version>=4) {
                        for (auto& x : p.env_c0) x=r.real();
                        const auto validRows=r.real(); if (validRows!=0 && validRows!=1) throw "Invalid environment rows";
                        p.env_rows=validRows!=0;
                        for (auto& x : p.env_c1) x=r.real();
                        (void)r.real();
                    }
                }
                if (version>=7) for (auto& value:p.material.lamp_emission) value=r.real();
            }
            source.meshes.push_back(std::move(mesh));
        }
        if (vertices!=nv || indices!=ni || primitives!=np) throw "Model totals mismatch";
        for (unsigned i=0;i<nt;++i) {
            scene::Texture texture; texture.width=r.count(4096); texture.height=r.count(4096);
            const auto flags=r.word(); texture.clamp_u=flags&1; texture.clamp_v=flags&2; texture.no_mips=flags&4;
            if (!texture.width || !texture.height || uint64_t(texture.width)*texture.height*4>r.data.size()-r.at) throw "Invalid model texture";
            texture.rgba.resize(size_t(texture.width)*texture.height);
            for (auto& pixel : texture.rgba) pixel=r.word();
            source.textures.push_back(std::move(texture));
        }
        if (r.at!=r.data.size()) throw "Trailing model packet bytes";
        r.data.clear(); r.data.shrink_to_fit();
        trace("Model packet converted to engine scene::Model");
        if (!courseBytes.empty()) {
            courseMotion=std::make_unique<CourseMotion>(courseBytes);
            const auto courseTextures=courseMotion->scene.textures.size();
            if (courseTextures>source.textures.size()) throw "Moving course texture count mismatch";
            for (size_t t=0;t<courseTextures;++t) {
                const auto& a=source.textures[t]; const auto& b=courseMotion->scene.textures[t];
                if (a.width!=b.width || a.height!=b.height || a.rgba!=b.rgba) throw "Moving course texture ordering mismatch";
            }
            scene::Model rebuilt;
            rebuilt.textures=std::move(source.textures);
            rebuilt.meshes=std::move(courseMotion->scene.meshes);
            auto roles=courseMotion->roles;
            std::vector<WheelBinding> wheels(rebuilt.meshes.size());
            for (size_t m=0;m<source.meshes.size();++m) if (options.meshRoles[m]==1 || options.meshRoles[m]==3) {
                rebuilt.meshes.push_back(std::move(source.meshes[m]));
                roles.push_back(options.meshRoles[m]); wheels.push_back(options.wheels[m]);
            }
            options.meshRoles=std::move(roles); options.wheels=std::move(wheels);
            course::CourseModelOptions skyOptions;
            skyOptions.scenery=skyOptions.backdrop=skyOptions.course_line=false; skyOptions.sky=true;
            auto sky=course::build_course_model(courseBytes,skyOptions,"route5_sky");
            char skyLog[128]; std::snprintf(skyLog,sizeof(skyLog),"Original course sky loaded: meshes=%zu textures=%zu",sky.meshes.size(),sky.textures.size()); trace(skyLog);
            options.meshRoles.insert(options.meshRoles.end(),sky.meshes.size(),4);
            options.wheels.resize(options.meshRoles.size());
            course::append_model(rebuilt,std::move(sky));
            source=std::move(rebuilt);
            courseCluts=std::move(courseMotion->cluts);
            courseMotion->scene.textures.clear(); courseMotion->scene.textures.shrink_to_fit();
            trace("Original moving course nodes separated from static scenery");
        }
        load_scene(source,options);
        char summary[192];
        std::snprintf(summary,sizeof(summary),"Packet adapter: file_bytes=%ld read_ms=%.2f total_ms=%.2f",size,milliseconds(begin,readEnd),milliseconds(begin,Clock::now())); trace(summary);
    }
    void load_scene(const scene::Model& source, const SceneOptions& options) {
        if (vertexMemory || !meshes.empty()) throw "Deko scene already uploaded";
        const auto begin=Clock::now();
        frontendScene=options.frontend; frameArena=options.frameArena;
        if (source.meshes.empty() || source.meshes.size()>1024 || source.textures.size()>4096 || (!options.meshRoles.empty() && options.meshRoles.size()!=source.meshes.size())) throw "Invalid engine scene counts/roles";
        if (!options.wheels.empty() && options.wheels.size()!=source.meshes.size()) throw "Invalid wheel binding count";
        for (float v : options.camera) if (!std::isfinite(v)) throw "Invalid camera";
        freeCamera=options.freeCamera;
        eyePosition={options.camera[0],options.camera[1],options.camera[2]}; yaw=options.camera[3]; objectHeading=yaw; pitch=0;
        initialVehicle.position={eyePosition.x+std::sin(yaw)*12,eyePosition.y-2,eyePosition.z+std::cos(yaw)*12};
        initialVehicle.orientation={std::cos(yaw*0.5f),0,std::sin(yaw*0.5f),0};
        vehicleBindPose=render::translate({initialVehicle.position.x,initialVehicle.position.y,initialVehicle.position.z})*render::rotate_y(yaw);
        // Use the same CPU geometry boundary as production GpuModel, preserving
        // packet order for the native renderer's material passes.
        uint64_t totalVertices=0,totalIndices=0,totalPrimitives=0;
        for (const auto& mesh : source.meshes) {
            totalVertices+=mesh.vertices.size(); totalIndices+=mesh.indices.size(); totalPrimitives+=mesh.primitives.size();
            if (totalVertices>1000000 || totalIndices>3000000 || totalPrimitives>maxPrimitives)
                throw "Engine scene exceeds Deko limits";
            for (auto index:mesh.indices) if(index>=mesh.vertices.size()) throw "Invalid scene vertex index";
            for (const auto& p:mesh.primitives)
                if (uint64_t(p.first_index)+p.index_count>mesh.indices.size() || p.index_count%3 ||
                    p.texture< -1 || (p.texture>=0 && size_t(p.texture)>=source.textures.size()))
                    throw "Invalid engine primitive";
        }
        std::vector<scene::Vertex> preparedVertices;
        std::vector<uint32_t> preparedIndices;
        std::vector<render::ModelMeshInfo> preparedMeshes;
        render::assemble_model_geometry(source.meshes,source.textures.size(),false,
            preparedVertices,preparedIndices,preparedMeshes,false);
        std::vector<Vertex> vertices;
        auto indices=std::move(preparedIndices);
        render::Vec3 low{1e20f,1e20f,1e20f}, high{-1e20f,-1e20f,-1e20f};
        size_t meshNumber=0;
        for (const auto& input : source.meshes) {
            const auto& geometry=preparedMeshes[meshNumber];
            Mesh mesh; mesh.lod=input.lod; mesh.transform=render::Mat4::from(input.transform);
            const unsigned role=options.meshRoles.empty() ? 0 : options.meshRoles[meshNumber];
            if (role>4) throw "Invalid mesh role";
            mesh.sky=role==4;
            mesh.car=role==1 || role==3; mesh.brakeVariant=role==3; mesh.backdrop=role==2;
            ++meshNumber;
            if (!options.wheels.empty()) mesh.wheel=options.wheels[meshNumber-1];
            if (mesh.wheel.index< -1 || mesh.wheel.index>3 || (mesh.wheel.index>=0 && !mesh.car) ||
                !std::isfinite(mesh.wheel.center.x)||!std::isfinite(mesh.wheel.center.y)||!std::isfinite(mesh.wheel.center.z)) throw "Invalid scene wheel binding";
            hasWheelBindings|=mesh.wheel.index>=0;
            hasBrakeVariant|=mesh.brakeVariant;
            mesh.vertexBase=uint32_t(vertices.size()); mesh.vertexCount=uint32_t(input.vertices.size()); mesh.firstPrimitive=uint32_t(primitives.size()); mesh.primitiveCount=uint32_t(geometry.primitives.size());
            for (float v : input.transform) if (!std::isfinite(v)) throw "Invalid mesh transform";
            render::Vec3 localLow{1e20f,1e20f,1e20f},localHigh{-1e20f,-1e20f,-1e20f};
            for (const auto& inputVertex : std::span<const scene::Vertex>(preparedVertices).subspan(size_t(geometry.vertex_offset),input.vertices.size())) {
                Vertex v; std::memcpy(v.pos,inputVertex.pos,sizeof(v.pos)); std::memcpy(v.normal,inputVertex.normal,sizeof(v.normal)); std::memcpy(v.uv,inputVertex.uv,sizeof(v.uv)); v.rgba=inputVertex.color;
                for (float x : v.pos) if (!std::isfinite(x)) throw "Invalid vertex position";
                for (float x : v.normal) if (!std::isfinite(x)) throw "Invalid vertex normal";
                for (float x : v.uv) if (!std::isfinite(x)) throw "Invalid vertex UV";
                localLow.x=std::min(localLow.x,v.pos[0]); localLow.y=std::min(localLow.y,v.pos[1]); localLow.z=std::min(localLow.z,v.pos[2]);
                localHigh.x=std::max(localHigh.x,v.pos[0]); localHigh.y=std::max(localHigh.y,v.pos[1]); localHigh.z=std::max(localHigh.z,v.pos[2]);
                const auto p=mesh.transform.transform_point({v.pos[0],v.pos[1],v.pos[2]});
                if (!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) throw "Invalid transformed vertex";
                if (!mesh.sky) {
                    low.x=std::min(low.x,p.x); low.y=std::min(low.y,p.y); low.z=std::min(low.z,p.z);
                    high.x=std::max(high.x,p.x); high.y=std::max(high.y,p.y); high.z=std::max(high.z,p.z);
                }
                vertices.push_back(v);
            }
            if (!input.vertices.empty()) mesh.bounds={(localLow+localHigh)*0.5f,(localHigh-localLow)*0.5f};
            const auto indexBase=geometry.index_offset;
            for (size_t primitiveIndex=0;primitiveIndex<geometry.primitives.size();++primitiveIndex) {
                const auto& in=geometry.primitives[primitiveIndex];
                if (uint64_t(in.first_index)+in.index_count>geometry.line_first || in.index_count%3 || in.texture< -1 || (in.texture>=0 && size_t(in.texture)>=source.textures.size()) || !std::isfinite(in.alpha_ref)) throw "Invalid engine primitive";
                Primitive p;
                if(primitiveIndex<geometry.primitive_spheres.size()) {
                    const auto& sphere=geometry.primitive_spheres[primitiveIndex];
                    p.bounds={sphere.center,{sphere.radius,sphere.radius,sphere.radius}}; p.bounded=true;
                }
                p.first=indexBase+in.first_index; p.count=in.index_count; p.texture=uint32_t(in.texture); p.flags=in.flags; p.alpha=in.alpha_ref;
                for (unsigned k=0;k<3;++k) { p.material[k]=in.material.ambient[k]; p.material[4+k]=in.material.diffuse[k]; p.material[8+k]=in.material.specular[k]; p.material[12+k]=in.material.emission[k]; p.env[k]=in.env_c0[k]; p.env[4+k]=in.env_c1[k]; }
                for (unsigned c=0;c<3;++c) { p.brakeEmission[c]=in.material.lamp_emission[c]; if (!std::isfinite(p.brakeEmission[c])) throw "Invalid brake emission"; }
                p.material[3]=in.material.valid ? (frontendScene && in.material.specular_local ? 3 : !in.material.specular_local) : 0; p.envKind=in.env_kind; p.envPoint=in.env_point; std::copy_n(in.env_point_pos,3,p.envPointPos); std::copy_n(in.env_alpha,2,p.envAlpha); p.material[7]=in.material.scale; p.material[11]=in.material.shininess; p.env[3]=in.env_rows;
                for (float x : p.material) if (!std::isfinite(x)) throw "Invalid material";
                for (float x : p.env) if (!std::isfinite(x)) throw "Invalid environment coefficients";
                if (p.material[7]<0 || p.material[7]>16 || p.material[11]<0 || p.material[11]>65536) throw "Invalid material range";
                primitives.push_back(p);
            }
            meshes.push_back(mesh); originalTransforms.push_back(mesh.transform);
        }
        preparedVertices.clear(); preparedVertices.shrink_to_fit();
        preparedMeshes.clear(); preparedMeshes.shrink_to_fit();
        trace("Native model uses shared GpuModel geometry preparation (packet order)");
        if (vertices.empty() || indices.empty() || primitives.empty()) throw "Empty engine scene";
        for (const auto& t : source.textures) if (!t.width || !t.height || t.width>4096 || t.height>4096 || (!options.textureSource && uint64_t(t.width)*t.height!=t.rgba.size())) throw "Invalid engine texture";
        center=(low+high)*0.5f; radius=std::max(0.5f,render::length(high-low)*0.5f);
        if (!std::isfinite(radius)||radius>100000) throw "Invalid scene bounds";
        {
            scene::Model occlusion;
            for (size_t m=0;m<source.meshes.size();++m) if (!meshes[m].car && !meshes[m].sky) occlusion.meshes.push_back(source.meshes[m]);
            sceneOccluder.add(occlusion); sceneOccluder.build();
        }
        visibleMeshes.resize(meshes.size(),1);
        frontendScene=options.frontend; frameArena=options.frameArena;
        textureUpdates.resize(source.textures.size());
        size_t demoArea=0;
        for (const auto& mesh : meshes) if (mesh.car) for (unsigned i=0;i<mesh.primitiveCount;++i) {
            const auto& primitive=primitives[mesh.firstPrimitive+i];
            if (primitive.texture==UINT32_MAX || (primitive.flags&(1u<<4))) continue;
            const auto& texture=source.textures[primitive.texture];
            if (texture.rgba.size()>demoArea) { demoArea=texture.rgba.size(); demoTexture=int(primitive.texture); }
        }
        if (demoTexture>=0) { demoOriginal=source.textures[size_t(demoTexture)].rgba; demoPixels=demoOriginal; }
        vertexShadow=vertices; initialVertices=vertices;
        vertexVersions.resize(meshes.size(),0);
        for (auto& versions : slotVersions) versions.resize(meshes.size(),0);
        size_t largestCarMesh=0;
        for (const auto& mesh : meshes) if (mesh.car) largestCarMesh=std::max(largestCarMesh,size_t(mesh.vertexCount));
        demoScratch.resize(largestCarMesh);
        vertexBytes = uint32_t(vertices.size()*sizeof(Vertex)); indexBytes = uint32_t(indices.size()*4);
        if(frontendScene) {
            descriptorBytes=aligned(uint32_t((source.textures.size()+1+(options.captureEnvironment ? 6 : 0)+externalImages)*sizeof(DkImageDescriptor)+4096));
            const auto data=render::deko::model_static_layout(vertexBytes,indexBytes,descriptorBytes);
            staticModelMemory=allocate(data.bytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
            vertexMemory=indexMemory=descriptorMemory=staticModelMemory;
            indexOffset=data.index; descriptorOffset=data.descriptors;
            char line[192]; std::snprintf(line,sizeof(line),"Native static model data: bytes=%u memory_blocks=1 index_offset=%u descriptor_offset=%u",data.bytes,indexOffset,descriptorOffset); trace(line);
        } else {
            vertexMemory = allocate(vertexBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            indexMemory = allocate(indexBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        }
        std::memcpy(dkMemBlockGetCpuAddr(vertexMemory), vertices.data(), vertexBytes);
        std::memcpy(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(indexMemory))+indexOffset, indices.data(), indexBytes);
        const auto initialCommandBytes=render::deko::model_upload_command_bytes(options.textureSource ? 0 : uint32_t(source.textures.size()+1),frontendScene);
        uploadCommandsMemory = allocate(initialCommandBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        DkCmdBufMaker cm; dkCmdBufMakerDefaults(&cm, device);
        uploadCommands = dkCmdBufCreate(&cm);
        if (!uploadCommands) throw "Model upload command buffer failed";
        dkCmdBufAddMemory(uploadCommands, uploadCommandsMemory, 0, initialCommandBytes);
        dkCmdBufBarrier(uploadCommands, DkBarrier_None, DkInvalidateFlags_L2Cache);
        if(options.textureSource) {
            const auto& originals=options.textureSource->impl_->textures;
            if(!frontendScene || options.textureSource->impl_->device!=device || originals.size()!=source.textures.size()+1) throw "Invalid shared native texture source";
            textures=originals;
            for(auto& texture:textures) texture.memory=nullptr; // borrowed, owned by the retained source
            trace("Native dynamic geometry borrows original GPU texture images");
        } else {
            if (frontendScene) prepare_static_texture_arena(source);
            add_texture(1,1,3,{0xffffffff}); // texture 0 is the untextured fallback
            for (const auto& texture : source.textures) {
                const unsigned flags=(texture.clamp_u ? 1u : 0u)|(texture.clamp_v ? 2u : 0u)|(texture.no_mips ? 4u : 0u);
                add_texture(texture.width,texture.height,flags,texture.rgba);
            }
        }
        dkCmdBufBarrier(uploadCommands, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache | DkInvalidateFlags_Descriptors);
        char uploadBudgetLog[128];
        std::snprintf(uploadBudgetLog,sizeof(uploadBudgetLog),"Model initial upload command pool: bytes=%u",initialCommandBytes); trace(uploadBudgetLog);
        trace("Model geometry and texture upload commands prepared");
        if (options.captureEnvironment && freeCamera) {
            environmentBase=unsigned(textures.size());
            DkImageLayoutMaker maker; dkImageLayoutMakerDefaults(&maker,device);
            maker.flags=DkImageFlags_UsageRender; maker.format=DkImageFormat_RGBA8_Unorm;
            maker.dimensions[0]=maker.dimensions[1]=128;
            DkImageLayout layout; dkImageLayoutInitialize(&layout,&maker);
            for (unsigned face=0;face<6;++face) {
                textures.emplace_back(); auto& texture=textures.back(); texture.flags=3;
                texture.memory=allocate(uint32_t(dkImageLayoutGetSize(&layout)),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
                dkImageInitialize(&texture.image,&layout,texture.memory,0);
            }
            maker.format=DkImageFormat_Z24X8; dkImageLayoutInitialize(&layout,&maker);
            captureDepthMemory=allocate(uint32_t(dkImageLayoutGetSize(&layout)),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image);
            dkImageInitialize(&captureDepth,&layout,captureDepthMemory,0);
            capturePosition=eyePosition+render::Vec3{std::sin(yaw)*12,-1,std::cos(yaw)*12};
        }
        descriptorBytes=aligned(uint32_t((textures.size()+(frontendScene ? externalImages : 0))*sizeof(DkImageDescriptor)+4096));
        if(!staticModelMemory) descriptorMemory = allocate(descriptorBytes, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
        // Samplers live at the next 4 KiB boundary beyond the image table.
        samplerOffset = aligned(uint32_t((textures.size()+(frontendScene ? externalImages : 0))*sizeof(DkImageDescriptor)));
        // Allocation above always covers this aligned offset and four samplers.
        descriptorShadow.resize(descriptorBytes);
        auto* descriptors = descriptorShadow.data();
        for (unsigned i = 0; i < textures.size(); ++i) {
            DkImageView view; dkImageViewDefaults(&view,&textures[i].image);
            DkImageDescriptor descriptor;
            dkImageDescriptorInitialize(&descriptor,&view,false,false);
            std::memcpy(descriptors+i*sizeof(descriptor),&descriptor,sizeof(descriptor));
        }
        if (frontendScene) for(unsigned i=0;i<externalImages;++i)
            std::memcpy(descriptors+(textures.size()+i)*sizeof(DkImageDescriptor),descriptors,sizeof(DkImageDescriptor));
        for (unsigned i = 0; i < 4 && !sharedSamplerDescriptors; ++i) {
            DkSampler sampler; dkSamplerDefaults(&sampler);
            sampler.minFilter = sampler.magFilter = DkFilter_Linear;
            sampler.maxAnisotropy = 4.0f;
            sampler.wrapMode[0] = (i&1) ? DkWrapMode_ClampToEdge : DkWrapMode_Repeat;
            sampler.wrapMode[1] = (i&2) ? DkWrapMode_ClampToEdge : DkWrapMode_Repeat;
            DkSamplerDescriptor descriptor; dkSamplerDescriptorInitialize(&descriptor,&sampler);
            std::memcpy(descriptors+samplerOffset+i*sizeof(descriptor),&descriptor,sizeof(descriptor));
        }
        std::memcpy(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(descriptorMemory))+descriptorOffset,descriptorShadow.data(),descriptorBytes);
        trace("Model initial upload begin");
        const auto prepareEnd=Clock::now();
        const DkQueue uploadQueue=options.uploadQueue ? options.uploadQueue : queue;
        dkQueueSubmitCommands(uploadQueue,dkCmdBufFinishList(uploadCommands));
        dkQueueWaitIdle(uploadQueue); // only the isolated upload queue for background models
        const auto uploadEnd=Clock::now();
        trace("Model initial upload complete");
        if (!frontendScene) for (size_t index : {1u, 55u, 147u, 209u, 227u, 269u, 365u, 385u, 432u}) {
            if (index >= source.textures.size()) continue;
            const auto& srcTex = source.textures[index];
            const auto& target = textures[index + 1];
            const size_t count = size_t(target.width) * target.height;
            if (!count || count > 1024 || !srcTex.height) continue;
            auto readback = allocate(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            std::memset(dkMemBlockGetCpuAddr(readback), 0xcd, count * 4);
            dkCmdBufClear(uploadCommands);
            dkCmdBufAddMemory(uploadCommands, uploadCommandsMemory, 0, initialCommandBytes);
            dkCmdBufBarrier(uploadCommands, DkBarrier_Full, DkInvalidateFlags_L2Cache | DkInvalidateFlags_Image);
            DkImageView view; dkImageViewDefaults(&view, &target.image);
            DkImageRect rect{0, 0, 0, target.width, target.height, 1};
            DkCopyBuf dst{dkMemBlockGetGpuAddr(readback), 0, 0};
            dkCmdBufCopyImageToBuffer(uploadCommands, &view, &rect, &dst, 0);
            dkCmdBufBarrier(uploadCommands, DkBarrier_Full, DkInvalidateFlags_L2Cache);
            dkQueueSubmitCommands(queue, dkCmdBufFinishList(uploadCommands));
            dkQueueWaitIdle(queue);
            const auto* actual = static_cast<const uint32_t*>(dkMemBlockGetCpuAddr(readback));
            size_t mismatch = 0, firstBad = count;
            uint32_t badCpu = 0, badGpu = 0;
            for (size_t p = 0; p < count; ++p) {
                const uint32_t y = uint32_t(p / target.width);
                const uint32_t x = uint32_t(p % target.width);
                const uint32_t srcY = (y * srcTex.height) / target.height;
                const uint32_t exp = srcTex.rgba[size_t(srcY) * srcTex.width + x];
                if (exp != actual[p]) {
                    if (!mismatch) { firstBad = p; badCpu = exp; badGpu = actual[p]; }
                    ++mismatch;
                }
            }
            char line[256];
            std::snprintf(line, sizeof(line), "Static texture upload check: id=%zu size=%ux%u mismatch=%zu first_bad=%zu cpu=%08x gpu=%08x row0=%08x row8=%08x",
                index, target.width, target.height, mismatch, firstBad, badCpu, badGpu, actual[0], count > 256 ? actual[256] : 0u);
            trace(line);
            dkMemBlockDestroy(readback);
            allocatedBytes -= 4096;
        }
        dkCmdBufDestroy(uploadCommands); uploadCommands = nullptr;
        dkMemBlockDestroy(uploadCommandsMemory); uploadCommandsMemory = nullptr;
        for (auto memory : staging) dkMemBlockDestroy(memory);
        staging.clear(); staticTextureUpload=nullptr;
        frameBudget=render::deko::model_buffer_budget(uint32_t(primitives.size()),frontendScene);
        if(frameArena) frameBudget.slots=slots;
        for (unsigned i = 0; i < (frameArena ? 0u : frameBudget.slots); ++i) {
            commandMemory[i] = allocate(frameBudget.commands,DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            lightUniforms[i]=allocate(6*256,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
            fogUniforms[i]=allocate(256,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
            uniforms[i] = allocate(frameBudget.draws*256,DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
            commands[i] = dkCmdBufCreate(&cm);
            if (!commands[i]) throw "Model draw command buffer failed";
        }
        char budgetLog[192]; std::snprintf(budgetLog,sizeof(budgetLog),"Native model frame budget: slots=%u draws=%u command_bytes_per_slot=%u",frameBudget.slots,frameBudget.draws,frameBudget.commands); trace(budgetLog);
        char summary[320];
        std::snprintf(summary,sizeof(summary),"Engine scene upload: meshes=%zu vertices=%zu triangles=%zu primitives=%zu textures=%zu prepare_ms=%.2f upload_wait_ms=%.2f total_ms=%.2f cumulative_alloc_bytes=%llu",
            meshes.size(),vertices.size(),indices.size()/3,primitives.size(),source.textures.size(),milliseconds(begin,prepareEnd),milliseconds(prepareEnd,uploadEnd),milliseconds(begin,Clock::now()),(unsigned long long)allocatedBytes);
        trace(summary);
    }
    bool set_mesh_transform(size_t index,const render::Mat4& transform) {
        if (!vertexMemory || index>=meshes.size()) return false;
        for (float value : transform.m) if (!std::isfinite(value)) return false;
        meshes[index].transform=transform; ++transformUpdates; return true;
    }
    bool set_camera(const render::Mat4& view,const render::Mat4& projection,const render::Vec3& eye) {
        for (float value : view.m) if (!std::isfinite(value)) return false;
        for (float value : projection.m) if (!std::isfinite(value)) return false;
        if (!std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z)) return false;
        frameView=view; frameProjection=projection; frameEye=eye; externalCamera=true; ++cameraUpdates; return true;
    }
    void configure_cars(unsigned count) {
        if (!count || count>carFrames.size() || carCount!=1 || preparedAllocationBytes) throw "Invalid car instance configuration";
        const auto originalCount=meshes.size();
        for (unsigned car=1;car<count;++car) for (size_t m=0;m<originalCount;++m) if (meshes[m].car) {
            if (meshes.size()>=1024) throw "Car instances exceed mesh limit";
            auto mesh=meshes[m]; mesh.carIndex=car;
            const auto transform=originalTransforms[m];
            meshes.push_back(mesh); originalTransforms.push_back(transform);
        }
        carCount=count; vertexVersions.resize(meshes.size(),0); visibleMeshes.resize(meshes.size(),1);
        for (auto& versions:slotVersions) versions.resize(meshes.size(),0);
        char line[160]; std::snprintf(line,sizeof(line),"Car instances: count=%u shared_geometry=1 shared_textures=1 meshes=%zu",carCount,meshes.size()); trace(line);
    }
    bool apply_vehicle_state(const sim::VehicleState& state,const sim::VehicleSetup& setup,unsigned car) {
        if (car>=carCount) return false;
        const auto& q=state.orientation; const auto& p=state.position;
        const float norm=q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z;
        if (!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(norm)||norm<0.00001f) return false;
        const float inv=1/std::sqrt(norm);
        const auto pose=render::translate({p.x,p.y,p.z})*render::rotate_quat(q.w*inv,q.x*inv,q.y*inv,q.z*inv);
        const auto inverseBind=render::inverse(vehicleBindPose);
        for (const auto& wheel:state.wheels) if (!std::isfinite(wheel.steer_angle)||!std::isfinite(wheel.spin_angle)||!std::isfinite(wheel.suspension)||!std::isfinite(wheel.camber)) return false;
        for (size_t m=0;m<meshes.size();++m) if (meshes[m].car && meshes[m].carIndex==car) {
            render::Mat4 delta;
            const auto& binding=meshes[m].wheel;
            if (hasWheelBindings && binding.index>=0) {
                const size_t index=size_t(binding.index); const auto& wheel=state.wheels[index];
                if (wheelMotion) delta=wheel_delta(unsigned(index),binding.center,wheel,setup.wheels[index],binding.spin);
                ++wheelUpdates;
                maximumSteer=std::max(maximumSteer,std::abs(wheel.steer_angle));
                maximumSuspensionTravel=std::max(maximumSuspensionTravel,std::abs(wheel.suspension-setup.wheels[index].static_suspension));
                maximumSpinStep=std::max(maximumSpinStep,std::abs(wheel.spin_step));
            } else if (hasWheelBindings) delta=render::translate({0,car_body_lift(setup),0});
            if (!set_mesh_transform(m,pose*delta*inverseBind*originalTransforms[m])) return false;
        }
        return true;
    }
    void demo_object_transform(float phase) {
        const float offset=phase<0 ? 0 : 2.0f*std::sin(phase);
        const auto motion=render::translate({std::sin(objectHeading)*offset,0,std::cos(objectHeading)*offset});
        for (size_t m=0;m<meshes.size();++m) if (meshes[m].car) {
            if (!set_mesh_transform(m,motion*originalTransforms[m])) throw "Object transform update failed";
        }
    }
    void update_car_lights(const sim::VehicleState& state,int lampHint,unsigned car) {
        if (car>=carCount) throw "Invalid car light instance";
        auto& carLights=carFrames[car].lights;
        carLights.fill(0);
        if (!courseLights.active() || !environment.has_env) return;
        const auto selected=courseLights.select(environment,{state.position.x,state.position.y,state.position.z},lampHint);
        for (unsigned c=0;c<3;++c) carLights[c]=environment.global_ambient[c];
        unsigned light=0;
        for (const auto& lamp:selected) {
            if (light>=4 || lamp.type<0 || size_t(lamp.type)>=environment.lamps.size()) continue;
            const auto& type=environment.lamps[size_t(lamp.type)];
            const float direction[3]={lamp.dir.x,lamp.dir.y,lamp.dir.z};
            for (unsigned c=0;c<3;++c) {
                carLights[c]+=type.ambient[c]*lamp.w;
                carLights[4+light*4+c]=direction[c];
                carLights[20+light*4+c]=type.diffuse[c]*lamp.w;
                carLights[36+light*4+c]=type.specular[c]*lamp.w;
            }
            carLights[4+light*4+3]=1; ++light;
        }
        maximumLights=std::max(maximumLights,light); ++lightUpdates;
    }
    void reset_course_animations() {
        for (auto& set:courseCluts) { set.textures.player().reset(); set.textures.player().start_signal(0); }
        if (courseMotion) { courseMotion->reset(); upload_course_motion(); }
    }
    void upload_course_motion() {
        courseMotion->emit([&](size_t index,const scene::Mesh& mesh) {
            if (!update_vertices(index,mesh.vertices)) throw "Moving course vertex update failed";
            meshes[index].transform=render::Mat4::from(mesh.transform);
            ++movingUpdates;
        });
    }
    void load_course_animations(std::span<const uint8_t> courseBytes) {
        const auto file=course::CourseFile::parse(courseBytes);
        courseLights.load(file.section(courseBytes,28));
        environment=race::SunParams::from_env0(file.section(courseBytes,32));
        char fogLog[192]; std::snprintf(fogLog,sizeof(fogLog),"Original course fog: A=%.3f b=%.3f RGB=%.3f/%.3f/%.3f",environment.fog_a,environment.fog_b,environment.fog_rgb[0],environment.fog_rgb[1],environment.fog_rgb[2]); trace(fogLog);
        char lightLog[160]; std::snprintf(lightLog,sizeof(lightLog),"Course lighting loaded: lamps=%zu point_active=%d env_valid=%d",courseLights.lamps().size(),int(courseLights.active()),int(environment.has_env)); trace(lightLog);
        if (courseMotion) {
            reset_course_animations();
            char line[128]; std::snprintf(line,sizeof(line),"Original course animations loaded: palette_sets=%zu moving_groups=%zu",courseCluts.size(),courseMotion->groups.size()); trace(line);
            return;
        }
        course::CourseModelOptions options; options.course_line=false; options.backdrop=true;
        std::vector<gtm1::ClutSet> sets;
        const auto source=course::build_course_model(courseBytes,options,"route5_dry",&sets);
        if (source.textures.size()>textureUpdates.size()) throw "Course animation texture count mismatch";
        for (size_t t=0;t<source.textures.size();++t) {
            const uint32_t expectedH = source.textures[t].height < 16 ? 16u : source.textures[t].height;
            if (source.textures[t].width!=textures[t+1].width || expectedH!=textures[t+1].height) throw "Course animation texture layout mismatch";
        }
        size_t records=0;
        for (const auto& set:sets) for (auto record:set.textures.records()) {
            if (set.first<0 || size_t(set.first)+record>=source.textures.size()) throw "Course animation record mismatch";
            ++records;
        }
        courseCluts=std::move(sets); reset_course_animations();
        char line[160]; std::snprintf(line,sizeof(line),"Original course CLUT animations loaded: sets=%zu records=%zu texture_order=uncompacted",courseCluts.size(),records); trace(line);
    }
    void animate_course(float seconds) {
        if (courseMotion && courseMotion->advance(seconds)) upload_course_motion();
        for (auto& set:courseCluts) {
            set.textures.player().update(seconds);
            for (auto record:set.textures.refresh()) {
                const auto* rgba=set.textures.rgba(record);
                if (!rgba || !update_texture(size_t(set.first)+record,*rgba)) throw "Course palette update failed";
                ++paletteUpdates;
            }
        }
    }
    bool update_texture(size_t index,std::span<const uint32_t> rgba) {
        if (!vertexMemory || index>=textureUpdates.size()) return false;
        const auto& texture=textures[index+1];
        if (rgba.size()!=size_t(texture.width)*texture.height) return false;
        auto& update=textureUpdates[index];
        update.pixels.assign(rgba.begin(),rgba.end()); ++update.version; ++textureUpdateCalls;
        return true;
    }
    void ensure_frame_descriptors(unsigned slot) {
        if(frameArena) {
            if(frameDescriptorShadows[slot].empty()) frameDescriptorShadows[slot]=descriptorShadow;
        } else if(!frameDescriptors[slot]) {
            frameDescriptors[slot]=allocate(descriptorBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
            std::memcpy(dkMemBlockGetCpuAddr(frameDescriptors[slot]),descriptorShadow.data(),descriptorBytes);
            ++textureDescriptorAllocations;
        }
    }
    uint8_t* frame_descriptor_cpu(unsigned slot) {
        return frameArena ? frameDescriptorShadows[slot].data() : static_cast<uint8_t*>(dkMemBlockGetCpuAddr(frameDescriptors[slot]));
    }
    void ensure_texture_slot(size_t index,unsigned slot) {
        auto& update=textureUpdates[index]; auto& target=update.slots[slot];
        if (target.memory) return;
        ensure_frame_descriptors(slot);
        const auto& original=textures[index+1];
        DkImageLayoutMaker maker; dkImageLayoutMakerDefaults(&maker,device);
        maker.format=DkImageFormat_RGBA8_Unorm; maker.dimensions[0]=original.width; maker.dimensions[1]=original.height;
        DkImageLayout layout; dkImageLayoutInitialize(&layout,&maker);
        target.memory=allocate(uint32_t(dkImageLayoutGetSize(&layout)),DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image); ++textureImageAllocations;
        dkImageInitialize(&target.image,&layout,target.memory,0);
        target.upload=allocate(original.width*original.height*4,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached); ++textureStagingAllocations;
        // Allocation does not publish the replacement: its pixels are undefined
        // until a normal animated texture upload has been recorded.
    }
    void inspect_course_textures() {
        trace("Course texture readback begin: explicit diagnostic pause, not normal rendering");
        dkQueueWaitIdle(queue);
        inFlight[captureSlot]=false;
        const auto memory=allocate(4096,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
        for (size_t index : {217u,218u,255u,256u,312u,313u,376u,377u,423u,424u,438u,439u}) {
            if (index>=textureUpdates.size()) continue;
            const auto& original=textures[index+1]; const size_t count=size_t(original.width)*original.height;
            if (!count || count>1024) continue;
            for (unsigned slot=0;slot<slots;++slot) {
                const auto& target=textureUpdates[index].slots[slot];
                if (!target.memory || !target.upload || !target.version) continue;
                std::memset(dkMemBlockGetCpuAddr(memory),0xcd,count*4);
                auto cmd=commands[captureSlot]; dkCmdBufClear(cmd); dkCmdBufAddMemory(cmd,commandMemory[captureSlot],0,commandBytes);
                dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_L2Cache|DkInvalidateFlags_Image);
                DkImageView view; dkImageViewDefaults(&view,&target.image);
                DkImageRect rectangle{0,0,0,original.width,original.height,1};
                DkCopyBuf destination{dkMemBlockGetGpuAddr(memory),0,0};
                dkCmdBufCopyImageToBuffer(cmd,&view,&rectangle,&destination,0);
                dkCmdBufBarrier(cmd,DkBarrier_Full,DkInvalidateFlags_L2Cache);
                dkQueueSubmitCommands(queue,dkCmdBufFinishList(cmd)); dkQueueWaitIdle(queue);
                const auto* expected=static_cast<const uint32_t*>(dkMemBlockGetCpuAddr(target.upload));
                const auto* actual=static_cast<const uint32_t*>(dkMemBlockGetCpuAddr(memory));
                size_t mismatch=0; unsigned alphaLow=255,alphaHigh=0;
                uint64_t alphaTotal=0;
                for (size_t p=0;p<count;++p) {
                    mismatch+=expected[p]!=actual[p]; const unsigned alpha=actual[p]>>24;
                    alphaLow=std::min(alphaLow,alpha); alphaHigh=std::max(alphaHigh,alpha); alphaTotal+=alpha;
                }
                DkImageDescriptor descriptor; dkImageDescriptorInitialize(&descriptor,&view,false,false);
                const auto* table=static_cast<const uint8_t*>(dkMemBlockGetCpuAddr(frameDescriptors[slot]));
                const bool descriptorMatch=std::memcmp(table+(index+1)*sizeof(descriptor),&descriptor,sizeof(descriptor))==0;
                char line[320]; std::snprintf(line,sizeof(line),"Course texture readback: id=%zu slot=%u version=%llu size=%ux%u mismatched_pixels=%zu descriptor_match=%d CPU0=%08x GPU0=%08x alpha=%u..%u mean_alpha=%.2f",index,slot,(unsigned long long)target.version,original.width,original.height,mismatch,int(descriptorMatch),expected[0],actual[0],alphaLow,alphaHigh,double(alphaTotal)/count); trace(line);
            }
        }
        dkMemBlockDestroy(memory);
        trace("Course texture readback complete");
    }
    void publish_texture_slot(size_t index,unsigned slot) {
        const auto& target=textureUpdates[index].slots[slot];
        DkImageView view; dkImageViewDefaults(&view,&target.image);
        DkImageDescriptor descriptor; dkImageDescriptorInitialize(&descriptor,&view,false,false);
        auto* table=frame_descriptor_cpu(slot);
        std::memcpy(table+(index+1)*sizeof(descriptor),&descriptor,sizeof(descriptor));
    }
    void prepare_dynamic_resources() {
        for (bool active:inFlight) if (active) throw "Dynamic preparation must precede rendering";
        const auto begin=Clock::now(); const auto allocationBegin=allocatedBytes;
        // Reserve storage without seeding animation state or selecting undefined
        // replacement images. The normal update path performs their first copy.
        for (unsigned slot=0;slot<frameBudget.slots;++slot) {
            if (!dynamicVertices[slot]) {
                dynamicVertices[slot]=allocate(vertexBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
                ++vertexSlotAllocations;
            }
            std::memcpy(dkMemBlockGetCpuAddr(dynamicVertices[slot]),vertexShadow.data(),vertexBytes);
            slotVersions[slot]=vertexVersions; vertexCopyBytes+=vertexBytes;
            for (const auto& set:courseCluts) for (auto record:set.textures.records()) {
                if (set.first<0 || size_t(set.first)+record>=textureUpdates.size()) throw "Invalid prepared texture record";
                ensure_texture_slot(size_t(set.first)+record,slot);
            }
            auto cmd=commands[slot]; dkCmdBufClear(cmd); dkCmdBufAddMemory(cmd,commandMemory[slot],0,frameBudget.commands);
            dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_L2Cache|DkInvalidateFlags_Image|DkInvalidateFlags_Descriptors);
            dkQueueSubmitCommands(queue,dkCmdBufFinishList(cmd));
            dkQueueWaitIdle(queue); // loading phase only; resources ready before gameplay
        }
        preparedAllocationBytes=allocatedBytes;
        char line[224]; std::snprintf(line,sizeof(line),"Dynamic resources prepared: mode=reserve_only slots=%u vertices=%llu texture_images=%llu allocated_bytes=%llu wall_ms=%.3f",frameBudget.slots,(unsigned long long)vertexSlotAllocations,(unsigned long long)textureImageAllocations,(unsigned long long)(allocatedBytes-allocationBegin),milliseconds(begin,Clock::now())); trace(line);
    }
    void demo_texture_color(float phase) {
        if (demoTexture<0) return;
        const bool restore=phase<0;
        const float green=restore ? 1 : 0.55f+0.45f*std::sin(phase);
        const float blue=restore ? 1 : 0.55f-0.45f*std::sin(phase);
        for (size_t i=0;i<demoOriginal.size();++i) {
            const uint32_t pixel=demoOriginal[i];
            demoPixels[i]=(pixel&0xff0000ffu)|(uint32_t(float((pixel>>8)&255)*green)<<8)|(uint32_t(float((pixel>>16)&255)*blue)<<16);
        }
        if (!update_texture(size_t(demoTexture),demoPixels)) throw "Demo texture update failed";
    }
    bool update_vertices(size_t meshIndex,std::span<const scene::Vertex> input) {
        if (!vertexMemory || meshIndex>=meshes.size() || input.size()!=meshes[meshIndex].vertexCount) return false;
        for (const auto& v : input) {
            for (float x : v.pos) if (!std::isfinite(x)) return false;
            for (float x : v.normal) if (!std::isfinite(x)) return false;
            for (float x : v.uv) if (!std::isfinite(x)) return false;
        }
        const auto& mesh=meshes[meshIndex];
        for (size_t i=0;i<input.size();++i) {
            auto& dest=vertexShadow[mesh.vertexBase+i]; const auto& source=input[i];
            std::memcpy(dest.pos,source.pos,sizeof(dest.pos)); std::memcpy(dest.normal,source.normal,sizeof(dest.normal));
            std::memcpy(dest.uv,source.uv,sizeof(dest.uv)); dest.rgba=source.color;
        }
        if (!input.empty()) {
            render::Vec3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
            for (const auto& v:input) {
                lo.x=std::min(lo.x,v.pos[0]); lo.y=std::min(lo.y,v.pos[1]); lo.z=std::min(lo.z,v.pos[2]);
                hi.x=std::max(hi.x,v.pos[0]); hi.y=std::max(hi.y,v.pos[1]); hi.z=std::max(hi.z,v.pos[2]);
            }
            meshes[meshIndex].bounds={(lo+hi)*0.5f,(hi-lo)*0.5f};
        }
        ++vertexVersions[meshIndex]; ++updateCalls; return true;
    }
    void demo_vertex_offset(float height) {
        for (size_t m=0;m<meshes.size();++m) {
            const auto& mesh=meshes[m]; if (!mesh.car) continue;
            const auto inverse=render::inverse(mesh.transform);
            const auto offset=inverse.transform_point({0,height,0})-inverse.transform_point({0,0,0});
            for (size_t i=0;i<mesh.vertexCount;++i) {
                const auto& source=initialVertices[mesh.vertexBase+i]; auto& dest=demoScratch[i];
                std::memcpy(dest.pos,source.pos,sizeof(dest.pos)); dest.pos[0]+=offset.x; dest.pos[1]+=offset.y; dest.pos[2]+=offset.z;
                std::memcpy(dest.normal,source.normal,sizeof(dest.normal)); std::memcpy(dest.uv,source.uv,sizeof(dest.uv)); dest.color=source.rgba;
            }
            if (!update_vertices(m,std::span<const scene::Vertex>(demoScratch.data(),mesh.vertexCount))) throw "Demo vertex update failed";
        }
    }
    void capture_environment() {
        if (environmentBase) {
            const auto captureBegin=Clock::now();
            for (unsigned face=0;face<6;++face) {
                char stage[80]; std::snprintf(stage,sizeof(stage),"Environment capture face %u begin",face); trace(stage);
                submit(captureSlot,0,128,128,true,face);
                dkQueueWaitIdle(queue);
                std::snprintf(stage,sizeof(stage),"Environment capture face %u complete",face); trace(stage);
            }
            dkQueueWaitIdle(queue); // explicit static capture, never part of regular per-frame rendering
            environmentReady=true;
            environmentFace=0; environmentFrame=0;
            char captureLog[160];
            std::snprintf(captureLog,sizeof(captureLog),"Static environment captured: six 128x128 faces, car excluded, wall_ms=%.2f",milliseconds(captureBegin,Clock::now()));
            trace(captureLog);
        }
    }
    void update_environment(unsigned slot,const render::Vec3& origin) {
        if (slot>=sceneSlots) throw "Invalid scene slot for live environment";
        if (!environmentBase || ++environmentFrame%10) return;
        // Never block the CPU for capture resources. If the previous face is
        // still running, leave this face pending until the next scheduled tick.
        if (inFlight[captureSlot] && dkFenceWait(&fences[captureSlot],0)!=DkResult_Success) {
            if (dkQueueIsInErrorState(queue)) throw "Environment capture queue failed";
            ++liveCaptureSkipped; return;
        }
        // One queue orders previous sampling, face rendering and new sampling.
        // Capture owns commands, uniforms and dynamic upload storage separately.
        if (!environmentFace) capturePosition=origin;
        const auto begin=Clock::now();
        submit(captureSlot,0,128,128,true,environmentFace);
        const auto elapsed=milliseconds(begin,Clock::now());
        liveCaptureTotal+=elapsed; liveCaptureMax=std::max(liveCaptureMax,elapsed);
        ++liveCaptureFaces; environmentFace=(environmentFace+1)%6;
        if (!environmentReady && !environmentFace) {
            // All six writes precede the scene on the same queue. Its image
            // barrier makes them visible before the first reflection sampling.
            environmentReady=true;
            trace("Initial environment ready: six incrementally queued faces, no CPU wait");
        }
    }
    uint32_t samplerOffset{};
    void submit(unsigned slot, float angle, unsigned width, unsigned height, bool capturing=false, unsigned face=0,
                const DkImage* viewColor=nullptr, const DkImage* viewDepth=nullptr, const float* clear=nullptr,
                const render::DrawOptions* frontend=nullptr, const DkImageDescriptor* sphere=nullptr,
                const render::DrawItem* object=nullptr, bool clearTarget=true, bool drawGeometry=true, const DkImageDescriptor* cubeFaces=nullptr, const DkImageDescriptor* imageOverride=nullptr, unsigned phase=0,bool post=false) {
        if(frameArena) slot=frameArena->begin();
        if (slot >= frameBudget.slots) throw "Model frame slot out of range";
        const auto waitBegin=Clock::now();
        bool changingTexture=false;
        if(frameArena && arenaGenerations[slot]==frameArena->generation())
            for(const auto& update:textureUpdates) if(update.slots[slot].version && update.version!=update.slots[slot].version) changingTexture=true;
        if (inFlight[slot] && (!frameArena || changingTexture)) {
            // SignalFence's flush argument flushes GPU caches, not the CPU queue.
            // Showrooms reuse model slots several times before PresentImage kicks it.
            dkQueueFlush(queue);
            if (frontend && measuredFrames<4) trace("Native model slot reuse: queue dispatched before CPU fence wait");
            if (dkFenceWait(&fences[slot],-1) != DkResult_Success) throw "Model frame fence failed";
        }
        const auto recordBegin=Clock::now();
        if (!capturing && brakeFactor>0) ++brakeFrames;
        const double wait=milliseconds(waitBegin,recordBegin);
        if (!capturing) { waitTotal+=wait; waitMax=std::max(waitMax,wait); }
        bool changedVertices=false;
        const bool hasUpdates=updateCalls!=0;
        if(frameArena && hasUpdates) {
            if(arenaGenerations[slot]!=frameArena->generation() || slotVersions[slot]!=vertexVersions) {
                arenaVertices[slot]=frameArena->allocate(vertexBytes);
                std::memcpy(arenaVertices[slot].cpu(),vertexShadow.data(),vertexBytes);
                slotVersions[slot]=vertexVersions; vertexCopyBytes+=vertexBytes; changedVertices=true;
            }
        } else if (hasUpdates) {
            if (!dynamicVertices[slot]) {
                dynamicVertices[slot]=allocate(vertexBytes,DkMemBlockFlags_CpuUncached|DkMemBlockFlags_GpuCached);
                std::memcpy(dkMemBlockGetCpuAddr(dynamicVertices[slot]),vertexShadow.data(),vertexBytes);
                slotVersions[slot]=vertexVersions; vertexCopyBytes+=vertexBytes; ++vertexSlotAllocations; changedVertices=true;
            } else {
                auto* destination=static_cast<uint8_t*>(dkMemBlockGetCpuAddr(dynamicVertices[slot]));
                for (size_t m=0;m<meshes.size();++m) if (slotVersions[slot][m]!=vertexVersions[m]) {
                    const auto& mesh=meshes[m]; const size_t bytes=size_t(mesh.vertexCount)*sizeof(Vertex);
                    std::memcpy(destination+size_t(mesh.vertexBase)*sizeof(Vertex),vertexShadow.data()+mesh.vertexBase,bytes);
                    vertexCopyBytes+=bytes; slotVersions[slot][m]=vertexVersions[m]; changedVertices=true;
                }
            }
        }
        if(frameArena && !commands[slot]) {
            DkCmdBufMaker maker; dkCmdBufMakerDefaults(&maker,device); commands[slot]=dkCmdBufCreate(&maker);
            if(!commands[slot]) throw "Native model command object failed";
        }
        const auto arenaCommands=frameArena ? frameArena->allocate(frameBudget.commands) : FrameArena::Slice{};
        const auto arenaUniforms=frameArena ? frameArena->allocate(frameBudget.draws*256) : FrameArena::Slice{};
        const auto arenaLights=frameArena ? frameArena->allocate(6*256) : FrameArena::Slice{};
        const auto arenaFog=frameArena ? frameArena->allocate(256) : FrameArena::Slice{};
        auto cmd = commands[slot];
        dkCmdBufClear(cmd); dkCmdBufAddMemory(cmd,frameArena ? arenaCommands.memory : commandMemory[slot],frameArena ? arenaCommands.offset : 0,frameBudget.commands);
        if (!capturing && liveCaptureFaces) dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_Image);
        if (frontend) carFrames[0].lights=render::deko::scene_lights(*frontend,object);
        const auto lightBuffer=frameArena ? arenaLights.gpu() : dkMemBlockGetGpuAddr(lightUniforms[slot]);
        for (unsigned car=0;car<carCount;++car)
            dkCmdBufPushConstants(cmd,lightBuffer+car*256,256,0,sizeof(carFrames[car].lights),carFrames[car].lights.data());
        dkCmdBufBindUniformBuffer(cmd,DkStage_Vertex,1,lightBuffer,256);
        if (changedVertices && !frameArena) dkCmdBufBarrier(cmd,DkBarrier_None,DkInvalidateFlags_L2Cache);
        bool uploadedTexture=false;
        if (textureUpdateCalls) {
            ensure_frame_descriptors(slot);
            dkCmdBufBarrier(cmd,DkBarrier_None,DkInvalidateFlags_L2Cache);
            for (size_t index=0;index<textureUpdates.size();++index) {
                auto& update=textureUpdates[index]; auto& target=update.slots[slot];
                if (!update.version || update.version==target.version) continue;
                const auto& original=textures[index+1];
                ensure_texture_slot(index,slot);
                const size_t bytes=update.pixels.size()*4;
                std::memcpy(dkMemBlockGetCpuAddr(target.upload),update.pixels.data(),bytes);
                DkCopyBuf source{dkMemBlockGetGpuAddr(target.upload),0,0};
                DkImageView view; dkImageViewDefaults(&view,&target.image);
                DkImageRect rectangle{0,0,0,original.width,original.height,1};
                dkCmdBufCopyBufferToImage(cmd,&source,&view,&rectangle,0);
                publish_texture_slot(index,slot);
                target.version=update.version; textureCopyBytes+=bytes; uploadedTexture=true;
            }
            if (uploadedTexture) dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_Image|DkInvalidateFlags_L2Cache|DkInvalidateFlags_Descriptors);
        }
        DkGpuAddr arenaDescriptors=0;
        if (frontend) {
            ensure_frame_descriptors(slot);
            const auto descriptorSlice=frameArena ? frameArena->allocate(descriptorBytes) : FrameArena::Slice{};
            if(frameArena) {
                std::memcpy(descriptorSlice.cpu(),frame_descriptor_cpu(slot),descriptorBytes);
                arenaDescriptors=descriptorSlice.gpu();
            }
            auto* descriptors=static_cast<uint8_t*>(frameArena ? descriptorSlice.cpu() : dkMemBlockGetCpuAddr(frameDescriptors[slot]));
            const auto* fallback=descriptorShadow.data();
            for(unsigned image=0;image<externalImages;++image) {
                const DkImageDescriptor* source=image==0 ? sphere : image==7 ? imageOverride : cubeFaces ? cubeFaces+image-1 : nullptr;
                std::memcpy(descriptors+(textures.size()+image)*sizeof(DkImageDescriptor),source ? static_cast<const void*>(source) : fallback,sizeof(DkImageDescriptor));
            }
            // Arena descriptors are immutable, cache-line-disjoint snapshots.
            // Their CPU visibility is prepared once per frame, before submit.
            // Changing descriptor addresses still invalidates header/sampler
            // caches; texture data visibility belongs to the view/upload edges.
            dkCmdBufBarrier(cmd,frameArena ? DkBarrier_None : DkBarrier_Fragments,
                DkInvalidateFlags_Descriptors | (frameArena ? 0u : DkInvalidateFlags_L2Cache|DkInvalidateFlags_Image));
        }
        const auto descriptors=frameDescriptors[slot] ? frameDescriptors[slot] : descriptorMemory;
        if (capturing) {
            DkImageView target,depth; dkImageViewDefaults(&target,&textures[environmentBase+face].image); dkImageViewDefaults(&depth,&captureDepth);
            dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_Image);
            dkCmdBufBindRenderTarget(cmd,&target,&depth);
        }
        if (!capturing && viewColor && (!frameArena || clearTarget)) {
            if (!viewDepth) throw "Native view depth target is missing";
            DkImageView color,depth;
            dkImageViewDefaults(&color,viewColor); dkImageViewDefaults(&depth,viewDepth);
            dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_Image);
            dkCmdBufBindRenderTarget(cmd,&color,&depth);
        }
        DkViewport viewport{0,0,float(width),float(height),0,1};
        DkScissor scissor{0,0,width,height};
        dkCmdBufSetViewports(cmd,0,&viewport,1); dkCmdBufSetScissors(cmd,0,&scissor,1);
        if (clearTarget) {
            dkCmdBufClearColorFloat(cmd,0,DkColorMask_RGBA,clear ? clear[0] : 0.07f,clear ? clear[1] : 0.09f,clear ? clear[2] : 0.13f,clear ? clear[3] : 1);
            dkCmdBufClearDepthStencil(cmd,true,0,0,0);
        }
        DkRasterizerState raster; dkRasterizerStateDefaults(&raster); raster.cullMode = DkFace_None;
        dkCmdBufBindRasterizerState(cmd,&raster);
        DkColorWriteState write; dkColorWriteStateDefaults(&write); dkCmdBufBindColorWriteState(cmd,&write);
        const DkShader* shaders[]{vertexShader,capturing ? captureShader : fragmentShader};
        dkCmdBufBindShaders(cmd,DkStageFlag_GraphicsMask,shaders,2);
        const DkShader* boundFragment=capturing ? captureShader : fragmentShader;
        DkVtxAttribState attributes[4]{};
        const DkVtxAttribSize sizes[]{DkVtxAttribSize_3x32,DkVtxAttribSize_3x32,DkVtxAttribSize_2x32,DkVtxAttribSize_4x8};
        const uint32_t offsets[]{0,12,24,32};
        for (unsigned i = 0; i < 4; ++i) {
            attributes[i].offset=offsets[i]; attributes[i].size=sizes[i];
            attributes[i].type=i==3 ? DkVtxAttribType_Unorm : DkVtxAttribType_Float;
        }
        DkVtxBufferState buffer{sizeof(Vertex),0};
        dkCmdBufBindVtxAttribState(cmd,attributes,4); dkCmdBufBindVtxBufferState(cmd,&buffer,1);
        dkCmdBufBindVtxBuffer(cmd,0,frameArena && hasUpdates ? arenaVertices[slot].gpu() : dkMemBlockGetGpuAddr(hasUpdates ? dynamicVertices[slot] : vertexMemory),vertexBytes);
        dkCmdBufBindIdxBuffer(cmd,DkIdxFormat_Uint32,dkMemBlockGetGpuAddr(indexMemory)+indexOffset);
        dkCmdBufBindImageDescriptorSet(cmd,frameArena ? arenaDescriptors : dkMemBlockGetGpuAddr(descriptors)+(descriptors==descriptorMemory ? descriptorOffset : 0),uint32_t(textures.size()+(frontendScene ? externalImages : 0)));
        const DkGpuAddr samplerDescriptors = sharedSamplerDescriptors
            ? sharedSamplerDescriptors
            : (frameArena ? arenaDescriptors : dkMemBlockGetGpuAddr(descriptors)+(descriptors==descriptorMemory ? descriptorOffset : 0))+samplerOffset;
        dkCmdBufBindSamplerDescriptorSet(cmd,samplerDescriptors,4);
        const bool explicitCamera=externalCamera && !capturing;
        auto eye = explicitCamera ? frameEye : center + render::Vec3{std::sin(angle)*radius*2.8f,radius*1.1f,std::cos(angle)*radius*2.8f};
        auto target=center;
        if (freeCamera && !explicitCamera) {
            eye=eyePosition;
            target=eye+render::Vec3{std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)};
        }
        render::Vec3 up{0,1,0};
        if (capturing) {
            static const render::Vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            static const render::Vec3 ups[]={{0,1,0},{0,1,0},{0,0,1},{0,0,-1},{0,1,0},{0,1,0}};
            eye=capturePosition; target=eye+directions[face]; up=ups[face];
        }
        if (!capturing) {
            if(!frameArena) dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_Image);
            DkResHandle handles[6];
            for (unsigned f=0;f<6;++f) handles[f]=dkMakeTextureHandle(cubeFaces ? unsigned(textures.size())+1+f : environmentReady ? environmentBase+f : 0,3);
            dkCmdBufBindTextures(cmd,DkStage_Fragment,1,handles,6);
        }
        auto view=explicitCamera ? frameView : render::look_at(eye,target,up);
        auto projection=explicitCamera ? frameProjection : render::perspective(capturing ? 1.570796327f : 0.785398f,float(width)/height,0.1f,std::max(100.0f,radius*20));
        float fogData[40]{};
        for (unsigned c=0;c<4;++c) fogData[c]=view.at(2,c);
        for (unsigned c=0;c<3;++c) fogData[4+c]=std::clamp(environment.fog_rgb[c],0.0f,1.0f);
        fogData[7]=!capturing && courseFog && environment.has_env && environment.fog_a>0 && environment.fog_b<0.999f ? 1 : 0;
        fogData[8]=environment.fog_a; fogData[9]=std::clamp(environment.fog_b,0.0f,1.0f);
        if (frontend) {
            const auto packed=render::deko::scene_fog(*frontend,object,view,sphere!=nullptr);
            std::copy(packed.begin(),packed.end(),fogData);
            const auto handle=dkMakeTextureHandle(unsigned(textures.size()),3);
            dkCmdBufBindTextures(cmd,DkStage_Fragment,7,&handle,1);
        }
        if(post && frontend) {
            fogData[7]=0; fogData[35]=0;
            for(unsigned c=0;c<3;++c) fogData[20+c]=frontend->post_grade.curve[c];
            fogData[24]=frontend->post_grade.black; fogData[25]=frontend->post_grade.white;
            fogData[26]=frontend->post_grade.saturation;
        }
        const auto fogBuffer=frameArena ? arenaFog.gpu() : dkMemBlockGetGpuAddr(fogUniforms[slot]);
        dkCmdBufPushConstants(cmd,fogBuffer,256,0,sizeof(fogData),fogData);
        dkCmdBufBindUniformBuffer(cmd,DkStage_Vertex,2,fogBuffer,256);
        dkCmdBufBindUniformBuffer(cmd,DkStage_Fragment,2,fogBuffer,256);
        const auto camera=projection*view;
        const float maxDistance=frontend ? (object && object->draw_distance>=0 ? object->draw_distance : frontend->draw_distance) : 600;
        const ViewCuller culler(camera,eye,maxDistance), backgroundCuller(camera,eye,0);
        const auto setupEnd=Clock::now();
        for (size_t m=0;m<meshes.size();++m) {
            visibleMeshes[m]=meshes[m].sky || meshes[m].car || (meshes[m].backdrop ? backgroundCuller.visible(meshes[m].bounds,meshes[m].transform) : culler.visible(meshes[m].bounds,meshes[m].transform));
            if (frontend) {
                const int lod=object && object->lod!=-2 ? object->lod : frontend->lod;
                if(lod>=0 && meshes[m].lod!=lod) visibleMeshes[m]=false;
                if(frontend->frustum_cull && !(object && (object->background || object->helper)))
                    visibleMeshes[m]=visibleMeshes[m] && culler.visible(meshes[m].bounds,meshes[m].transform);
            }
            if (meshes[m].car && hasBrakeVariant && meshes[m].brakeVariant!=carFrames[meshes[m].carIndex].brakeNow) visibleMeshes[m]=false;
            if (!capturing && !meshes[m].car && !meshes[m].backdrop && !meshes[m].sky) { ++testedMeshes; culledMeshes+=!visibleMeshes[m]; }
        }
        const auto eyeDirection=render::normalize(render::Vec3{view.at(2,0),view.at(2,1),view.at(2,2)});
        const auto cullEnd=Clock::now();
        size_t draw = 0, triangles=0, uniformBytes=0, stateChanges=0;
        int lastDepth=-1, lastBlend=-1, lastCull=-1, lastWriteMask=-1;
        uint32_t lastTexture=UINT32_MAX;
        unsigned lastCar=UINT32_MAX;
        // Opaque scene first, then course blending, then each car's transparent,
        // reflection and depth-only packets in their original order.
        for (unsigned pass = 0; drawGeometry && pass < (frontend ? 1u : 3+carCount); ++pass) for (size_t meshIndex=0;meshIndex<meshes.size();++meshIndex) {
            if (!visibleMeshes[meshIndex]) continue;
            const auto& mesh=meshes[meshIndex];
            const bool sky=mesh.sky || (object && object->background);
            const bool car=mesh.car && !sky;
            if (!frontend && sky!=(pass==0)) continue;
            if (capturing && car) continue;
            if (car && lastCar!=mesh.carIndex) {
                dkCmdBufBindUniformBuffer(cmd,DkStage_Vertex,1,lightBuffer+mesh.carIndex*256,256);
                lastCar=mesh.carIndex;
            }
            const auto world=mesh.sky ? render::translate(eye)*mesh.transform : mesh.transform;
            const auto mvp=camera*world;
            for (unsigned i = 0; i < mesh.primitiveCount; ++i) {
                const auto& p = primitives[mesh.firstPrimitive+i];
                const bool maskDebug=courseMaskView && !car && !sky && !capturing;
                auto state=render::deko::primitive_state(p.flags,imageOverride || p.texture!=UINT32_MAX,object && object->sphere_map,frontend!=nullptr,sky,car,object && object->frame_alpha,object ? object->dest_alpha_blend : 0);
                if(state.skip) continue;
                const bool reflection=state.reflection, depthOnly=state.depth_only;
                if(object && object->alpha<1 && state.blend==render::deko::NativeBlend::Opaque && !depthOnly) {
                    state.blend=render::deko::NativeBlend::Alpha; state.transparent=true;
                }
                const bool transparent=state.transparent;
                if(frontend) {
                    if((phase==1 && transparent) || (phase==2 && !transparent)) continue;
                } else if (car) {
                    if (transparent ? pass!=3+mesh.carIndex : pass!=1) continue;
                } else if (pass>=3) continue;
                if (reflection) {
                    if (capturing || !reflections || (!environmentReady && !frontend)) continue;
                } else {
                    // Other additive/course-mask passes still need their own handling.
                    if (state.skip) continue;
                    if (!frontend && !sky && !car && bool(p.flags&1)!=(pass==2)) continue;
                }
                if(frontend && frontend->frustum_cull && object && object->profile_role==render::ProfileRole::Scene &&
                   !object->background && !object->helper && object->modelview_scale==1 && !hasUpdates && p.bounded) {
                    ++primitiveTests;
                    if(!culler.visible(p.bounds,world)) { ++primitiveCulled; continue; }
                }
                const bool blended=!maskDebug && !depthOnly && state.blend!=render::deko::NativeBlend::Opaque;
                if (!p.count) continue;
                const int writeMask=depthOnly ? 0 : state.alpha_only ? DkColorMask_A : state.keep_alpha ? DkColorMask_RGB : DkColorMask_RGBA;
                if(lastWriteMask!=writeMask) {
                    DkColorWriteState colorWrite; dkColorWriteStateDefaults(&colorWrite);
                    dkColorWriteStateSetMask(&colorWrite,0,writeMask);
                    dkCmdBufBindColorWriteState(cmd,&colorWrite); lastWriteMask=writeMask; ++stateChanges;
                }
                const int cull=render::deko::native_packet_cull(p.flags,courseCulling,car,sky,frontend,object);
                if(frontend && object && object->car_surface) {
                    if(cull) ++bodyBackfacePackets; else ++bodyTwoSidedPackets;
                }
                if (lastCull!=cull) {
                    DkRasterizerState raster; dkRasterizerStateDefaults(&raster);
                    raster.frontFace=DkFrontFace_CCW;
                    raster.cullMode=cull==2 ? DkFace_Front : cull==1 ? DkFace_Back : DkFace_None;
                    dkCmdBufBindRasterizerState(cmd,&raster); lastCull=cull; ++stateChanges;
                }
                // Course packets may blend and still write Z. The material's
                // explicit no-write flag decides, as in production View3D.
                // Preserve the separately tested car policy for this comparison.
                const bool depthWrite=state.depth_write;
                const int depthMode=sky ? 2 : int(depthWrite);
                if (lastDepth!=depthMode) {
                    DkDepthStencilState depth; dkDepthStencilStateDefaults(&depth);
                    depth.depthTestEnable=!sky;
                    depth.depthCompareOp=DkCompareOp_Gequal; depth.depthWriteEnable=depthWrite;
                    dkCmdBufBindDepthStencilState(cmd,&depth); lastDepth=depthMode; ++stateChanges;
                }
                const int blendMode=(blended ? int(state.blend) : 0)+(object && object->frame_alpha ? 16 : 0)+(state.keep_alpha ? 32 : 0);
                if (lastBlend!=blendMode) {
                    DkColorState color; dkColorStateDefaults(&color); color.blendEnableMask=blended ? 1 : 0;
                    dkCmdBufBindColorState(cmd,&color);
                    DkBlendState blend; dkBlendStateDefaults(&blend); blend.dstAlphaBlendFactor=DkBlendFactor_InvSrcAlpha;
                    if (state.blend==render::deko::NativeBlend::Additive) {
                        blend.srcColorBlendFactor=DkBlendFactor_SrcAlpha;
                        blend.dstColorBlendFactor=DkBlendFactor_One;
                        blend.srcAlphaBlendFactor=DkBlendFactor_Zero;
                        blend.dstAlphaBlendFactor=DkBlendFactor_One;
                    }
                    if (state.blend==render::deko::NativeBlend::MultiplyDest) {
                        blend.srcColorBlendFactor=DkBlendFactor_Zero; blend.dstColorBlendFactor=DkBlendFactor_SrcAlpha;
                        blend.srcAlphaBlendFactor=DkBlendFactor_Zero; blend.dstAlphaBlendFactor=DkBlendFactor_One;
                    }
                    if(state.blend==render::deko::NativeBlend::DestAlphaAdd || state.blend==render::deko::NativeBlend::DestAlphaLerp) {
                        blend.srcColorBlendFactor=DkBlendFactor_DstAlpha;
                        blend.dstColorBlendFactor=state.blend==render::deko::NativeBlend::DestAlphaAdd ? DkBlendFactor_One : DkBlendFactor_InvDstAlpha;
                        blend.srcAlphaBlendFactor=DkBlendFactor_Zero; blend.dstAlphaBlendFactor=DkBlendFactor_One;
                    }
                    if(object && object->frame_alpha && !state.keep_alpha) {
                        blend.srcAlphaBlendFactor=DkBlendFactor_One; blend.dstAlphaBlendFactor=DkBlendFactor_Zero;
                    }
                    dkCmdBufBindBlendState(cmd,0,&blend); lastBlend=blendMode; ++stateChanges;
                }
                const unsigned texture=imageOverride ? unsigned(textures.size())+7 : p.texture==UINT32_MAX ? 0 : p.texture+1;
                const auto handle=dkMakeTextureHandle(texture,imageOverride ? 3 : textures[texture].flags&3);
                if (lastTexture!=handle) { dkCmdBufBindTextures(cmd,DkStage_Fragment,0,&handle,1); lastTexture=handle; ++stateChanges; }
                float values[64]{};
                std::memcpy(values,mvp.m.data(),64); std::memcpy(values+16,world.m.data(),64);
                values[32]=imageOverride || p.texture!=UINT32_MAX ? 1 : 0;
                const float defaultAlpha=frontend ? (blended ? frontend->blend_alpha_ref : frontend->alpha_ref) : blended && !mesh.car ? 0.02f : 0.5f;
                values[33]=(p.flags&scene::kAlphaTest) ? (p.alpha>=0 ? p.alpha : defaultAlpha) : -1;
                values[34]=mesh.sky || (p.flags&(1u<<5)) ? 1 : 0;
                if (maskDebug) {
                    values[34]=courseMaskView==1 ? 5 : 4;
                    if (courseMaskView==1) values[33]=p.texture==UINT32_MAX ? 0 : float(p.texture+1);
                }
                values[35]=((p.flags&(1u<<13)) ? 2 : 1)*(object ? object->tint : 1);
                const bool useMaterial=!sky && !reflection && materialLighting && p.material[3]>0.5f && !(p.flags&(1u<<5));
                if (sky) values[39]=-1; // vertex shader: project background to far depth
                if (useMaterial) {
                    std::memcpy(values+36,p.material,sizeof(p.material));
                    if (mesh.car) for (unsigned c=0;c<3;++c) values[48+c]+=p.brakeEmission[c]*(object ? object->lamp : carFrames[mesh.carIndex].brake);
                    if (p.material[3]<2.5f && ((frontend && frontend->gt3_lighting) || (mesh.car && pointLighting && courseLights.active() && environment.has_env))) values[39]=2;
                    const bool spec=p.material[8]>0 || p.material[9]>0 || p.material[10]>0;
                    values[47]=!spec ? 0 : p.material[11]<6 ? 4 : p.material[11]<12 ? 8 : 16;
                    values[51]=!imageOverride && p.texture==UINT32_MAX ? 128.0f/255.0f : 1.0f;
                    values[52]=eyeDirection.x; values[53]=eyeDirection.y; values[54]=eyeDirection.z;
                    if(frontend && p.material[3]>2.5f) {
                        const float scale=object ? object->modelview_scale : 1;
                        for(unsigned c=0;c<3;++c) values[52+c]=scale*scale*(&eye.x)[c]+2*scale*view.at(1,c);
                        values[47]=p.material[11];
                    }
                }
                // Generic/prelit draws read only through matAmbient.w (offset 156).
                // Zero validity disables all shader reads of the omitted tail.
                if (reflection) {
                    values[34]=2; values[39]=(p.flags&scene::kDestAlphaTest) ? 3 : 0;
                    values[52]=eye.x; values[53]=eye.y; values[54]=eye.z; values[55]=(cubeFaces || environmentReady) ? 1 : 0;
                    std::memcpy(values+56,p.env,sizeof(p.env)); values[63]=frontend ? frontend->reflection_strength*(object ? object->reflection_scale*object->tint : 1) : 0.4f;
                }
                if(state.sphere) {
                    values[34]=6; values[52]=eye.x; values[53]=eye.y; values[54]=eye.z;
                    values[55]=(p.flags&scene::kAdditive) ? 1 : 0;
                    std::memcpy(values+56,p.env,sizeof(p.env));
                }
                if(frontend && object && object->sphere_map && (p.envKind==2 || p.envKind==3)) {
                    const float scale=object->modelview_scale;
                    values[34]=7; values[39]=p.envKind; values[47]=scale;
                    for(unsigned c=0;c<3;++c) values[52+c]=scale*scale*(&eye.x)[c];
                    std::memcpy(values+56,p.env,sizeof(p.env)); values[59]=p.envAlpha[0]; values[63]=p.envAlpha[1];
                    if(p.envKind==3) {
                        const auto d=p.envPoint ? render::Vec3{p.envPointPos[0]-eye.x,p.envPointPos[1]-eye.y,p.envPointPos[2]-eye.z} : render::Vec3{-view.at(2,0),-view.at(2,1),-view.at(2,2)};
                        const float g=d.x+d.y+d.z>0 ? 0 : 4, phi=0.78539816f*(g+1);
                        values[36]=std::cos(phi); values[37]=std::sin(phi);
                        values[40]=0.875f-(g+1)/8; values[41]=frontend->panorama[0]/6; values[42]=0.5f+frontend->panorama[1];
                    }
                }
                if(post) values[34]=8;
                const bool common=render::deko::common_fragment(values[34],commonFragmentShader!=nullptr,frontend!=nullptr,capturing,post,object,
                    frontend ? frontend->profile_pass : render::ProfilePass::Other);
                const DkShader* selected=common ? commonFragmentShader : capturing ? captureShader : fragmentShader;
                if(selected!=boundFragment) {
                    dkCmdBufBindShaders(cmd,DkStageFlag_Fragment,&selected,1); boundFragment=selected; ++shaderSwitches;
                }
                if(common) ++commonPackets; else ++generalPackets;
                const unsigned pushBytes=(reflection || state.sphere || values[34]==7) ? sizeof(values) : useMaterial ? 224 : 160;
                uniformBytes+=pushBytes;
                if (draw>=frameBudget.draws) throw "Car field exceeds per-frame uniform capacity";
                auto uniform=(frameArena ? arenaUniforms.gpu() : dkMemBlockGetGpuAddr(uniforms[slot]))+draw++*256;
                dkCmdBufPushConstants(cmd,uniform,256,0,pushBytes,values);
                dkCmdBufBindUniformBuffer(cmd,DkStage_Vertex,0,uniform,256);
                dkCmdBufBindUniformBuffer(cmd,DkStage_Fragment,0,uniform,256);
                triangles+=p.count/3;
                dkCmdBufDrawIndexed(cmd,DkPrimitive_Triangles,p.count,1,p.first,int32_t(mesh.vertexBase),0);
            }
        }
        if(object && object->depth_reset) dkCmdBufClearDepthStencil(cmd,true,0,0,0);
        if (viewColor && !frameArena) dkCmdBufBarrier(cmd,DkBarrier_Fragments,DkInvalidateFlags_Image);
        const auto packetsEnd=Clock::now();
        if(frameArena) frameArena->prepare_cpu_reads();
        dkQueueSubmitCommands(queue,dkCmdBufFinishList(cmd));
        dkQueueSignalFence(queue,&fences[slot],capturing); inFlight[slot]=true;
        if(frameArena) arenaGenerations[slot]=frameArena->generation();
        if (capturing) return;
        const auto submissionEnd=Clock::now();
        const double record=milliseconds(recordBegin,submissionEnd);
        lastSubmission={wait,record,draw,triangles,milliseconds(recordBegin,setupEnd),milliseconds(setupEnd,cullEnd),
            milliseconds(cullEnd,packetsEnd),milliseconds(packetsEnd,submissionEnd)};
        recordTotal+=record; recordMax=std::max(recordMax,record);
        ++measuredFrames; measuredDraws=draw; measuredTriangles=triangles;
        auto& mode=modes[materialLighting ? 1 : 0]; ++mode.frames; mode.total+=record; mode.maximum=std::max(mode.maximum,record);
        lastUniformBytes=uniformBytes; lastStateChanges=stateChanges;
    }
};
Model::Model(DkDevice d,DkQueue q,const DkShader* v,const DkShader* f,const DkShader* c,const DkShader* common,DkGpuAddr sharedSamplerDescriptors)
    : impl_(std::make_unique<Impl>(d,q,v,f,c,common,sharedSamplerDescriptors)) {}
Model::~Model()=default;
SubmissionTiming Model::submission_timing() const { return impl_->lastSubmission; }
bool Model::segment_blocked(const render::Vec3& a,const render::Vec3& b) const { return impl_->sceneOccluder.segment_blocked(a,b); }
bool Model::set_environment_origin(const render::Vec3& p) {
    if (!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)) return false;
    impl_->capturePosition=p; return true;
}
void Model::capture_environment() { impl_->capture_environment(); }
void Model::update_environment(unsigned slot,const render::Vec3& origin) { impl_->update_environment(slot,origin); }
bool Model::toggle_reflections() { return impl_->reflections=!impl_->reflections; }
bool Model::toggle_course_culling() { return impl_->courseCulling=!impl_->courseCulling; }
bool Model::toggle_course_fog() { return impl_->courseFog=!impl_->courseFog; }
unsigned Model::cycle_course_mask_view() { impl_->courseMaskView=(impl_->courseMaskView+1)%3; return impl_->courseMaskView; }
void Model::inspect_course_textures() { impl_->inspect_course_textures(); }
bool Model::toggle_materials() { return impl_->materialLighting=!impl_->materialLighting; }
void Model::set_brake_factor(float factor,bool now,unsigned car) {
    if (car>=impl_->carCount) throw "Invalid car brake instance";
    auto& frame=impl_->carFrames[car]; frame.brakeNow=now; frame.brake=std::clamp(factor,0.0f,1.0f);
    if (!car) { impl_->brakeNow=now; impl_->brakeFactor=frame.brake; }
    impl_->maximumBrakeFactor=std::max(impl_->maximumBrakeFactor,frame.brake);
}
void Model::update_car_lights(const sim::VehicleState& s,int hint,unsigned car) { impl_->update_car_lights(s,hint,car); }
bool Model::toggle_point_lighting() { return impl_->pointLighting=!impl_->pointLighting; }
void Model::load_course_animations(std::span<const uint8_t> c) { impl_->load_course_animations(c); }
void Model::prepare_dynamic_resources() { impl_->prepare_dynamic_resources(); }
void Model::configure_cars(unsigned count) { impl_->configure_cars(count); }
void Model::animate_course(float s) { impl_->animate_course(s); }
void Model::reset_course_animations() { impl_->reset_course_animations(); }
sim::VehicleState Model::initial_vehicle_state() const { return impl_->initialVehicle; }
bool Model::apply_vehicle_state(const sim::VehicleState& s,const sim::VehicleSetup& setup,unsigned car) { return impl_->apply_vehicle_state(s,setup,car); }
bool Model::toggle_wheel_motion() { return impl_->wheelMotion=!impl_->wheelMotion; }
void Model::clear_external_camera() { impl_->externalCamera=false; }
bool Model::set_mesh_transform(size_t i,const render::Mat4& t) { return impl_->set_mesh_transform(i,t); }
bool Model::set_camera(const render::Mat4& v,const render::Mat4& p,const render::Vec3& e) { return impl_->set_camera(v,p,e); }
void Model::demo_object_transform(float p) { impl_->demo_object_transform(p); }
bool Model::update_texture(size_t i,std::span<const uint32_t> p) { return impl_->update_texture(i,p); }
void Model::demo_texture_color(float p) { impl_->demo_texture_color(p); }
bool Model::update_vertices(size_t m,std::span<const scene::Vertex> v) { return impl_->update_vertices(m,v); }
void Model::demo_vertex_offset(float h) { impl_->demo_vertex_offset(h); }
void Model::load_scene(const scene::Model& m,const SceneOptions& o) { impl_->load_scene(m,o); }
void Model::load(const char* p,std::span<const uint8_t> course) { impl_->load(p,course); }
void Model::move_camera(float forward,float side,float turn,float tilt,float lift,float seconds) {
    auto& m=*impl_;
    if (!m.freeCamera) return;
    const float dt=std::clamp(seconds,0.0f,0.05f);
    m.yaw=std::remainder(m.yaw-turn*dt*1.8f,6.2831853f);
    m.pitch=std::clamp(m.pitch+tilt*dt*1.4f,-1.4f,1.4f);
    m.eyePosition=m.eyePosition+render::Vec3{std::sin(m.yaw)*forward-std::cos(m.yaw)*side,lift,std::cos(m.yaw)*forward+std::sin(m.yaw)*side}*(dt*20.0f);
}
void Model::submit(unsigned s,float a,unsigned w,unsigned h) { impl_->submit(s,a,w,h); }
void Model::wait_submissions() { impl_->wait_submissions(); }
DkFence Model::submission_fence(unsigned slot) const {
    if(slot>=impl_->frameBudget.slots || !impl_->inFlight[slot]) throw "Missing native reader submission fence";
    return impl_->fences[slot];
}
void Model::submit_to(unsigned slot,unsigned w,unsigned h,const DkImage* color,const DkImage* depth,const std::array<float,4>& clear,const render::DrawOptions* options,const DkImageDescriptor* sphere,const render::DrawItem* item,bool clearTarget,bool drawGeometry,const DkImageDescriptor* cubeFaces,const DkImageDescriptor* imageOverride,unsigned phase,bool post) {
    if (!color || !depth || !w || !h) throw "Invalid native view target";
    impl_->submit(slot,0,w,h,false,0,color,depth,clear.data(),options,sphere,item,clearTarget,drawGeometry,cubeFaces,imageOverride,phase,post);
}
}
