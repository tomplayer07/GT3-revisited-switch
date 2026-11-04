// Host-only geometric regression checks; not linked into the Switch NRO.
#include "visibility.h"
#include <cassert>
#include <cstdio>
using namespace gt3r;
using namespace gt3r::deko_probe;
int main() {
    const auto identity=render::Mat4::identity();
    const auto projection=render::perspective(1.570796327f,1,0.1f,1000);
    const ViewCuller front(projection,{0,0,0},0);
    assert(front.visible({{0,0,-10},{1,1,1}},identity));
    assert(!front.visible({{0,0,10},{1,1,1}},identity));
    assert(!front.visible({{0,0,-0.03f},{0.01f,0.01f,0.01f}},identity));
    assert(!front.visible({{0,0,-2000},{1,1,1}},identity));
    assert(front.visible({{10,0,-10},{1,1,1}},identity)); // crosses right plane
    assert(front.visible({{0,10,-10},{1,1,1}},identity)); // crosses top plane
    assert(!front.visible({{20,0,-10},{1,1,1}},identity));
    assert(front.visible({{20,0,-10},{11,1,1}},identity)); // centre outside, box intersects
    assert(front.visible({{0,0,0},{1,1,1}},render::translate({0,0,-10})));
    const auto reflected=render::translate({0,0,-10})*render::scale({-3,2,1});
    assert(front.visible({{0,0,0},{1,1,1}},reflected));
    const ViewCuller right(projection*render::look_at({20,0,0},{30,0,0},{0,1,0}),{20,0,0},0);
    assert(right.visible({{30,0,0},{1,1,1}},identity));
    assert(!right.visible({{10,0,0},{1,1,1}},identity));
    const ViewCuller distance(projection,{0,0,0},600);
    assert(!distance.visible({{0,0,-700},{1,1,1}},identity));
    assert(distance.visible({{0,0,-605},{10,10,10}},identity));
    std::puts("Visibility: 14 geometric regression checks passed");
}
