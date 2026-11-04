#include "render/gpu_model.h"

#include "base/log.h"
#include "base/render_profile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace gt3r::render {

ModelStats model_stats(const scene::Model& model, int lod) {
    ModelStats s;
    s.textures = model.textures.size();
    for (const scene::Mesh& m : model.meshes) {
        s.max_lod = std::max(s.max_lod, m.lod);
        if (lod >= 0 && m.lod != lod) continue;
        ++s.meshes;
        s.vertices += m.vertices.size();
        s.primitives += m.primitives.size();
        for (const scene::Primitive& p : m.primitives) s.triangles += p.index_count / 3;
    }
    return s;
}

namespace {

// Makes `buffer` hold at least `size` bytes, creating it or replacing it with one twice the size
// (contents lost). True when it is now a new buffer.
bool ensure_buffer(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage, SDL_GPUBuffer*& buffer, u32& capacity,
                   u32 size, bool& ok) {
    ok = true;
    if (buffer && capacity >= size) return false;
#if defined(__SWITCH__)
    GLuint buf = (GLuint)(uintptr_t)buffer;
    if (buf) glDeleteBuffers(1, &buf);
    glGenBuffers(1, &buf);
    GLenum target = (usage & SDL_GPU_BUFFERUSAGE_INDEX) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    glBindBuffer(target, buf);
    glBufferData(target, std::max(size, capacity * 2), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(target, 0);
    buffer = (SDL_GPUBuffer*)(uintptr_t)buf;
    capacity = std::max(size, capacity * 2);
    return true;
#else
    if (buffer) SDL_ReleaseGPUBuffer(device, buffer);
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = usage;
    bi.size = std::max(size, capacity * 2);
    buffer = SDL_CreateGPUBuffer(device, &bi);
    capacity = buffer ? bi.size : 0;
    ok = buffer != nullptr;
    return true;
#endif
}

} // namespace

GpuModel::GpuModel(GpuContext& ctx, const scene::Model& model, bool keep_geometry)
    : ctx_(&ctx), device_(ctx.device()), keep_(keep_geometry) {
    std::vector<scene::Vertex> vertices;
    std::vector<u32> indices;
    assemble_model_geometry(model.meshes, model.textures.size(), true, vertices, indices, meshes_);
    vertex_count_ = vertices.size();
    Uploader up(device_);
    if (!vertices.empty() && !indices.empty()) {
        vertices_ = up.buffer(SDL_GPU_BUFFERUSAGE_VERTEX, vertices.data(), u32(vertices.size() * sizeof(scene::Vertex)));
        indices_ = up.buffer(SDL_GPU_BUFFERUSAGE_INDEX, indices.data(), u32(indices.size() * sizeof(u32)));
    }
    if (keep_) geometry_ = {std::move(vertices), std::move(indices)};
    for (const scene::Texture& t : model.textures) {
        TextureInfo ti;
        ti.clamp_u = t.clamp_u;
        ti.clamp_v = t.clamp_v;
        ti.no_mips = t.no_mips;
        if (t.width && t.height && t.rgba.size() >= size_t(t.width) * t.height) {
            ti.texture = ctx.acquire_texture(up, t.width, t.height, t.rgba.data());
            ti.width = t.width, ti.height = t.height;
        }
        if (!ti.texture) LOG_WARN("Texture '%s' (%ux%u) not uploaded", t.name.c_str(), t.width, t.height);
        textures_.push_back(ti);
    }
    if (!up.submit()) LOG_WARN("Model upload failed: %s", SDL_GetError());
}

// Each upload goes through staging_ (grown when too small), at 16-byte aligned offsets, into its
// buffer from offset 0 with cycling: frames still drawing keep the old contents.
bool GpuModel::stage(const Upload* uploads, int count) {
    render_profile::Scope measure(render_profile::frame.geometry);
    if (render_profile::enabled) for (int i = 0; i < count; ++i) {
        ++render_profile::frame.geometry_updates;
        render_profile::frame.geometry_bytes += uploads[i].size;
    }
#if defined(__SWITCH__)
    for (int i = 0; i < count; ++i) {
        if (!uploads[i].size) continue;
        GLuint buf = (GLuint)(uintptr_t)uploads[i].buffer;
        glBindBuffer(GL_ARRAY_BUFFER, buf);
        glBufferSubData(GL_ARRAY_BUFFER, 0, uploads[i].size, uploads[i].data);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
#else
    auto aligned = [](u32 v) { return (v + 15u) & ~15u; };
    u32 total = 0;
    for (int i = 0; i < count; ++i) total += aligned(uploads[i].size);
    if (total == 0) return true;
    if (!staging_ || staging_capacity_ < total) {
        if (staging_) SDL_ReleaseGPUTransferBuffer(device_, staging_);
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = std::max(total, staging_capacity_ * 2);
        staging_ = SDL_CreateGPUTransferBuffer(device_, &ti);
        staging_capacity_ = staging_ ? ti.size : 0;
        if (!staging_) return false;
    }
    auto* p = static_cast<u8*>(SDL_MapGPUTransferBuffer(device_, staging_, true));
    if (!p) return false;
    for (int i = 0, at = 0; i < count; at += int(aligned(uploads[i].size)), ++i)
        std::memcpy(p + at, uploads[i].data, uploads[i].size);
    SDL_UnmapGPUTransferBuffer(device_, staging_);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) return false;
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    for (int i = 0, at = 0; i < count; at += int(aligned(uploads[i].size)), ++i) {
        if (!uploads[i].size) continue;
        SDL_GPUTransferBufferLocation src{staging_, u32(at)};
        SDL_GPUBufferRegion dst{uploads[i].buffer, 0, uploads[i].size};
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    }
    SDL_EndGPUCopyPass(cp);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) return false;
    note_gpu_submits(device_, 1); // staging_ is this model's own: no new transfer buffer
    return true;
#endif
}

