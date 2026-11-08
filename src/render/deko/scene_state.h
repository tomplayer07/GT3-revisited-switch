#pragma once
#include "render/view3d.h"
#include <array>
#include <cmath>

namespace gt3r::render::deko {
// Layout shared by native CarLights and the slot-owned upload. Never retain
// ItemLights pointers: callers may build them on the stack for one draw only.
inline std::array<float,52> scene_lights(const DrawOptions& options,const DrawItem* item) {
    std::array<float,52> out{};
    const ItemLights* own=item ? item->lights : nullptr;
    for(int c=0;c<3;++c) out[c]=own ? own->ambient[c] : options.light_ambient[c];
    const int count=own ? std::clamp(own->count,0,4) : 1+std::clamp(options.fill_count,0,3);
    for(int i=0;i<count;++i) {
        for(int c=0;c<3;++c) {
            const float main_dir[3]={options.light_dir.x,options.light_dir.y,options.light_dir.z};
            out[4+i*4+c]=own ? own->dir[i][c] : i ? options.fill_dir[i-1][c] : main_dir[c];
            out[20+i*4+c]=(own ? own->diffuse[i][c] : i ? options.fill_diffuse[i-1][c] : options.light_diffuse[c])*(item ? item->light_scale[1] : 1);
            out[36+i*4+c]=(own ? own->specular[i][c] : i ? options.fill_specular[i-1][c] : options.light_specular[c])*(item ? item->light_scale[2] : 1);
        }
        out[7+i*4]=1;
    }
    for(int c=0;c<3;++c) out[c]*=item ? item->light_scale[0] : 1;
    return out;
}
// CourseFog block: first 32 floats preserve the prototype ABI; the final
// two vec4s carry native distance/output settings. One immutable copy per draw.
inline std::array<float,40> scene_fog(const DrawOptions& opt,const DrawItem* item,const Mat4& view,bool sphere) {
    std::array<float,40> out{};
    for(int c=0;c<4;++c) out[c]=view.at(2,c);
    for(int c=0;c<3;++c) {
        out[4+c]=opt.fog_color[c]; out[12+c]=view.at(0,c); out[16+c]=view.at(1,c);
        out[20+c]=opt.env_sky[c]; out[24+c]=opt.env_horizon[c]; out[28+c]=opt.env_ground[c];
    }
    out[7]=opt.fog && !(item && (item->background || item->helper || item->no_fog)) ? 1 : 0;
    out[8]=opt.fog_start; out[9]=1-std::clamp(opt.fog_max,0.0f,1.0f);
    out[10]=1; out[11]=sphere ? 1 : 0; out[19]=item ? item->alpha : 1;
    out[32]=opt.fog_start; out[33]=1/std::max(opt.fog_end-opt.fog_start,0.001f);
    out[34]=std::clamp(opt.fog_max,0.0f,1.0f);
    out[35]=item && (item->background || item->helper) ? 0 : opt.far_clip/std::max(item ? item->modelview_scale : 1.0f,0.000001f);
    out[36]=opt.quantize_rgb5 ? 1 : 0; out[37]=opt.env_gt3_lookup ? 1 : 0; out[38]=opt.fog_gt3 ? 1 : 0; out[39]=item && item->gs_bilinear ? 1 : 0;
    return out;
}
inline bool valid_scene_lights(const ItemLights& lights) {
    if(lights.count<0 || lights.count>4) return false;
    for(float v:lights.ambient) if(!std::isfinite(v)) return false;
    for(int i=0;i<lights.count;++i) for(int c=0;c<3;++c)
        if(!std::isfinite(lights.dir[i][c]) || !std::isfinite(lights.diffuse[i][c]) || !std::isfinite(lights.specular[i][c])) return false;
    return true;
}
inline int scene_cull(int cull,bool mirrored,bool background) {
    return background ? 0 : mirrored && cull ? 3-cull : cull;
}
// Apply the normal material policy to native race bodies. Other car parts keep
// their bring-up policy: wheels, projected shadows and morph helpers are not
// inferred to be closed bodies. Explicit double-sided materials stay two-sided.
inline int native_packet_cull(u32 flags,bool enabled,bool car,bool background,
        const DrawOptions* options,const DrawItem* item) {
    const bool material_policy=!car || (item && (item->sphere_map || item->car_surface));
    int cull=enabled && material_policy && !background && !(flags&scene::kDoubleSided) ? 1 : 0;
    if(options && options->cull) cull=int(*options->cull);
    if(item && item->single_sided && !background) cull=1;
    return scene_cull(cull,options && options->mirrored,background);
}
// Exact ordinary fragment modes only. Keep all car parts on the full shader
// to avoid shader-program churn inside their layered materials.
inline bool common_fragment(float mode,bool available,bool frontend,bool capturing,bool post,const DrawItem* item,ProfilePass pass=ProfilePass::Main) {
    return available && frontend && !capturing && !post && item &&
        pass==ProfilePass::Main && item->profile_role==ProfileRole::Scene && (mode==0 || mode==1);
}
} // namespace gt3r::render::deko
