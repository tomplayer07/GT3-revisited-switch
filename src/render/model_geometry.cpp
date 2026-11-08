#include "render/model_geometry.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace gt3r::render {
static bool primitives_compatible(const scene::Primitive& a, const scene::Primitive& b) {
    if (a.texture != b.texture) return false;
    if (a.flags != b.flags) return false;
    if (a.alpha_ref != b.alpha_ref) return false;
    if (a.env_rows != b.env_rows) return false;
    if (a.env_kind != b.env_kind) return false;
    if (a.env_point != b.env_point) return false;
    if (a.material.valid != b.material.valid) return false;
    if (a.material.valid) {
        if (a.material.shininess != b.material.shininess ||
            a.material.scale != b.material.scale ||
            a.material.specular_local != b.material.specular_local) return false;
        for (int i = 0; i < 3; ++i) {
            if (a.material.ambient[i] != b.material.ambient[i] ||
                a.material.diffuse[i] != b.material.diffuse[i] ||
                a.material.specular[i] != b.material.specular[i] ||
                a.material.emission[i] != b.material.emission[i] ||
                a.material.lamp_emission[i] != b.material.lamp_emission[i]) return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (a.env_c0[i] != b.env_c0[i] || a.env_c1[i] != b.env_c1[i] ||
            a.env_point_pos[i] != b.env_point_pos[i]) return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (a.env_alpha[i] != b.env_alpha[i]) return false;
    }
    return true;
}

bool same_model_geometry_layout(const std::vector<scene::Mesh>& a,const std::vector<scene::Mesh>& b) {
    if(a.size()!=b.size()) return false;
    for(size_t m=0;m<a.size();++m) {
        const auto& x=a[m]; const auto& y=b[m];
        if(x.lod!=y.lod || x.vertices.size()!=y.vertices.size() || x.indices!=y.indices || x.primitives.size()!=y.primitives.size()) return false;
        for(size_t p=0;p<x.primitives.size();++p)
            if(x.primitives[p].first_index!=y.primitives[p].first_index || x.primitives[p].index_count!=y.primitives[p].index_count ||
               !primitives_compatible(x.primitives[p],y.primitives[p])) return false;
    }
    return true;
}

size_t describe_model_geometry(const std::vector<scene::Mesh>& meshes,size_t texture_count,
        std::vector<ModelMeshInfo>& out) {
    out.resize(meshes.size());
    size_t vertices=0,indices=0;
    for(size_t mi=0;mi<meshes.size();++mi) {
        const auto& m=meshes[mi]; auto& info=out[mi];
        info.transform=Mat4::from(m.transform); info.lod=m.lod;
        info.vertex_offset=s32(vertices); info.index_offset=u32(indices);
        info.primitives.clear(); info.primitive_spheres.clear();
        info.center={}; info.radius=0;
        if(!m.vertices.empty()) {
            float lo[3],hi[3];
            for(unsigned k=0;k<3;++k) lo[k]=hi[k]=m.vertices[0].pos[k];
            for(const auto& v:m.vertices) for(unsigned k=0;k<3;++k) {
                lo[k]=std::min(lo[k],v.pos[k]); hi[k]=std::max(hi[k],v.pos[k]);
            }
            info.center={(lo[0]+hi[0])*0.5f,(lo[1]+hi[1])*0.5f,(lo[2]+hi[2])*0.5f};
            float r2=0;
            for(const auto& v:m.vertices) {
                const Vec3 d=Vec3{v.pos[0],v.pos[1],v.pos[2]}-info.center;
                r2=std::max(r2,dot(d,d));
            }
            info.radius=std::sqrt(r2);
        }
        for(auto p:m.primitives) {
            const u32 end=u32(std::min<uint64_t>(uint64_t(p.first_index)+p.index_count,m.indices.size()));
            const u32 tri_end=p.first_index+(end>p.first_index ? (end-p.first_index)/3*3 : 0);
            u32 count=0;
            for(u32 i=p.first_index;i<tri_end;i+=3)
                if(m.indices[i]<m.vertices.size() && m.indices[i+1]<m.vertices.size() && m.indices[i+2]<m.vertices.size()) count+=3;
            if(!count) continue;
            p.first_index=u32(indices)-info.index_offset; p.index_count=count;
            if(p.texture>=int(texture_count)) p.texture=-1;
            info.primitives.push_back(p); indices+=count;
        }
        info.line_first=u32(indices)-info.index_offset; info.line_count=0;
        vertices+=m.vertices.size();
    }
    return vertices;
}

// Meshes -> one vertex and one index list, per-mesh draw info; `lines`: wireframe edge lists.
void assemble_model_geometry(const std::vector<scene::Mesh>& meshes, size_t texture_count, bool lines,
              std::vector<scene::Vertex>& vertices, std::vector<u32>& indices, std::vector<ModelMeshInfo>& out, bool merge, bool primitive_bounds) {
    for (const scene::Mesh& m : meshes) {
        ModelMeshInfo info;
        info.transform = Mat4::from(m.transform);
        info.lod = m.lod;
        info.vertex_offset = s32(vertices.size());
        info.index_offset = u32(indices.size());
        vertices.insert(vertices.end(), m.vertices.begin(), m.vertices.end());
        if (!m.vertices.empty()) {
            float lo[3], hi[3];
            for (int k = 0; k < 3; ++k) lo[k] = hi[k] = m.vertices[0].pos[k];
            for (const scene::Vertex& v : m.vertices)
                for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], v.pos[k]), hi[k] = std::max(hi[k], v.pos[k]);
            info.center = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
            float r2 = 0;
            for (const scene::Vertex& v : m.vertices) {
                Vec3 d = Vec3{v.pos[0], v.pos[1], v.pos[2]} - info.center;
                r2 = std::max(r2, dot(d, d));
            }
            info.radius = std::sqrt(r2);
        }
        u32 nv = u32(m.vertices.size());
        // Drop out-of-range indices instead of letting the GPU read garbage.
        struct ExtractedPrim {
            scene::Primitive prim;
            std::vector<u32> indices;
        };
        std::vector<ExtractedPrim> extracted;
        extracted.reserve(m.primitives.size());

        for (const scene::Primitive& p : m.primitives) {
            ExtractedPrim ep;
            ep.prim = p;
            u32 end = u32(std::min<uint64_t>(uint64_t(p.first_index) + p.index_count, m.indices.size()));
            u32 tri_end = p.first_index + (end > p.first_index ? (end - p.first_index) / 3 * 3 : 0);
            for (u32 i = p.first_index; i < tri_end; i += 3) {
                u32 a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
                if (a >= nv || b >= nv || c >= nv) continue;
                ep.indices.insert(ep.indices.end(), {a, b, c});
            }
            ep.prim.index_count = u32(ep.indices.size());
            if (ep.prim.texture >= int(texture_count)) ep.prim.texture = -1;
            if (ep.prim.index_count > 0) {
                extracted.push_back(std::move(ep));
            }
        }

        constexpr u32 kNonOpaqueMask = scene::kAlphaBlend | scene::kAdditive |
                                      scene::kMultiplyDest | scene::kAlphaOnly |
                                      scene::kDestAlphaAdd | scene::kAlphaFailBlend |
                                      scene::kNoDepthWrite | scene::kDepthOnly | scene::kDestAlphaTest;

        std::vector<ExtractedPrim> merged;
        merged.reserve(extracted.size());

        for (auto& ep : extracted) {
            if (!merge) { merged.push_back(std::move(ep)); continue; }
            bool is_opaque = (ep.prim.flags & kNonOpaqueMask) == 0;
            if (is_opaque) {
                bool combined = false;
                for (auto& existing : merged) {
                    bool existing_opaque = (existing.prim.flags & kNonOpaqueMask) == 0;
                    if (existing_opaque && primitives_compatible(existing.prim, ep.prim)) {
                        existing.indices.insert(existing.indices.end(), ep.indices.begin(), ep.indices.end());
                        existing.prim.index_count += ep.prim.index_count;
                        combined = true;
                        break;
                    }
                }
                if (!combined) {
                    merged.push_back(std::move(ep));
                }
            } else {
                if (!merged.empty() && primitives_compatible(merged.back().prim, ep.prim)) {
                    merged.back().indices.insert(merged.back().indices.end(), ep.indices.begin(), ep.indices.end());
                    merged.back().prim.index_count += ep.prim.index_count;
                } else {
                    merged.push_back(std::move(ep));
                }
            }
        }

        for (auto& mp : merged) {
            mp.prim.first_index = u32(indices.size()) - info.index_offset;
            indices.insert(indices.end(), mp.indices.begin(), mp.indices.end());
            info.primitives.push_back(mp.prim);
            if (!primitive_bounds) continue;
            const auto& q=mp.prim;
            const u32* idx=indices.data()+info.index_offset+q.first_index;
            float lo[3],hi[3];
            for (int k=0;k<3;++k) lo[k]=hi[k]=m.vertices[idx[0]].pos[k];
            for (u32 i=0;i<q.index_count;++i) for (int k=0;k<3;++k) {
                lo[k]=std::min(lo[k],m.vertices[idx[i]].pos[k]); hi[k]=std::max(hi[k],m.vertices[idx[i]].pos[k]);
            }
            ModelMeshInfo::Sphere sphere;
            sphere.center={(lo[0]+hi[0])*0.5f,(lo[1]+hi[1])*0.5f,(lo[2]+hi[2])*0.5f};
            float r2=0;
            for (u32 i=0;i<q.index_count;++i) {
                const auto* v=m.vertices[idx[i]].pos; const Vec3 d=Vec3{v[0],v[1],v[2]}-sphere.center;
                r2=std::max(r2,dot(d,d));
            }
            sphere.radius=std::sqrt(r2); info.primitive_spheres.push_back(sphere);
        }
        // Unique edges for the wireframe view.
        info.line_first = u32(indices.size()) - info.index_offset;
        if (lines) {
            std::unordered_set<u64> edges;
            size_t tri_count = indices.size() - info.index_offset;
            for (size_t i = info.index_offset; i < info.index_offset + tri_count; i += 3)
                for (int e = 0; e < 3; ++e) {
                    u32 a = indices[i + e], b = indices[i + (e + 1) % 3];
                    if (a > b) std::swap(a, b);
                    if (edges.insert((u64(a) << 32) | b).second) indices.insert(indices.end(), {a, b});
                }
        }
        info.line_count = u32(indices.size()) - info.index_offset - info.line_first;
        out.push_back(std::move(info));
    }
}


} // namespace gt3r::render