bool GpuModel::update_vertices(const std::vector<scene::Vertex>& all) {
    if (!vertices_ || all.size() != vertex_count_) return false;
#if defined(__SWITCH__)
    if (uploaded_vertices_.matches(all)) {
        if (render_profile::enabled) ++render_profile::frame.geometry_skipped;
        return true;
    }
#endif
    const Upload u{vertices_, all.data(), u32(all.size() * sizeof(scene::Vertex))};
    if (keep_) geometry_.vertices = all;
    const bool ok = stage(&u, 1);
#if defined(__SWITCH__)
    if (ok) uploaded_vertices_.remember(all);
    else uploaded_vertices_.invalidate();
#endif
    return ok;
}

bool GpuModel::update_textures(const std::vector<TextureUpdate>& updates) {
#if defined(__SWITCH__)
    // Diagnostic isolation: keep the initial images, while CPU palette animation,
    // moving geometry, lighting and simulation continue. Not a quality preset.
    if (ctx_->gl_freeze_textures()) return true;
#endif
    bool ok = true;
    Uploader up(device_);
#if defined(__SWITCH__)
    texture_batch_.clear();
#endif
    for (const TextureUpdate& u : updates) {
        TextureInfo* t = u.index < textures_.size() ? &textures_[u.index] : nullptr;
        if (!t || !t->texture || !u.rgba || u.rgba->size() != size_t(t->width) * t->height) {
            ok = false;
            continue;
        }
        if (ctx_->take_texture(t->texture)) {
#if defined(__SWITCH__)
            SDL_GPUTexture* target = t->texture;
            if (ctx_->gl_texture_rotate()) {
                // take_texture above first detaches shared storage. Other models
                // keep their original image; this model owns every ring slot.
                if (!t->rotation.initialized()) t->rotation.adopt(t->texture);
                target = t->rotation.target();
                if (!target) {
                    target = up.texture_rgba(t->width, t->height, u.rgba->data(), !t->no_mips);
                    if (!target) { ok = false; continue; }
                    t->rotation.advance(target);
                    t->texture = target;
                    continue; // the new image already contains this update
                }
            }
            if (ctx_->gl_texture_stream()) {
                render_profile::Scope stage_measure(render_profile::frame.texture_stage);
                ok = texture_batch_.add(u32(uintptr_t(target)), t->width, t->height, *u.rgba, !t->no_mips) && ok;
            } else
#endif
            {
#if defined(__SWITCH__)
                up.update_rgba(target, t->width, t->height, u.rgba->data(), !t->no_mips, true);
#else
                up.update_rgba(t->texture, t->width, t->height, u.rgba->data(), !t->no_mips, true);
#endif
            }
#if defined(__SWITCH__)
            if (ctx_->gl_texture_rotate()) {
                t->rotation.advance(target);
                t->texture = target;
            }
#endif
        } else if (SDL_GPUTexture* own = up.texture_rgba(t->width, t->height, u.rgba->data(), !t->no_mips)) {
            ctx_->release_texture(t->texture); // other models keep drawing the shared texels
            t->texture = own;
        } else {
            ok = false;
        }
    }
#if defined(__SWITCH__)
    ctx_->upload_gl_textures(texture_batch_);
#endif
    return up.submit() && ok;
}

