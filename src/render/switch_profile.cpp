#include "render/switch_profile.h"

#if defined(__SWITCH__)
#include "base/log.h"
#include "base/hitch.h"
#include <algorithm>
#include <cstring>

namespace gt3r::render {

void SwitchProfile::init(bool enabled) {
    enabled_ = enabled;
    render_profile::enabled = enabled;
    if (!enabled_) return;
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (extension && std::strcmp(extension, "GL_EXT_disjoint_timer_query") == 0) timers_ = true;
    }
    if (timers_) {
        gen_ = reinterpret_cast<PFNGLGENQUERIESEXTPROC>(SDL_GL_GetProcAddress("glGenQueriesEXT"));
        delete_ = reinterpret_cast<PFNGLDELETEQUERIESEXTPROC>(SDL_GL_GetProcAddress("glDeleteQueriesEXT"));
        begin_ = reinterpret_cast<PFNGLBEGINQUERYEXTPROC>(SDL_GL_GetProcAddress("glBeginQueryEXT"));
        end_ = reinterpret_cast<PFNGLENDQUERYEXTPROC>(SDL_GL_GetProcAddress("glEndQueryEXT"));
        available_ = reinterpret_cast<PFNGLGETQUERYOBJECTUIVEXTPROC>(SDL_GL_GetProcAddress("glGetQueryObjectuivEXT"));
        result_ = reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT"));
        timers_ = gen_ && delete_ && begin_ && end_ && available_ && result_;
    }
    if (timers_) {
        auto query_bits = reinterpret_cast<PFNGLGETQUERYIVEXTPROC>(SDL_GL_GetProcAddress("glGetQueryivEXT"));
        GLint bits = 0;
        if (query_bits) query_bits(GL_TIME_ELAPSED_EXT, GL_QUERY_COUNTER_BITS_EXT, &bits);
        timers_ = bits > 0;
    }
    if (timers_) for (auto& q : queries_) gen_(1, &q.id);
    LOG_INFO("Switch profile: CPU counters on, asynchronous GPU timer %s", timers_ ? "on" : "unavailable");
}

void SwitchProfile::release() {
    if (active_ >= 0) end_(GL_TIME_ELAPSED_EXT);
    for (auto& q : queries_) if (q.id) delete_(1, &q.id);
    queries_ = {};
    active_ = -1;
}

void SwitchProfile::begin() {
    if (!enabled_) return;
    frame = {};
    if (!timers_) return;
    GLint disjoint = 0;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    if (disjoint) {
        ++disjoints_;
        // A clock discontinuity invalidates outstanding results as well as this
        // window's completed samples. Keep pending objects until they are ready.
        gpu_ms_ = gpu_max_ = 0;
        gpu_samples_ = 0;
        for (auto& q : queries_) q.valid = false;
    }
    for (auto& q : queries_) if (q.pending) {
        GLuint ready = 0;
        available_(q.id, GL_QUERY_RESULT_AVAILABLE_EXT, &ready);
        if (!ready) continue;
        if (q.valid) {
            GLuint64 ns = 0;
            result_(q.id, GL_QUERY_RESULT_EXT, &ns);
            const double ms = double(ns) / 1e6;
            gpu_ms_ += ms;
            gpu_max_ = std::max(gpu_max_, ms);
            ++gpu_samples_;
        }
        q.pending = false;
    }
    for (size_t i = 0; i < queries_.size(); ++i) if (!queries_[i].pending) {
        active_ = int(i);
        queries_[i].valid = true;
        begin_(GL_TIME_ELAPSED_EXT, queries_[i].id);
        return;
    }
    ++skipped_; // GPU is still using every slot: skip, never block.
}

void SwitchProfile::end(SDL_Renderer* renderer) {
    if (!enabled_ || active_ < 0) return;
    // Include SDL's queued HUD/flare/composite draws in the measured interval.
    const Uint64 start = SDL_GetTicksNS();
    SDL_FlushRenderer(renderer);
    frame.flush_ms += double(SDL_GetTicksNS() - start) / 1e6;
    end_(GL_TIME_ELAPSED_EXT);
    queries_[size_t(active_)].pending = true;
    active_ = -1;
}

