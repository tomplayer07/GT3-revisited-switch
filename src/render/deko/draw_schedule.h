#pragma once
#include "render/view3d.h"
#include <numeric>

namespace gt3r::render::deko {
// Phases are independent submissions sharing one depth/colour target. Joined
// scenery nodes finish their solid phase before any member's blended phase.
struct ScheduledDraw { size_t item; unsigned phase; bool reset_depth=false; };
inline std::vector<ScheduledDraw> draw_schedule(const std::vector<DrawItem>& items,const Vec3& eye,bool sort_blended) {
    std::vector<ScheduledDraw> out;
    for(size_t i=0;i<items.size();++i) if(items[i].background) out.push_back({i,0,items[i].depth_reset});
    std::vector<size_t> blended;
    for(size_t i=0;i<items.size();) {
        if(items[i].background || items[i].late) { ++i; continue; }
        if(!items[i].in_order) { out.push_back({i,1,false}); blended.push_back(i++); continue; }
        size_t end=i+1;
        while(end<items.size() && items[end].in_order && items[end].in_order_join && !items[end].background && !items[end].late) ++end;
        for(size_t k=i;k<end;++k) out.push_back({k,1,false});
        for(size_t k=i;k<end;++k) out.push_back({k,2,k+1==end && items[end-1].depth_reset});
        i=end;
    }
    if(sort_blended) std::stable_sort(blended.begin(),blended.end(),[&](size_t a,size_t b) {
        auto distance=[&](size_t i) {
            const auto p=items[i].transform.transform_point({0,0,0})-eye;
            return dot(p,p);
        };
        return distance(a)>distance(b);
    });
    for(size_t i:blended) out.push_back({i,2,items[i].depth_reset});
    for(size_t i=0;i<items.size();++i) if(items[i].late && !items[i].background) out.push_back({i,0,items[i].depth_reset});
    return out;
}
} // namespace gt3r::render::deko
