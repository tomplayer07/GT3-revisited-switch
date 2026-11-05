#include "render/renderer_abi.h"

namespace gt3r::render {
bool verify_renderer_abi(const RendererAbi& caller) {
    const auto engine=local_renderer_abi();
    if(caller==engine) return true;
    return SDL_SetError("Renderer ABI mismatch: client %zu/%zu/%zu/%zu flags=%u, engine %zu/%zu/%zu/%zu flags=%u",
        caller.context,caller.model,caller.view,caller.texture_info,caller.features,
        engine.context,engine.model,engine.view,engine.texture_info,engine.features);
}
} // namespace gt3r::render
