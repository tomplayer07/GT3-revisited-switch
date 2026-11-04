#pragma once
#include "render/math3d.h"
#include <array>
#include <cmath>
namespace gt3r::deko_probe {
struct MeshBounds { render::Vec3 center{}, half{}; };
// Clip depth 0..1, including reversed depth. Conservative world AABB support.
class ViewCuller {
    std::array<std::array<float,4>,6> planes{};
    render::Vec3 eye;
    float distance;
public:
    ViewCuller(const render::Mat4& vp,render::Vec3 e,float maxDistance):eye(e),distance(maxDistance) {
        for (unsigned i=0;i<4;++i) {
            const float x=vp.at(0,i), y=vp.at(1,i), z=vp.at(2,i), w=vp.at(3,i);
            planes[0][i]=w+x; planes[1][i]=w-x; planes[2][i]=w+y;
            planes[3][i]=w-y; planes[4][i]=z; planes[5][i]=w-z;
        }
    }
    bool visible(const MeshBounds& b,const render::Mat4& world) const {
        const auto center=world.transform_point(b.center);
        float extent[3];
        for (unsigned r=0;r<3;++r) extent[r]=std::abs(world.at(r,0))*b.half.x+std::abs(world.at(r,1))*b.half.y+std::abs(world.at(r,2))*b.half.z;
        const render::Vec3 half{extent[0],extent[1],extent[2]};
        if (distance>0 && render::length(center-eye)-render::length(half)>distance) return false;
        for (const auto& p:planes) {
            const float support=std::abs(p[0])*half.x+std::abs(p[1])*half.y+std::abs(p[2])*half.z;
            if (p[0]*center.x+p[1]*center.y+p[2]*center.z+p[3]+support<0) return false;
        }
        return true;
    }
};
}
