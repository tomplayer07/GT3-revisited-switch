#include "render/uniform_staging.h"
#include "test_framework.h"

#include <array>
#include <cstring>

using gt3r::render::UniformStaging;

TEST(render_uniform_ranges_preserve_pending_draws) {
    // Many different materials in flight: every saved range must retain its own
    // bytes, even after CPU storage reallocations. Exercise common UBO alignments.
    for (size_t alignment : {size_t(16), size_t(256)}) {
        UniformStaging staging;
        std::vector<size_t> offsets;
        for (unsigned i = 0; i < 300; ++i) {
            std::array<unsigned, 57> material;
            material.fill(i);
            offsets.push_back(staging.append(material.data(), sizeof(material), alignment));
        }
        for (unsigned i = 0; i < offsets.size(); ++i) {
            CHECK(offsets[i] % alignment == 0);
            if (i) CHECK(offsets[i] >= offsets[i - 1] + 57 * sizeof(unsigned));
            std::array<unsigned, 57> material;
            std::memcpy(material.data(), staging.data() + offsets[i], sizeof(material));
            for (unsigned value : material) CHECK(value == i);
        }
    }
}

TEST(render_uniform_ranges_restart_for_next_view) {
    UniformStaging staging;
    const std::array<unsigned, 4> lit{1, 2, 3, 4}, unlit{};
    const size_t first = staging.append(lit.data(), sizeof(lit), 256);
    const size_t second = staging.append(unlit.data(), sizeof(unlit), 256);
    CHECK(std::memcmp(staging.data() + first, lit.data(), sizeof(lit)) == 0);
    CHECK(std::memcmp(staging.data() + second, unlit.data(), sizeof(unlit)) == 0);
    staging.clear();
    CHECK(staging.size() == 0);
    const size_t next = staging.append(unlit.data(), sizeof(unlit), 256);
    CHECK(next == 0);
    CHECK(std::memcmp(staging.data() + next, unlit.data(), sizeof(unlit)) == 0);
}
