#include "test_framework.h"
#include "render/deko/post_grade_lut.h"
#include <limits>
#include <vector>
using namespace gt3r;
using namespace gt3r::render;
namespace {
std::array<u8,3> lut_pixel(const std::array<u32,256*16>& lut,u8 r,u8 g,u8 b) {
    const u32 pixels[]={lut[r],lut[256+g],lut[512+b]};
    std::array<u8,3> out{};
    for(unsigned c=0;c<3;++c) {
        unsigned sum=0;
        for(u32 pixel:pixels) sum+=(pixel>>(c*8))&255;
        out[c]=u8(std::min(sum,255u));
    }
    return out;
}
std::vector<DrawOptions::PostGrade> grades() {
    std::vector<DrawOptions::PostGrade> out(8);
    out[1].white=0.8f; out[1].curve[0]=0.87f; out[1].curve[1]=0.90f; out[1].curve[2]=0.95f;
    out[2].black=1; out[2].white=0;
    out[3].black=0.6f; out[3].white=0.61f;
    out[4].black=out[4].white=0.5f;
    out[5].curve[0]=1; out[5].curve[1]=0; out[5].curve[2]=0; out[5].saturation=0;
    out[6].saturation=3; out[6].curve[0]=1; out[6].curve[1]=0.1f;
    out[7].saturation=-0.5f; out[7].black=-0.1f; out[7].white=0.9f;
    return out;
}
}
TEST(deko_grade_lut_matches_original_formula_for_channel_ramps_black_and_clipping) {
    for(const auto& grade:grades()) {
        const auto lut=deko::make_post_grade_lut(grade);
        for(unsigned i=0;i<256;++i) {
            for(const auto rgb:{std::array<u8,3>{u8(i),0,0},{0,u8(i),0},{0,0,u8(i)},{u8(i),u8(i),u8(i)},{u8(i),255,255},{255,u8(i),255},{255,255,u8(i)}})
                CHECK(lut_pixel(lut,rgb[0],rgb[1],rgb[2])==post_grade_pixel(grade,rgb[0],rgb[1],rgb[2]));
        }
    }
}
TEST(deko_grade_lut_matches_original_formula_for_mixed_colours_and_preset_changes) {
    uint32_t state=12345;
    for(const auto& grade:grades()) {
        const auto lut=deko::make_post_grade_lut(grade);
        for(unsigned i=0;i<16384;++i) {
            state=state*1664525u+1013904223u;
            const u8 r=u8(state),g=u8(state>>8),b=u8(state>>16);
            CHECK(lut_pixel(lut,r,g,b)==post_grade_pixel(grade,r,g,b));
        }
    }
    DrawOptions::PostGrade grade;
    const auto before=deko::post_grade_key(grade);
    grade.curve[2]=0.9f; CHECK(deko::post_grade_key(grade)!=before);
    grade={}; grade.saturation=0.7f; CHECK(deko::post_grade_key(grade)!=before);
}
TEST(deko_grade_lut_rejects_nonfinite_settings_before_upload) {
    DrawOptions::PostGrade grade;
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        grade.white=bad; bool rejected=false;
        try { deko::make_post_grade_lut(grade); } catch(const std::invalid_argument&) { rejected=true; }
        CHECK(rejected);
    }
}
