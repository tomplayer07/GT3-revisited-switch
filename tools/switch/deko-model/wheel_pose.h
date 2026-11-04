#pragma once
#include "render/math3d.h"
#include "sim/sim_types.h"
namespace gt3r::deko_probe {
// Input vertices already have their original wheel placement baked in.
inline render::Mat4 wheel_delta(unsigned index,render::Vec3 bindCenter,const sim::WheelState& state,
                               const sim::VehicleSetup::Wheel& setup,bool spin) {
    auto current=bindCenter;
    current.y=setup.rest_center.y+(state.suspension-setup.static_suspension);
    const float camber=(index&1) ? -state.camber : state.camber;
    auto result=render::translate(current)*render::rotate_y(-state.steer_angle)*render::rotate_z(camber);
    if (spin) result=result*render::rotate_x(-std::remainder(state.spin_angle,6.283185307f));
    return result*render::translate(-bindCenter);
}
inline float car_body_lift(const sim::VehicleSetup& setup) {
    const float height=setup.wheels[0].ride_height;
    return height>0 ? height : 0.13f;
}
}
