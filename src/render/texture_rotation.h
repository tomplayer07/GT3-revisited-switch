#pragma once
#include <array>
#include <cstddef>

namespace gt3r::render {

// Keeps recently sampled images separate from the next update target. GL still
// provides implicit synchronization if the GPU is more than three updates behind.
// The caller owns/releases the handles and commits only successful updates.
template <class Handle>
class TextureRotation {
public:
    bool initialized() const { return images_[0] != Handle{}; }
    void adopt(Handle image) { images_[0] = image; current_ = 0; }
    Handle target() const { return images_[(current_ + 1) % images_.size()]; }
    void advance(Handle image) {
        current_ = (current_ + 1) % images_.size();
        images_[current_] = image;
    }
    const auto& handles() const { return images_; }
private:
    std::array<Handle, 3> images_{};
    size_t current_ = 0;
};

} // namespace gt3r::render
