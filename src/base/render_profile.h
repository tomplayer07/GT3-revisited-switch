#pragma once

#include <SDL3/SDL_timer.h>
#include <cstddef>

namespace gt3r::render_profile {

// Main-thread CPU timings and upload counters, collected only in diagnostic runs.
inline bool enabled = false;
struct Detail {
    double animations = 0, palettes = 0, moving = 0, assemble = 0, geometry = 0;
    double textures = 0, mipmaps = 0, hud = 0, overlays = 0, scene = 0;
    size_t texture_updates = 0, texture_bytes = 0, mipmap_calls = 0;
    size_t geometry_updates = 0, geometry_bytes = 0, geometry_skipped = 0;
    size_t palette_pixels = 0, palette_skipped = 0;
    size_t texture_batches = 0, texture_batch_bytes = 0;
    double texture_stage = 0, texture_commands = 0;
    double car_prepare = 0;
    double street_glows = 0, car_glows = 0;
    size_t street_visibility_queries = 0, street_visibility_reused = 0;
    size_t car_bodies_tested = 0, car_bodies_skipped = 0;
    void add(const Detail& d) {
        car_prepare += d.car_prepare;
        street_glows += d.street_glows; car_glows += d.car_glows;
        street_visibility_queries += d.street_visibility_queries;
        street_visibility_reused += d.street_visibility_reused;
        car_bodies_tested += d.car_bodies_tested; car_bodies_skipped += d.car_bodies_skipped;
        texture_batches += d.texture_batches; texture_batch_bytes += d.texture_batch_bytes;
        texture_stage += d.texture_stage; texture_commands += d.texture_commands;
        animations += d.animations; palettes += d.palettes; moving += d.moving;
        assemble += d.assemble; geometry += d.geometry; textures += d.textures;
        mipmaps += d.mipmaps; hud += d.hud; overlays += d.overlays; scene += d.scene;
        texture_updates += d.texture_updates; texture_bytes += d.texture_bytes; mipmap_calls += d.mipmap_calls;
        geometry_updates += d.geometry_updates; geometry_bytes += d.geometry_bytes; geometry_skipped += d.geometry_skipped;
        palette_pixels += d.palette_pixels; palette_skipped += d.palette_skipped;
    }
};
inline Detail frame;
class Scope {
public:
    explicit Scope(double& total) : total_(total), start_(enabled ? SDL_GetTicksNS() : 0) {}
    ~Scope() { if (start_) total_ += double(SDL_GetTicksNS() - start_) / 1e6; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    double& total_;
    Uint64 start_;
};

} // namespace gt3r::render_profile
