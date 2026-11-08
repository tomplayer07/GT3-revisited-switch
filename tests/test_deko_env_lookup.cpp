#include "test_framework.h"
#include "render/deko/env_lookup.h"
#include <array>
#include <limits>
using namespace gt3r::render;
namespace {
Vec3 trig_reference(Vec3 e) {
    const double theta=2*std::asin(0.9*std::sin(std::acos(std::clamp(double(e.z),-1.0,1.0))*0.5));
    const double phi=std::atan2(double(e.y),double(e.x));
    return {float(std::sin(theta)*std::cos(phi)),float(std::sin(theta)*std::sin(phi)),float(std::cos(theta))};
}
void check_direction(Vec3 e) {
    const auto actual=deko::compressed_env_direction(e),expected=trig_reference(e);
    CHECK(std::isfinite(actual.x)); CHECK(std::isfinite(actual.y)); CHECK(std::isfinite(actual.z));
    CHECK(length(actual-expected)<0.00002f);
    CHECK(std::abs(length(actual)-1)<0.00002f);
}
}
TEST(deko_environment_projection_matches_trigonometric_mapping_across_directions) {
    uint32_t state=123456;
    const auto random=[&] { state=1664525u*state+1013904223u; return float(state>>8)/8388607.5f-1; };
    for(unsigned i=0;i<65536;++i) {
        const Vec3 e{random(),random(),random()};
        if(length(e)>0.0001f) check_direction(normalize(e));
        check_direction(e); // original formula clamps z and derives azimuth independently
    }
}
TEST(deko_environment_projection_preserves_poles_seams_and_z_clamps) {
    for(float z:{-1.01f,-1.f,std::nextafter(-1.f,0.f),-0.5f,0.f,0.5f,std::nextafter(1.f,0.f),1.f,1.01f})
        for(float xy:{0.f,1e-9f,1e-7f,0.000001f,0.0001f,1.f})
            for(float sign:{-1.f,1.f}) {
                check_direction({xy,sign*xy,z});
                check_direction({-xy,sign*xy,z});
                check_direction({sign*xy,0,z});
                check_direction({0,sign*xy,z});
            }
}
