// Host-only regressions for wheel transforms over already placed geometry.
#include "wheel_pose.h"
#include <cassert>
#include <cstdio>
using namespace gt3r;
using namespace gt3r::deko_probe;
static void close(render::Vec3 a,render::Vec3 b) { assert(render::length(a-b)<0.0001f); }
int main() {
    constexpr float halfPi=1.570796327f;
    const render::Vec3 hub{-0.8f,0.3f,-1.3f};
    sim::VehicleSetup::Wheel setup; setup.rest_center={hub.x,hub.y,hub.z}; setup.static_suspension=0.1f;
    sim::WheelState state; state.suspension=0.1f;
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub),hub);
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub+render::Vec3{0,1,0}),hub+render::Vec3{0,1,0});
    state.spin_angle=halfPi;
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub+render::Vec3{0,1,0}),hub+render::Vec3{0,0,-1});
    close(wheel_delta(0,hub,state,setup,false).transform_point(hub+render::Vec3{0,1,0}),hub+render::Vec3{0,1,0});
    state.spin_angle=0; state.steer_angle=halfPi;
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub),hub);
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub+render::Vec3{0,0,1}),hub+render::Vec3{-1,0,0});
    state.steer_angle=0; state.suspension=0.2f;
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub),hub+render::Vec3{0,0.1f,0});
    state.suspension=0.1f; state.camber=halfPi;
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub+render::Vec3{1,0,0}),hub+render::Vec3{0,1,0});
    close(wheel_delta(1,hub,state,setup,true).transform_point(hub+render::Vec3{1,0,0}),hub+render::Vec3{0,-1,0});
    state.camber=0; state.spin_angle=halfPi+6.283185307f;
    close(wheel_delta(0,hub,state,setup,true).transform_point(hub+render::Vec3{0,1,0}),hub+render::Vec3{0,0,-1});
    sim::VehicleSetup car; car.wheels[0].ride_height=0.075f;
    assert(car_body_lift(car)==0.075f); car.wheels[0].ride_height=0; assert(car_body_lift(car)==0.13f);
    std::puts("Wheel pose: 12 static, spin, hub, steering, suspension, camber and height checks passed");
}
