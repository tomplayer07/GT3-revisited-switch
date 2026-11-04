#pragma once
#include "formats/course/course_model.h"
#include "base/bytes.h"

namespace gt3r::deko_probe {
// CPU-only bridge: preserve original texture ordering and separate the nodes
// controlled by the course programs from the static geometry.
struct CourseMotion {
    struct Group {
        gtm1::AnimatedNodes animator;
        size_t first=0;
        std::vector<scene::Mesh> initial;
    };
    scene::Model scene;
    std::vector<uint8_t> roles;
    std::vector<gtm1::ClutSet> cluts;
    std::vector<Group> groups;

    explicit CourseMotion(ByteSpan bytes) {
        for (unsigned role : {0u,2u}) {
            course::CourseModelOptions options;
            options.course_line=false; options.scenery=role==0; options.backdrop=role==2;
            std::vector<gtm1::AnimatedNodes> nodes;
            std::vector<gtm1::ClutSet> sets;
            auto part=course::build_course_model(bytes,options,"route5_motion",&sets,&nodes);
            const int base=int(scene.textures.size());
            for (auto& set:sets) { set.first+=base; cluts.push_back(std::move(set)); }
            roles.insert(roles.end(),part.meshes.size(),uint8_t(role));
            course::append_model(scene,std::move(part));
            for (auto& animator:nodes) {
                animator.rebase_textures(base);
                auto initial=animator.meshes();
                const auto first=scene.meshes.size();
                for (const auto& mesh:initial) scene.meshes.push_back(mesh);
                roles.insert(roles.end(),initial.size(),uint8_t(role));
                groups.push_back({std::move(animator),first,std::move(initial)});
            }
        }
    }
    bool advance(float seconds) {
        bool changed=false;
        for (auto& group:groups) changed=group.animator.update(seconds) || changed;
        return changed;
    }
    void reset() { for (auto& group:groups) group.animator.reset(); }
    template<class Update> void emit(Update&& update) const {
        for (const auto& group:groups) {
            const auto current=group.animator.meshes();
            if (current.size()!=group.initial.size()) throw FormatError("Moving course mesh count changed");
            for (size_t i=0;i<current.size();++i) {
                const auto& a=group.initial[i]; const auto& b=current[i];
                if (a.name!=b.name || a.vertices.size()!=b.vertices.size() || a.indices!=b.indices || a.primitives.size()!=b.primitives.size())
                    throw FormatError("Moving course topology changed");
                for (size_t p=0;p<a.primitives.size();++p) {
                    const auto& x=a.primitives[p]; const auto& y=b.primitives[p];
                    if (x.first_index!=y.first_index || x.index_count!=y.index_count || x.texture!=y.texture || x.flags!=y.flags || x.alpha_ref!=y.alpha_ref)
                        throw FormatError("Moving course draw state changed");
                }
                update(group.first+i,b);
            }
        }
    }
};
}
