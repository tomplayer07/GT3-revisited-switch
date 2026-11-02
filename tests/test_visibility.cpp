#include "test_framework.h"
#include "render/visibility.h"

using namespace gt3r;
using namespace gt3r::render;

TEST(visibility_spheres_keep_edges_and_clip_depth) {
    const ViewFrustum view(perspective(1.5707963f, 1, 0.1f, 100));
    CHECK(view.intersects_sphere({0, 0, -10}, 1));
    CHECK(!view.intersects_sphere({0, 0, 10}, 1));
    CHECK(view.intersects_sphere({11, 0, -10}, 1)); // centre outside; sphere still on screen
    CHECK(!view.intersects_sphere({15, 0, -10}, 1));
    CHECK(view.intersects_sphere({0, 11, -10}, 1));
    CHECK(!view.intersects_sphere({0, 15, -10}, 1));
    CHECK(view.intersects_sphere({0, 0, -101}, 2));
    CHECK(!view.intersects_sphere({0, 0, -104}, 1));
    CHECK(view.intersects_sphere({0, 0, 0}, 0.2f)); // crosses the near plane
}

TEST(visibility_camera_views_are_independent) {
    const Mat4 proj = perspective(1.5707963f, 1, 0.1f, 100);
    const ViewFrustum front(proj * look_at({100, 3, 50}, {100, 3, 40}, {0, 1, 0}));
    const ViewFrustum rear(proj * look_at({100, 3, 50}, {100, 3, 60}, {0, 1, 0}));
    CHECK(front.intersects_sphere({100, 3, 40}, 1));
    CHECK(!rear.intersects_sphere({100, 3, 40}, 1));
    CHECK(rear.intersects_sphere({100, 3, 60}, 1));
    CHECK(!front.intersects_sphere({100, 3, 60}, 1));
}

TEST(visibility_invalid_data_keeps_geometry) {
    const ViewFrustum view(perspective(1, 1, 0.1f, 100));
    CHECK(view.intersects_sphere({0, 0, 10}, std::numeric_limits<float>::infinity()));
    CHECK(view.intersects_sphere({NAN, 0, 10}, 1));
    Mat4 invalid;
    invalid.m.fill(0);
    CHECK(ViewFrustum(invalid).intersects_sphere({0, 0, 10}, 1));
}

TEST(visibility_model_radius_includes_mesh_transform_and_all_lods) {
    scene::Model m;
    scene::Mesh a;
    a.vertices.resize(1);
    a.vertices[0].pos[0] = 1;
    a.transform = translate({10, 0, 0}).m;
    m.meshes.push_back(a);
    a.lod = 2;
    a.transform = translate({-20, 0, 0}).m;
    m.meshes.push_back(a);
    CHECK(std::fabs(model_origin_radius(m) - 19) < 0.001f);
    m.meshes[0].vertices[0].pos[0] = NAN;
    CHECK(std::isinf(model_origin_radius(m)));
}
