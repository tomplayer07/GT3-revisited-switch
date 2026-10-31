#include "race/clut_animations.h"
#include "base/render_profile.h"

namespace gt3r::race {

void ClutAnimations::add(render::GpuModel* model, std::vector<gtm1::ClutSet>&& sets) {
    if (model && !sets.empty()) models_.push_back({model, std::move(sets)});
}

void ClutAnimations::restart(float countdown) {
    for (Model& m : models_)
        for (gtm1::ClutSet& s : m.sets) {
            s.textures.player().reset();
            s.textures.player().start_signal(countdown);
        }
}

void ClutAnimations::animate(float seconds) {
    for (Model& m : models_)
        for (gtm1::ClutSet& s : m.sets) s.textures.player().update(seconds);
}

void ClutAnimations::upload() {
    for (Model& m : models_) {
        std::vector<render::GpuModel::TextureUpdate> updates;
        {
            render_profile::Scope measure(render_profile::frame.palettes);
            for (gtm1::ClutSet& s : m.sets)
                for (u32 r : s.textures.refresh()) updates.push_back({size_t(s.first) + r, s.textures.rgba(r)});
        }
        if (!updates.empty()) m.gpu->update_textures(updates);
    }
}

} // namespace gt3r::race
