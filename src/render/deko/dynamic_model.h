#pragma once
#include "render/native_renderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace gt3r::render::deko {
inline bool valid_dynamic_vertices(std::span<const scene::Vertex> vertices) {
    for(const auto& v:vertices) {
        for(float x:v.pos) if(!std::isfinite(x)) return false;
        for(float x:v.normal) if(!std::isfinite(x)) return false;
        for(float x:v.uv) if(!std::isfinite(x)) return false;
    }
    return true;
}
inline bool valid_dynamic_geometry(const std::vector<scene::Mesh>& meshes,size_t texture_count) {
    if(meshes.size()>1024) return false;
    uint64_t vertices=0,indices=0,primitives=0;
    for(const auto& mesh:meshes) {
        vertices+=mesh.vertices.size(); indices+=mesh.indices.size(); primitives+=mesh.primitives.size();
        if(vertices>1000000 || indices>3000000 || primitives>16384 || !valid_dynamic_vertices(mesh.vertices)) return false;
        for(float x:mesh.transform) if(!std::isfinite(x)) return false;
        for(auto index:mesh.indices) if(index>=mesh.vertices.size()) return false;
        for(const auto& p:mesh.primitives) {
            if(uint64_t(p.first_index)+p.index_count>mesh.indices.size() || p.index_count%3 ||
               p.texture< -1 || (p.texture>=0 && size_t(p.texture)>=texture_count) || !std::isfinite(p.alpha_ref)) return false;
            for(const auto* row:{p.material.ambient,p.material.diffuse,p.material.specular,p.material.emission,p.material.lamp_emission,p.env_c0,p.env_c1,p.env_point_pos})
                for(unsigned c=0;c<3;++c) if(!std::isfinite(row[c])) return false;
            if(!std::isfinite(p.material.scale) || p.material.scale<0 || p.material.scale>16 ||
               !std::isfinite(p.material.shininess) || p.material.shininess<0 || p.material.shininess>65536) return false;
            for(float x:p.env_alpha) if(!std::isfinite(x)) return false;
        }
    }
    return true;
}
// CPU state shared by a model family. GPU originals remain immutable/shared;
// consumers own their fenced replacements for the textures that actually animate.
class TextureUpdates {
public:
    struct Revision { std::shared_ptr<const std::vector<u32>> pixels; uint64_t version=0; };
private:
    std::vector<scene::Texture> metadata_;
    std::vector<Revision> revisions_;
    uint64_t serial_=0,allocations_=0,reuses_=0;
public:
    explicit TextureUpdates(const std::vector<scene::Texture>& source) {
        for(const auto& texture:source) {
            if(!texture.width || !texture.height || texture.width>4096 || texture.height>4096)
                throw std::invalid_argument("Invalid dynamic texture dimensions");
            scene::Texture meta;
            meta.name=texture.name; meta.width=texture.width; meta.height=texture.height;
            meta.clamp_u=texture.clamp_u; meta.clamp_v=texture.clamp_v; meta.no_mips=texture.no_mips;
            metadata_.push_back(std::move(meta));
        }
        revisions_.resize(metadata_.size());
    }
    const auto& metadata() const { return metadata_; }
    const auto& revisions() const { return revisions_; }
    uint64_t serial() const { return serial_; }
    uint64_t allocations() const { return allocations_; }
    uint64_t reuses() const { return reuses_; }
    bool apply(std::span<const ModelTextureUpdate> updates) {
        for(const auto& update:updates) {
            if(update.index>=metadata_.size() || !update.rgba) return SDL_SetError("Invalid native texture update index/data");
            const auto& meta=metadata_[update.index];
            if(update.rgba->size()!=size_t(meta.width)*meta.height) return SDL_SetError("Native texture update dimensions changed");
        }
        if(updates.empty()) return true;
        if(serial_==std::numeric_limits<uint64_t>::max()) return SDL_SetError("Native texture update revision exhausted");
        std::vector<std::pair<size_t,std::shared_ptr<const std::vector<u32>>>> prepared;
        prepared.reserve(updates.size());
        uint64_t allocations=0,reuses=0;
        // Plan every allocation before changing a published revision. Only an
        // exclusively owned revision can be recycled: retained snapshots remain
        // immutable. Raw borrowed input vectors must also remain untouched.
        for(const auto& update:updates) {
            const auto& meta=metadata_[update.index];
            const auto& old=revisions_[update.index].pixels;
            const bool borrowed=old && std::any_of(updates.begin(),updates.end(),[&](const auto& input) { return input.rgba==old.get(); });
            std::shared_ptr<std::vector<u32>> pixels;
            if(old && old.use_count()==1 && !borrowed) {
                pixels=std::const_pointer_cast<std::vector<u32>>(old); ++reuses;
            } else {
                pixels=std::make_shared<std::vector<u32>>(size_t(meta.width)*std::max(16u,meta.height)); ++allocations;
            }
            prepared.emplace_back(update.index,std::move(pixels));
        }
        for(size_t i=0;i<updates.size();++i) {
            const auto& update=updates[i]; const auto& meta=metadata_[update.index];
            // Every buffer originated as mutable storage owned by this family.
            auto& pixels=*std::const_pointer_cast<std::vector<u32>>(prepared[i].second);
            const unsigned height=std::max(16u,meta.height);
            if(height==meta.height) std::copy(update.rgba->begin(),update.rgba->end(),pixels.begin());
            else for(unsigned row=0;row<height;++row)
                std::copy_n(update.rgba->data()+size_t(row*meta.height/height)*meta.width,meta.width,pixels.data()+size_t(row)*meta.width);
        }
        // No allocations or validation after the first shared-state mutation.
        ++serial_; allocations_+=allocations; reuses_+=reuses;
        for(auto& [index,pixels]:prepared) revisions_[index]={std::move(pixels),serial_};
        return true;
    }
};
} // namespace gt3r::render::deko
