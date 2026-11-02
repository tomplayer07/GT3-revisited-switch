#include "test_framework.h"
#include "race/lamp_visibility_cache.h"

using gt3r::race::LampVisibilityCache;
using namespace gt3r::render;

TEST(lamp_visibility_cache_staggers_stable_ids_and_keeps_channels_independent) {
    LampVisibilityCache cache;
    cache.begin(2, 2, {}, {}, {}, 0);
    CHECK(cache.sample(0, 0, [] { return 0.25f; }) == 0.25f);
    CHECK(cache.sample(1, 0, [] { return 0.75f; }) == 0.75f);
    CHECK(cache.queries == 2);
    cache.begin(2, 2, {}, {}, {}, 0.016);
    // Reordered visible list: lamp 0 is due, lamp 1 retains its previous result.
    CHECK(cache.sample(1, 0, [] { return 0.0f; }) == 0.75f);
    CHECK(cache.sample(0, 0, [] { return 0.5f; }) == 0.5f);
    CHECK(cache.queries == 1 && cache.reused == 1);
    // Halo was not queried before: compute it even on an unscheduled lamp.
    CHECK(cache.sample(1, 1, [] { return 0.0f; }) == 0.0f);
    cache.begin(2, 2, {}, {}, {}, 0.032);
    CHECK(cache.sample(0, 0, [] { return 1.0f; }) == 0.5f);
    CHECK(cache.sample(1, 0, [] { return 0.0f; }) == 0.0f);
    CHECK(cache.queries == 1 && cache.reused == 1);
}

TEST(lamp_visibility_cache_refreshes_reappearing_lamps_and_camera_cuts) {
    LampVisibilityCache cache;
    cache.begin(1, 2, {}, {}, {}, 0);
    cache.sample(0, 0, [] { return 1.0f; });
    cache.begin(1, 2, {}, {}, {}, 0.016); // off-screen, not sampled
    cache.begin(1, 2, {}, {}, {}, 0.032);
    CHECK(cache.sample(0, 0, [] { return 0.0f; }) == 0.0f);
    cache.begin(1, 2, {5, 0, 0}, {}, {}, 0.048);
    CHECK(cache.sample(0, 0, [] { return 1.0f; }) == 1.0f);
    Mat4 rear;
    rear.at(0, 0) = rear.at(2, 2) = -1;
    cache.begin(1, 2, {5, 0, 0}, rear, {}, 0.064);
    CHECK(cache.sample(0, 0, [] { return 0.0f; }) == 0.0f);
    cache.begin(1, 2, {5, 0, 0}, rear, {}, 0.3);
    CHECK(cache.sample(0, 0, [] { return 1.0f; }) == 1.0f);
    Frustum zoom;
    zoom.up = 0.5f;
    cache.begin(1, 2, {5, 0, 0}, rear, zoom, 0.316);
    CHECK(cache.sample(0, 0, [] { return 0.0f; }) == 0.0f);
}

TEST(lamp_visibility_cache_reference_mode_never_reuses_results) {
    LampVisibilityCache cache;
    for (int i = 0; i < 4; ++i) {
        cache.begin(1, 1, {}, {}, {}, i * 0.016);
        CHECK(cache.sample(0, 0, [i] { return float(i); }) == float(i));
        CHECK(cache.queries == 1 && cache.reused == 0);
    }
}

TEST(lamp_visibility_cache_camera_changes_refresh_unscheduled_lamps) {
    for (int change = 0; change < 5; ++change) {
        LampVisibilityCache cache;
        cache.begin(2, 2, {}, {}, {}, 1.0);
        cache.sample(1, 0, [] { return 1.0f; });
        Vec3 eye;
        Mat4 view;
        Frustum fr;
        double time = 1.016;
        if (change == 0) eye.x = 5;
        if (change == 1) view.at(0, 0) = view.at(2, 2) = -1;
        if (change == 2) fr.right = 0.5f;
        if (change == 3) time = 1.2;
        if (change == 4) time = 0.5;
        // Frame 2 is lamp 0's turn. Lamp 1 must still refresh on every cut.
        cache.begin(2, 2, eye, view, fr, time);
        CHECK(cache.sample(1, 0, [] { return 0.0f; }) == 0.0f);
        CHECK(cache.queries == 1 && cache.reused == 0);
    }
}
