#pragma once
#include "render/deko/sdl_backend.h"
#include <memory>

namespace gt3r::render::deko {
// A copy of the provider contract, retained across queued and submitted draws.
class BorrowedImageLease final {
public:
    static std::shared_ptr<BorrowedImageLease> acquire(const GT3R_DekoImage& image,
                                                       void* backend, int width, int height) {
        if (image.version != GT3R_DEKO_IMAGE_VERSION || image.backend_userdata != backend ||
            width <= 0 || height <= 0 || image.width != width || image.height != height ||
            !image.retain || !image.release || !image.descriptor) {
            SDL_SetError("Deko SDL: invalid borrowed image contract");
            return {};
        }
        return std::shared_ptr<BorrowedImageLease>(new BorrowedImageLease(image));
    }
    ~BorrowedImageLease() { source.release(source.userdata); }
    BorrowedImageLease(const BorrowedImageLease&) = delete;
    BorrowedImageLease& operator=(const BorrowedImageLease&) = delete;
    const GT3R_DekoImage source;
private:
    explicit BorrowedImageLease(const GT3R_DekoImage& image): source(image) { source.retain(source.userdata); }
};
} // namespace gt3r::render::deko