void SwitchProfile::present(double frame_ms, double update_ms, double render_ms, double present_ms, float scale) {
    if (!enabled_ || frame_ms <= 0) return;
    ++frames_;
    if (frame_ms > 17.5) ++late_;
    elapsed_ += frame_ms;
    collect_ms_ += hitch::collect_ms, lamps_ms_ += hitch::lamps_ms;
    update_ += update_ms, render_ += render_ms, present_ += present_ms;
    max_frame_ = std::max(max_frame_, frame_ms);
    details_.add(render_profile::frame);
    totals_.passes += frame.passes, totals_.draws += frame.draws, totals_.blended += frame.blended;
    totals_.triangles += frame.triangles, totals_.uploads += frame.uploads;
    totals_.upload_bytes += frame.upload_bytes, totals_.uniform_binds += frame.uniform_binds;
    totals_.prepare_ms += frame.prepare_ms, totals_.submit_ms += frame.submit_ms, totals_.flush_ms += frame.flush_ms;
    if (elapsed_ < 3000) return;
    const double n = double(frames_);
    LOG_INFO("SWPERF: %.1f fps, frame %.2f ms max %.2f, >17.5ms %zu/%zu; CPU update %.2f render %.2f present %.2f; "
             "3D prepare %.2f submit %.2f, SDL flush %.2f ms; scale %.2f, pos %.0f,%.0f t %.1f",
             1000.0 * n / elapsed_, elapsed_ / n, max_frame_, late_, frames_, update_ / n, render_ / n, present_ / n,
             totals_.prepare_ms / n, totals_.submit_ms / n, totals_.flush_ms / n, double(scale),
             double(hitch::x), double(hitch::z), double(hitch::race_t));
    LOG_INFO("SWPERF: CPU collect %.2f lamps/sun %.2f ms (main view)", collect_ms_ / n, lamps_ms_ / n);
    LOG_INFO("SWPERF: per frame %.1f passes, %.0f draws (%.0f blended), %.0f triangles; "
             "UBO %.1f uploads, %.1f KiB, %.0f range binds; GPU render %s avg %.2f max %.2f ms, "
             "%zu samples, %zu skipped, %zu disjoints (GPU excludes present)",
             double(totals_.passes) / n, double(totals_.draws) / n, double(totals_.blended) / n,
             double(totals_.triangles) / n, double(totals_.uploads) / n, double(totals_.upload_bytes) / (1024 * n),
             double(totals_.uniform_binds) / n, gpu_samples_ ? "available" : "n/a",
             gpu_samples_ ? gpu_ms_ / double(gpu_samples_) : 0.0, gpu_max_, gpu_samples_, skipped_, disjoints_);
    LOG_INFO("SWDETAIL: CPU animations %.2f [palette %.2f moving %.2f], scene %.2f HUD %.2f overlays %.2f ms/frame",
             details_.animations / n, details_.palettes / n, details_.moving / n,
             details_.scene / n, details_.hud / n, details_.overlays / n);
    LOG_INFO("SWDETAIL: CPU geometry assemble %.2f upload %.2f, texture update %.2f [mips %.2f] ms/frame; "
             "textures %.1f uploads %.1f KiB %.1f mip builds; geometry %.1f uploads %.1f KiB %.1f skipped; "
             "palette %.0f pixels recoloured %.1f records skipped/frame",
             details_.assemble / n, details_.geometry / n, details_.textures / n, details_.mipmaps / n,
             double(details_.texture_updates) / n, double(details_.texture_bytes) / (1024 * n),
             double(details_.mipmap_calls) / n, double(details_.geometry_updates) / n,
             double(details_.geometry_bytes) / (1024 * n), double(details_.geometry_skipped) / n,
             double(details_.palette_pixels) / n, double(details_.palette_skipped) / n);
    LOG_INFO("SWDETAIL: texture PBO %.1f batches %.1f KiB/frame; CPU staging %.2f commands %.2f ms/frame",
             double(details_.texture_batches) / n, double(details_.texture_batch_bytes) / (1024 * n),
             details_.texture_stage / n, details_.texture_commands / n);
    LOG_INFO("SWCULL: car preparation %.3f ms/frame; bodies tested %.1f skipped %.1f/frame (shadows retained)",
             details_.car_prepare / n, double(details_.car_bodies_tested) / n, double(details_.car_bodies_skipped) / n);
    LOG_INFO("SWDETAIL: CPU lamp glows street %.3f car %.3f ms/frame (preparation only)",
             details_.street_glows / n, details_.car_glows / n);
    LOG_INFO("SWDETAIL: street visibility %.1f checks %.1f reused/frame",
             double(details_.street_visibility_queries) / n, double(details_.street_visibility_reused) / n);
    details_ = {};
    frames_ = late_ = gpu_samples_ = skipped_ = disjoints_ = 0;
    elapsed_ = update_ = render_ = present_ = max_frame_ = gpu_ms_ = gpu_max_ = 0;
    totals_ = {};
    collect_ms_ = lamps_ms_ = 0;
}

} // namespace gt3r::render
#endif
