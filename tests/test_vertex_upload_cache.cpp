#include "render/vertex_upload_cache.h"
#include "test_framework.h"

using namespace gt3r;

TEST(render_vertex_cache_observes_every_animated_attribute) {
    render::VertexUploadCache cache;
    std::vector<scene::Vertex> vertices(4);
    CHECK(!cache.matches(vertices));
    cache.remember(vertices);
    CHECK(cache.matches(vertices));
    auto changed = vertices;
    changed[1].pos[2] = 2;
    CHECK(!cache.matches(changed));
    changed = vertices;
    changed[1].normal[1] = 1;
    CHECK(!cache.matches(changed));
    changed = vertices;
    changed[1].uv[0] = 0.5f;
    CHECK(!cache.matches(changed));
    changed = vertices;
    changed[1].color = 0x00FFFFFF;
    CHECK(!cache.matches(changed));
    changed = vertices;
    changed.pop_back();
    CHECK(!cache.matches(changed));
    // The snapshot must not alias its caller's mutable CPU geometry.
    vertices[0].pos[0] = 5;
    CHECK(!cache.matches(vertices));
    cache.remember(vertices);
    CHECK(cache.matches(vertices));
    cache.invalidate(); // e.g. the GPU buffer was grown or an upload failed
    CHECK(!cache.matches(vertices));
}
