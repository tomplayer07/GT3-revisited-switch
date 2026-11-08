#pragma once
#include "render/view3d.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace gt3r::render::deko {
inline std::array<float,6> post_grade_key(const DrawOptions::PostGrade& g) {
    const std::array<float,6> key{g.black,g.white,g.curve[0],g.curve[1],g.curve[2],g.saturation};
    for(float x:key) if(!std::isfinite(x)) throw std::invalid_argument("Invalid native colour grade");
    return key;
}
// Rows 0..2 are the R/G/B input contributions to each output channel. Keep
// physical height 16: the native uploader expands smaller images vertically.
// Contributions are nonnegative. Clamping each to 255 before adding preserves
// min(sum,255), including saturation >1 and the special unclamped entry zero.
inline std::array<u32,256*16> make_post_grade_lut(const DrawOptions::PostGrade& g) {
    post_grade_key(g);
    std::array<u32,256*16> pixels{};
    const float delta=g.white-g.black,inv=delta==0 ? 0 : 1/delta;
    const float sp=std::min(128*g.saturation,255.0f),m=std::max(128-sp,0.0f);
    const float weights[3]={0.30f,0.59f,0.11f};
    for(unsigned lane=0;lane<3;++lane) for(unsigned input=0;input<256;++input) {
        u32 rgba=0xff000000u;
        float t=(float(input)/255-g.black)*inv;
        if(input) t=std::clamp(t,0.0f,1.0f);
        for(unsigned output=0;output<3;++output) {
            const float curve=std::clamp(g.curve[output],0.0f,1.0f);
            const float table=std::floor(std::clamp(255*(t*t+2*curve*t*(1-t)),0.0f,255.0f));
            const float mix=output==lane ? std::floor(std::clamp(sp+m*weights[lane],0.0f,255.0f)) : std::floor(m*weights[lane]);
            const float contribution=std::clamp(std::floor(table*mix/128),0.0f,255.0f);
            if(!std::isfinite(contribution)) throw std::invalid_argument("Invalid native colour grade range");
            rgba|=u32(contribution)<<(output*8);
        }
        pixels[lane*256+input]=rgba;
    }
    return pixels;
}
} // namespace gt3r::render::deko
