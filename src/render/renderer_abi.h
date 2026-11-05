#pragma once
#include "render/view3d.h"
#include <cstddef>

namespace gt3r::render {
struct RendererAbi {
    size_t context, model, view, texture_info;
    unsigned features;
    bool operator==(const RendererAbi&) const = default;
};
// Internal linkage is intentional: sizes must come from this translation unit,
// not a weak inline definition selected by the linker from another build.
static constexpr RendererAbi local_renderer_abi() {
    unsigned features=0;
#if defined(__SWITCH__)
    features|=1;
#endif
#if GT3R_GLES
    features|=2;
#endif
#if GT3R_XR
    features|=4;
#endif
    return {sizeof(GpuContext),sizeof(GpuModel),sizeof(View3D),sizeof(GpuModel::TextureInfo),features};
}
// Compiled into the engine. Call before allocating renderer classes in a client
// built separately (such as a diagnostic executable linked to the engine).
bool verify_renderer_abi(const RendererAbi& caller);
} // namespace gt3r::render
