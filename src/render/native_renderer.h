#pragma once
#include "render/math3d.h"
#include "scene/model.h"
#include <SDL3/SDL.h>
#include <memory>
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
class NativeView {
public:
    virtual ~NativeView() = default;
    // Return a borrowed SDL wrapper in top-left orientation. Keep its contents
    // alive and unchanged until queued 2D draws and GPU sampling have finished.
    virtual SDL_Texture* draw(int width,int height,const Mat4& view,const Mat4& projection,
        const Vec3& eye,const std::vector<DrawItem>& items,const DrawOptions& options) = 0;
};
class NativeRenderer {
public:
    virtual ~NativeRenderer() = default;
    // Owns the SDL renderer. The context's models/views must be destroyed first.
    virtual SDL_Renderer* renderer2d() const = 0;
    virtual const char* name() const = 0;
    virtual std::unique_ptr<NativeModel> upload_model(const scene::Model&) = 0;
    // Shared texture source must outlive the dynamic model, as for GpuModel.
    virtual std::unique_ptr<NativeModel> share_model_textures(const NativeModel&) = 0;
    virtual std::unique_ptr<NativeView> create_view() = 0;
};
} // namespace gt3r::render
