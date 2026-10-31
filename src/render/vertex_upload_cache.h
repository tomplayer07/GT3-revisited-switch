#pragma once

#include "scene/model.h"
#include <cstring>
#include <span>
#include <vector>

namespace gt3r::render {

// Snapshot of bytes successfully uploaded to a dynamic vertex buffer. Includes normals,
// UVs and colours: stationary geometry can still animate any of these attributes.
class VertexUploadCache {
public:
    bool matches(std::span<const scene::Vertex> vertices) const {
        return valid_ && vertices.size() == vertices_.size() &&
               (vertices.empty() || std::memcmp(vertices.data(), vertices_.data(), vertices.size_bytes()) == 0);
    }
    void remember(std::span<const scene::Vertex> vertices) {
        vertices_.assign(vertices.begin(), vertices.end());
        valid_ = true;
    }
    void invalidate() { valid_ = false; }
private:
    bool valid_ = false;
    std::vector<scene::Vertex> vertices_;
};

} // namespace gt3r::render
