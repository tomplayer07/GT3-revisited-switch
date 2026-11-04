#pragma once
#include "sim/sim_types.h"
#include <memory>
#include <span>
#include <cstdint>
namespace gt3r::deko_probe {
// Minimal real physics session. Private user-disc bundle, no renderer dependency.
class Session {
public:
    explicit Session(std::span<const uint8_t> bundle, unsigned cars = 1);
    ~Session();
    void tick(const sim::ControlInputs& input);
    unsigned car_count() const;
    const sim::VehicleState& state(unsigned car = 0) const;
    const sim::VehicleSetup& setup(unsigned car = 0) const;
    uint64_t ticks() const;
    int lap_counter() const;
    int lamp_hint(unsigned car = 0) const;
    float brake_factor(unsigned car = 0) const;
    bool brake_now(unsigned car = 0) const;
    bool finished() const;
    std::span<const uint8_t> course_bytes() const;
    std::span<const uint8_t> car_bytes() const;
    std::span<const uint8_t> flare_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
