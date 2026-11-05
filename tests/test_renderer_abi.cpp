#include "test_framework.h"
#include "render/renderer_abi.h"
using namespace gt3r::render;
TEST(renderer_abi_rejects_client_platform_and_layout_mismatches) {
    const auto valid=local_renderer_abi();
    CHECK(verify_renderer_abi(valid));
    auto wrong=valid; wrong.features^=1;
    CHECK(!verify_renderer_abi(wrong));
    CHECK(std::string(SDL_GetError()).find("Renderer ABI mismatch")!=std::string::npos);
    wrong=valid; wrong.model+=8;
    CHECK(!verify_renderer_abi(wrong));
    wrong=valid; wrong.view+=8;
    CHECK(!verify_renderer_abi(wrong));
    wrong=valid; wrong.texture_info+=8;
    CHECK(!verify_renderer_abi(wrong));
}
