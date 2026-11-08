#pragma once
#include "scene/model.h"

namespace gt3r::render::deko {
enum class NativeBlend { Opaque, Alpha, Additive, MultiplyDest, DestAlphaAdd, DestAlphaLerp };
struct PrimitiveState {
    NativeBlend blend=NativeBlend::Opaque;
    bool sphere=false, reflection=false, skip=false, transparent=false;
    bool depth_only=false, depth_write=false;
    bool alpha_only=false, keep_alpha=false;
};
// Keep the validated race/car bring-up policy until a caller opts into native
// item semantics. Prize sphere maps use their own texture, not the car env map.
inline PrimitiveState primitive_state(u32 flags,bool textured,bool sphere_maps,bool frontend,bool sky,bool car,bool frame_alpha=false,unsigned dest_alpha_blend=0) {
    PrimitiveState state;
    state.sphere=frontend && sphere_maps && textured && (flags&scene::kEnvironmentMap);
    state.reflection=!state.sphere && (flags&(scene::kAdditive|scene::kEnvironmentMap))==(scene::kAdditive|scene::kEnvironmentMap);
    state.depth_only=(flags&scene::kDepthOnly)!=0;
    if(frontend) {
        state.skip=((flags&scene::kAlphaOnly) && !frame_alpha) || ((flags&scene::kEnvironmentMap) && !state.sphere && !state.reflection);
        state.alpha_only=frame_alpha && (flags&scene::kAlphaOnly);
        state.keep_alpha=(frame_alpha && (flags&scene::kKeepAlpha)) || dest_alpha_blend;
        state.blend=dest_alpha_blend==1 ? NativeBlend::DestAlphaLerp :
                    (dest_alpha_blend==2 || (frame_alpha && (flags&scene::kAdditive) && (flags&scene::kDestAlphaAdd))) ? NativeBlend::DestAlphaAdd :
                    (flags&scene::kMultiplyDest) ? NativeBlend::MultiplyDest :
                    (state.reflection || (flags&scene::kAdditive)) ? NativeBlend::Additive :
                    (flags&scene::kAlphaBlend) ? NativeBlend::Alpha : NativeBlend::Opaque;
    } else {
        state.skip=!state.reflection && (flags&(scene::kAdditive|scene::kEnvironmentMap|scene::kMultiplyDest|scene::kAlphaOnly));
        state.blend=state.reflection ? NativeBlend::Additive : (flags&scene::kAlphaBlend) ? NativeBlend::Alpha : NativeBlend::Opaque;
    }
    state.transparent=state.depth_only || state.blend!=NativeBlend::Opaque;
    state.depth_write=!sky && !(flags&scene::kNoDepthWrite) && !state.reflection &&
        ((frontend && sphere_maps) || !car || state.blend==NativeBlend::Opaque);
    if(state.depth_only) { state.blend=NativeBlend::Opaque; state.depth_write=!sky && !(flags&scene::kNoDepthWrite); }
    return state;
}
} // namespace gt3r::render::deko
