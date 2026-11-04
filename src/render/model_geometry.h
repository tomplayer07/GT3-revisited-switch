#pragma once
#include "render/math3d.h"
#include "scene/model.h"
#include <vector>

namespace gt3r::render {
struct ModelMeshInfo {
    Mat4 transform;
    int lod = 0;
    s32 vertex_offset = 0; // added to every index
    u32 index_offset = 0;  // first triangle index in the shared buffer
    u32 line_first = 0, line_count = 0;
    std::vector<scene::Primitive> primitives; // first_index relative to index_offset
    // Bounding sphere of the mesh's vertices in mesh space (before `transform`),
    // used for frustum culling, draw distance and transparency sorting.
    Vec3 center;
    float radius = 0;
    // Each primitive's own sphere (the vertices of its triangles), in the same space: the
    // guard band's clipping skips the primitives it cannot cut (View3D::upload_clipped).
    struct Sphere {
        Vec3 center;
        float radius = 0;
    };
    std::vector<Sphere> primitive_spheres; // parallel to `primitives`
};


// Shared CPU preparation; local indices are relative to each mesh's vertex_offset.
// Set merge=false to preserve primitive packet order, e.g. native car passes.
void assemble_model_geometry(const std::vector<scene::Mesh>& meshes, size_t texture_count, bool lines,
    std::vector<scene::Vertex>& vertices, std::vector<u32>& indices,
    std::vector<ModelMeshInfo>& out, bool merge = true);
} // namespace gt3r::render
