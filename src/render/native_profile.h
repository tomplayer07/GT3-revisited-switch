#pragma once
#include <cstdint>
#include <optional>
#include <cmath>
namespace gt3r::render {
enum class ProfilePass : uint8_t { Other, Environment, WetRoad, WetBlur, Main, Mirror, Post, Count };
enum class GpuProfileRange : uint8_t { Environment, WetRoad, WetBlur, MainScenery, MainCarsEffects, Main, Mirror, Post, Count };
enum class ProfileRole : uint8_t { Scene, Car, Effect, Count };
struct RaceProfileInfo { float x=0,z=0,speed=0; int opponents=0; bool shadows_disabled=false; };
struct RaceProfileCpu { double total=0,simulation=0,environment=0,collection=0,draw=0,lamps=0,animations=0,wet=0,tail=0,palettes=0,moving=0,texture_updates=0,geometry_updates=0,metadata=0; };
// Present-to-present cadence is meaningful only across consecutive live race
// frames. Loading, previews, pause and menu transitions never bridge a sample.
class RaceCadenceGate {
    bool live_=false; uint64_t previous_=0;
public:
    std::optional<double> present(bool live,uint64_t now_ns) {
        std::optional<double> out;
        if(live && live_ && now_ns>previous_) out=double(now_ns-previous_)/1e6;
        live_=live; previous_=now_ns; return out;
    }
};
inline bool valid_gpu_interval(uint64_t begin,uint64_t end) { return begin && end>=begin; }
} // namespace gt3r::render
