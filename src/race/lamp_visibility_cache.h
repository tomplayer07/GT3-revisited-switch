#pragma once

#include "render/math3d.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gt3r::race {

// Only the occlusion result is reused. Lamp position, projection and colour are
// recalculated every frame. Two entries per lamp keep glow and halo independent.
class LampVisibilityCache {
public:
    void reset() { entries_.clear(); have_view_ = false; }

    void begin(size_t lamps, int interval, const render::Vec3& eye,
               const render::Mat4& view, const render::Frustum& fr, double time) {
        interval_ = std::clamp(interval, 1, 2);
        ++frame_;
        queries = reused = 0;
        const render::Vec3 delta = eye - eye_;
        bool cut = !have_view_ || entries_.size() != lamps * 2 ||
                   render::dot(delta, delta) > 16.0f || time < time_ || time - time_ > 0.1 ||
                   fr.left != fr_.left || fr.right != fr_.right || fr.up != fr_.up || fr.down != fr_.down;
        // Compare all camera axes, including roll. About 10 degrees in one frame.
        for (int axis = 0; axis < 3 && !cut; ++axis) {
            float alignment = 0;
            for (int c = 0; c < 3; ++c) alignment += view.at(axis, c) * view_.at(axis, c);
            if (!(alignment > 0.9848f)) cut = true;
        }
        entries_.resize(lamps * 2);
        if (cut) for (auto& e : entries_) e.valid = false;
        eye_ = eye; view_ = view; fr_ = fr; time_ = time; have_view_ = true;
    }

    template<class Compute>
    float sample(size_t lamp, size_t channel, Compute compute) {
        auto& e = entries_.at(lamp * 2 + channel);
        // Lamp ID controls the phase, never its position in the visible list.
        // Newly visible/stale entries are evaluated immediately.
        const bool due = interval_ == 1 || !e.valid || frame_ - e.frame >= uint64_t(interval_) ||
                         (frame_ % uint64_t(interval_) == lamp % size_t(interval_) && e.frame != frame_);
        if (due) {
            e.value = compute(); e.frame = frame_; e.valid = true;
            ++queries;
        } else {
            ++reused;
        }
        return e.value;
    }

    size_t queries = 0, reused = 0;

private:
    struct Entry { float value = 0; uint64_t frame = 0; bool valid = false; };
    std::vector<Entry> entries_;
    uint64_t frame_ = 0;
    int interval_ = 1;
    bool have_view_ = false;
    render::Vec3 eye_;
    render::Mat4 view_;
    render::Frustum fr_;
    double time_ = 0;
};
} // namespace gt3r::race
