#pragma once

namespace gt3r::render::deko {
// Several UI batches can be submitted before one visible frame is presented.
// Keep the acquired swapchain image separate from the command-memory ring.
template<unsigned Slots> class SubmissionFrame {
    static_assert(Slots > 0);
    int image_ = -1;
    unsigned next_ = 0;
public:
    template<class Acquire> int acquire(Acquire&& acquire_image) {
        if (image_ < 0) image_ = acquire_image();
        return image_;
    }
    unsigned next_submission() {
        const unsigned slot = next_;
        next_ = (next_ + 1) % Slots;
        return slot;
    }
    void presented() { image_ = -1; }
};
} // namespace gt3r::render::deko
