#pragma once
#include "render/math3d.h"
#include <algorithm>
#include <cmath>
namespace gt3r::render::deko {
// CPU counterpart of the native fragment shader's GT3 environment projection.
// theta' = 2 asin(.9 sin(acos(z)/2)); eliminate trigonometry on the regular
// path using half/double-angle identities. Retain the old polar calculation
// when azimuth normalization would amplify tiny coordinates.
inline Vec3 compressed_env_direction(Vec3 e) {
    const float z=std::clamp(e.z,-1.0f,1.0f),xy2=e.x*e.x+e.y*e.y;
    if(xy2>1e-12f) {
        const float s=0.9f*std::sqrt(std::max(0.0f,(1-z)*0.5f));
        const float radial=2*s*std::sqrt(std::max(0.0f,1-s*s));
        const float scale=radial/std::sqrt(xy2);
        return {e.x*scale,e.y*scale,1-2*s*s};
    }
    const float theta=2*std::asin(0.9f*std::sin(std::acos(z)*0.5f));
    const float phi=std::atan2(e.y,e.x);
    return {std::sin(theta)*std::cos(phi),std::sin(theta)*std::sin(phi),std::cos(theta)};
}
} // namespace gt3r::render::deko
