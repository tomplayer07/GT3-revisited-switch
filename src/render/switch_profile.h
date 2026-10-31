#pragma once

#if defined(__SWITCH__)
#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include "base/render_profile.h"
#include <array>
#include <cstddef>

namespace gt3r::render {

// One optional timer around all rendering (2D and 3D), excluding presentation.
// Results are polled in later frames; profiling never waits for the GPU.
class SwitchProfile {
public:
    void init(bool enabled);
    void release();
    void begin();
    void end(SDL_Renderer* renderer);
    void present(double frame_ms, double update_ms, double render_ms, double present_ms, float scale);
    bool enabled() const { return enabled_; }
    struct Counters {
        size_t passes = 0, draws = 0, blended = 0, triangles = 0;
        size_t uploads = 0, upload_bytes = 0, uniform_binds = 0;
        double prepare_ms = 0, submit_ms = 0, flush_ms = 0;
    } frame;
private:
    bool enabled_ = false, timers_ = false;
    struct Query { GLuint id = 0; bool pending = false, valid = false; };
    std::array<Query, 8> queries_{};
    int active_ = -1;
    PFNGLGENQUERIESEXTPROC gen_ = nullptr;
    PFNGLDELETEQUERIESEXTPROC delete_ = nullptr;
    PFNGLBEGINQUERYEXTPROC begin_ = nullptr;
    PFNGLENDQUERYEXTPROC end_ = nullptr;
    PFNGLGETQUERYOBJECTUIVEXTPROC available_ = nullptr;
    PFNGLGETQUERYOBJECTUI64VEXTPROC result_ = nullptr;
    Counters totals_;
    render_profile::Detail details_;
    double collect_ms_ = 0, lamps_ms_ = 0;
    size_t frames_ = 0, late_ = 0, gpu_samples_ = 0, skipped_ = 0, disjoints_ = 0;
    double elapsed_ = 0, update_ = 0, render_ = 0, present_ = 0, max_frame_ = 0, gpu_ms_ = 0, gpu_max_ = 0;
};

} // namespace gt3r::render
#endif
