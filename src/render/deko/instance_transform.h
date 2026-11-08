#pragma once
#include "render/math3d.h"
#include <optional>
#include <cstdint>
#include <cstddef>
#include <span>
namespace gt3r::render::deko {
// Cache the item placement, not the camera. Invalidate when local geometry
// transforms change. Clear before applying so partial failure cannot validate
// a previous placement that no longer matches the model.
class InstanceTransformCache {
    std::optional<Mat4> world_;
public:
    uint64_t skips=0,products=0;
    void invalidate() { world_.reset(); }
    template<class Apply> bool apply(const Mat4& world,std::span<const Mat4> local,Apply&& sink) {
        if(world_ && world_->m==world.m) { ++skips; return true; }
        world_.reset();
        for(size_t i=0;i<local.size();++i) { ++products; if(!sink(i,world*local[i])) return false; }
        world_=world; return true;
    }
};
} // namespace gt3r::render::deko