GpuModel::GpuModel(GpuContext& ctx, const GpuModel& textures)
    : ctx_(&ctx), device_(ctx.device()), texture_source_(&textures) {}

bool GpuModel::update_geometry(const std::vector<scene::Mesh>& meshes) {
    std::vector<scene::Vertex> vertices;
    std::vector<u32> indices;
    meshes_.clear();
    {
        render_profile::Scope measure(render_profile::frame.assemble);
        assemble_model_geometry(meshes, textures().size(), false, vertices, indices, meshes_);
    }
    vertex_count_ = vertices.size();
    const u32 vsize = u32(vertices.size() * sizeof(scene::Vertex)), isize = u32(indices.size() * sizeof(u32));
    if (vsize == 0 || isize == 0) return true; // nothing drawn (meshes_ is empty too)
    bool vok = true, iok = true;
    const bool new_vertices = ensure_buffer(device_, SDL_GPU_BUFFERUSAGE_VERTEX, vertices_, vertex_capacity_, vsize, vok);
#if defined(__SWITCH__)
    if (new_vertices) uploaded_vertices_.invalidate();
#else
    (void)new_vertices;
#endif
    // The index buffer only when its contents change (the animated nodes' usually don't).
    if (ensure_buffer(device_, SDL_GPU_BUFFERUSAGE_INDEX, indices_, index_capacity_, isize, iok))
        uploaded_indices_.clear();
    const bool same_indices = indices == uploaded_indices_;
#if defined(__SWITCH__)
    const bool same_vertices = uploaded_vertices_.matches(vertices);
#else
    const bool same_vertices = false;
#endif
    Upload uploads[2];
    int count = 0;
    if (!same_vertices) uploads[count++] = {vertices_, vertices.data(), vsize};
    if (!same_indices) uploads[count++] = {indices_, indices.data(), isize};
    if (render_profile::enabled && same_vertices) ++render_profile::frame.geometry_skipped;
    bool ok = vok && iok && (!count || stage(uploads, count));
    if (ok && keep_) geometry_ = {vertices, indices};
    if (ok && !same_indices) uploaded_indices_ = std::move(indices);
#if defined(__SWITCH__)
    if (ok && !same_vertices) uploaded_vertices_.remember(vertices);
    if (!ok) uploaded_vertices_.invalidate();
#endif
    if (!ok) meshes_.clear(), uploaded_indices_.clear(), geometry_ = {};
    return ok;
}

GpuModel::~GpuModel() {
#if defined(__SWITCH__)
    GLuint v = (GLuint)(uintptr_t)vertices_;
    if (v) glDeleteBuffers(1, &v);
    GLuint i = (GLuint)(uintptr_t)indices_;
    if (i) glDeleteBuffers(1, &i);
#else
    if (staging_) SDL_ReleaseGPUTransferBuffer(device_, staging_);
    if (vertices_) SDL_ReleaseGPUBuffer(device_, vertices_);
    if (indices_) SDL_ReleaseGPUBuffer(device_, indices_);
#endif
    for (TextureInfo& t : textures_) {
#if defined(__SWITCH__)
        if (t.rotation.initialized()) {
            for (SDL_GPUTexture* image : t.rotation.handles()) if (image) ctx_->release_texture(image);
            continue;
        }
#endif
        ctx_->release_texture(t.texture);
    }
}

} // namespace gt3r::render
