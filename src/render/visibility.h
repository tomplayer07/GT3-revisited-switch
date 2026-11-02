#pragma once

#include "render/math3d.h"
#include "scene/model.h"

#include <algorithm>
#include <limits>

namespace gt3r::render {

// CPU visibility in the engine's reversed [0,1] depth convention. Invalid
// inputs keep geometry visible; a small margin retains near-tangent spheres.
class ViewFrustum {
public:
    explicit ViewFrustum(const Mat4& view_projection) {
        for (int i = 0; i < 4; ++i) {
            const float x = view_projection.at(0, i), y = view_projection.at(1, i);
            const float z = view_projection.at(2, i), w = view_projection.at(3, i);
            planes_[0][i] = w + x; planes_[1][i] = w - x;
            planes_[2][i] = w + y; planes_[3][i] = w - y;
            planes_[4][i] = z; planes_[5][i] = w - z;
        }
        for (auto& p : planes_) {
            const float n = std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);
            if (!std::isfinite(n) || n <= 0) { valid_ = false; break; }
            for (float& v : p) {
                v /= n;
                if (!std::isfinite(v)) valid_ = false;
            }
        }
    }

    bool intersects_sphere(const Vec3& center, float radius) const {
        if (!valid_ || !std::isfinite(radius) || radius < 0 ||
            !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z)) return true;
        for (const auto& p : planes_)
            if (p[0]*center.x + p[1]*center.y + p[2]*center.z + p[3] < -radius - 0.05f) return false;
        return true;
    }

private:
    float planes_[6][4] = {};
    bool valid_ = true;
};

// Radius about the model origin, including mesh transforms and every LOD.
// Calculated at load time, independent of texture/material batching.
inline float model_origin_radius(const scene::Model& model) {
    float radius = 0;
    for (const scene::Mesh& mesh : model.meshes) {
        const Mat4 at = Mat4::from(mesh.transform);
        for (const scene::Vertex& v : mesh.vertices) {
            const float d = length(at.transform_point({v.pos[0], v.pos[1], v.pos[2]}));
            if (!std::isfinite(d)) return std::numeric_limits<float>::infinity();
            radius = std::max(radius, d);
        }
    }
    return radius;
}

} // namespace gt3r::render
