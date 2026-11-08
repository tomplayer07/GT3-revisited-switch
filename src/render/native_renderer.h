#pragma once
#include "render/math3d.h"
#include "render/native_profile.h"
#include "scene/model.h"
#include <SDL3/SDL.h>
#include <memory>
#include <array>
#include <span>

namespace gt3r::render {
struct DrawItem;
struct DrawOptions;
struct ModelTextureUpdate {
    size_t index=0;
    const std::vector<u32>* rgba=nullptr;
};
// Native resources stay typed: they are never cast to SDL_GPU or GL handles.
// Implementations copy update input before return and retire GPU-visible
// resources safely; failed updates must leave the previous model usable.
class NativeModel {
public:
    virtual ~NativeModel() = default;
    virtual bool valid() const = 0;
    virtual bool update_vertices(std::span<const scene::Vertex>) = 0;
    virtual bool update_geometry(const std::vector<scene::Mesh>&) = 0;
    virtual bool update_textures(std::span<const ModelTextureUpdate>) = 0;
};
class NativeTexture;
class NativeCubeTexture;
class NativeView {
public:
    virtual ~NativeView() = default;
    // Return a borrowed SDL wrapper in top-left orientation. Keep its contents
    // alive and unchanged until queued 2D draws and GPU sampling have finished.
    virtual SDL_Texture* draw(int width,int height,const Mat4& view,const Mat4& projection,
        const Vec3& eye,const std::vector<DrawItem>& items,const DrawOptions& options) = 0;
    virtual std::shared_ptr<NativeTexture> render_texture(int,int,const Mat4&,const Mat4&,
        const Vec3&,const std::vector<DrawItem>&,const DrawOptions&) {
        SDL_SetError("Native view textures are unsupported"); return {};
    }
    virtual std::shared_ptr<NativeTexture> process_texture(int,int,const std::shared_ptr<NativeTexture>&,const DrawOptions&) {
        SDL_SetError("Native image processing is unsupported"); return {};
    }

};
class NativeTexture {
public:
    virtual ~NativeTexture() = default;
    virtual SDL_Texture* renderer_texture() const { return nullptr; }
};
class NativeCubeTexture {
public:
    virtual ~NativeCubeTexture() = default;
};
class NativeRenderer {
public:
    virtual ~NativeRenderer() = default;
    // Owns the SDL renderer. The context's models/views must be destroyed first.
    virtual SDL_Renderer* renderer2d() const = 0;
    virtual const char* name() const = 0;
    // Native model samplers may expose a renderer-specific anisotropy level.
    virtual bool anisotropy_supported() const { return false; }
    virtual int anisotropy() const { return 1; }
    virtual bool set_anisotropy(int) { return false; }
    virtual void profile_race_frame(const RaceProfileInfo&) {}
    virtual void profile_race_cpu(const RaceProfileCpu&) {}
    virtual std::shared_ptr<NativeTexture> upload_texture(int, int, std::span<const u32>) {
        SDL_SetError("Native texture uploads are unsupported"); return {};
    }
    // Opt in only when resource creation/uploads are isolated from the render queue.
    virtual bool background_model_upload() const { return false; }
    virtual std::unique_ptr<NativeModel> upload_model(const scene::Model&) = 0;
    // Shared texture source must outlive the dynamic model, as for GpuModel.
    virtual std::unique_ptr<NativeModel> share_model_textures(const NativeModel&) = 0;
    virtual std::unique_ptr<NativeView> create_view() = 0;
    virtual std::shared_ptr<NativeCubeTexture> create_cube(const std::array<std::shared_ptr<NativeTexture>,6>&) {
        SDL_SetError("Native cube textures are unsupported"); return {};
    }
};
} // namespace gt3r::render
