#include "render/texture_rotation.h"
#include "test_framework.h"

TEST(render_texture_rotation_retains_three_images) {
    gt3r::render::TextureRotation<unsigned> ring;
    CHECK(!ring.initialized());
    ring.adopt(10);
    CHECK(ring.initialized() && ring.target() == 0);
    // A failed allocation must leave the target and owned images intact.
    CHECK(ring.target() == 0 && ring.handles()[0] == 10);
    ring.advance(20);
    CHECK(ring.target() == 0);
    ring.advance(30);
    CHECK(ring.target() == 10);
    ring.advance(ring.target());
    CHECK(ring.target() == 20);
    ring.advance(ring.target());
    CHECK(ring.target() == 30);
    CHECK((ring.handles() == std::array<unsigned, 3>{10, 20, 30}));
}
