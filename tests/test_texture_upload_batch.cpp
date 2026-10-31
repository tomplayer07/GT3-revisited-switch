#include "render/texture_upload_batch.h"
#include "test_framework.h"

#include <array>
#include <cstring>

using namespace gt3r;

TEST(render_texture_batch_owns_images_at_aligned_offsets) {
    render::TextureUploadBatch batch;
    std::array<u32, 3> strip{0x00000000, 0xFFFFFFFF, 0xFF123456};
    const auto expected = strip;
    std::array<u32, 4> square{1, 2, 3, 4};
    CHECK(batch.add(11, 3, 1, strip, false));
    CHECK(batch.add(12, 2, 2, square, true));
    strip.fill(0); // palette data can be reused immediately after staging
    CHECK(batch.images().size() == 2);
    const auto& a = batch.images()[0];
    const auto& b = batch.images()[1];
    CHECK(a.texture == 11 && a.width == 3 && a.height == 1 && !a.mipmaps);
    CHECK(b.texture == 12 && b.width == 2 && b.height == 2 && b.mipmaps);
    CHECK(a.offset % 4 == 0 && b.offset % 4 == 0);
    CHECK(b.offset >= a.offset + sizeof(expected));
    CHECK(std::memcmp(batch.data() + a.offset, expected.data(), sizeof(expected)) == 0);
    CHECK(std::memcmp(batch.data() + b.offset, square.data(), sizeof(square)) == 0);
    batch.clear();
    CHECK(batch.images().empty() && batch.size() == 0);
    CHECK(batch.add(13, 1, 3, expected, true));
    CHECK(batch.images()[0].offset == 0);
}

TEST(render_texture_batch_rejects_incomplete_images) {
    render::TextureUploadBatch batch;
    std::array<u32, 1> pixel{0xFFFFFFFF};
    CHECK(!batch.add(0, 1, 1, pixel, true));
    CHECK(!batch.add(1, 0, 1, pixel, true));
    CHECK(!batch.add(1, 2, 1, pixel, true));
    CHECK(!batch.add(1, 0xFFFFFFFFu, 0xFFFFFFFFu, pixel, true));
    CHECK(batch.images().empty() && batch.size() == 0);
}
