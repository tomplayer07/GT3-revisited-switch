#pragma once

#include "base/bytes.h"
#include "render/uniform_staging.h"
#include <limits>
#include <span>
#include <vector>

namespace gt3r::render {

// Owns a snapshot of each image until the batch is submitted. Offsets are byte offsets
// in a pixel-unpack buffer, not pointers into the caller's mutable palette data.
class TextureUploadBatch {
public:
    struct Image {
        u32 texture, width, height;
        size_t offset;
        bool mipmaps;
    };
    void clear() { images_.clear(); bytes_.clear(); }
    bool add(u32 texture, u32 width, u32 height, std::span<const u32> pixels, bool mipmaps) {
        const u64 count = u64(width) * height;
        if (!texture || !count || count > std::numeric_limits<size_t>::max() / sizeof(u32) || count != pixels.size())
            return false;
        const size_t offset = bytes_.append(pixels.data(), pixels.size_bytes(), sizeof(u32));
        images_.push_back({texture, width, height, offset, mipmaps});
        return true;
    }
    const auto& images() const { return images_; }
    const std::byte* data() const { return bytes_.data(); }
    size_t size() const { return bytes_.size(); }
private:
    UniformStaging bytes_;
    std::vector<Image> images_;
};

} // namespace gt3r::render
