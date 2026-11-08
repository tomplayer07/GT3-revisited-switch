#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace gt3r::render::deko {
// Checked packing for differently sized images/staging slices in one allocation.
class AllocationSlices {
    uint64_t size_ = 0;
    uint64_t limit_;
public:
    explicit AllocationSlices(uint64_t limit=std::numeric_limits<uint32_t>::max()-4095u):limit_(limit) {}
    uint32_t reserve(uint64_t bytes,uint64_t alignment) {
        if (!alignment || size_>limit_ || bytes>limit_) throw std::overflow_error("Invalid native allocation slice");
        const uint64_t padding=(alignment-size_%alignment)%alignment;
        if (padding>limit_-size_ || bytes>limit_-size_-padding)
            throw std::overflow_error("Native allocation slices exceed limit");
        const uint64_t offset=size_+padding;
        size_=offset+bytes;
        return uint32_t(offset);
    }
    uint32_t size() const { return uint32_t(size_); }
};
struct ModelStaticLayout { uint32_t vertex,index,descriptors,bytes; };
inline ModelStaticLayout model_static_layout(uint32_t vertex_bytes,uint32_t index_bytes,uint32_t descriptor_bytes) {
    if(!vertex_bytes || !index_bytes || !descriptor_bytes) throw std::invalid_argument("Empty native model static slice");
    AllocationSlices storage;
    const auto vertex=storage.reserve(vertex_bytes,4096),index=storage.reserve(index_bytes,4096),descriptors=storage.reserve(descriptor_bytes,4096);
    return {vertex,index,descriptors,storage.size()};
}
// Initial uploads contain only texture copies and fixed setup/barrier commands.
// Reserve conservatively per image instead of clearing 4 MiB for every wheel,
// brake part and shadow model. Legacy probe readback retains its existing pool.
inline uint32_t model_upload_command_bytes(uint32_t images,bool frontend) {
    if(!frontend) return 4*1024*1024;
    // Full course scenes can contain over 1300 distinct texture entries.
    // Include one fallback image in addition to the scene texture table.
    if(images>4097) throw std::invalid_argument("Invalid native upload image count");
    return (64*1024+images*512+4095)&~4095u;
}
struct ModelBufferBudget { uint32_t commands, draws, slots; };
inline ModelBufferBudget model_buffer_budget(uint32_t primitives,bool frontend) {
    // Some full-size courses exceed 8K scenery primitives (Seattle has 8615).
    // Keep the smaller race/demo model cap, but let frontend scenes reserve up
    // to 16K per model. The frame arena owns these per-view command/uniform slices.
    constexpr uint32_t max_commands=4*1024*1024, max_race_draws=8192, max_frontend_draws=16384;
    if (!primitives || primitives>(frontend ? max_frontend_draws : max_race_draws))
        throw std::invalid_argument("Invalid native primitive budget");
    if (!frontend) return {max_commands,max_race_draws,3};
    const uint32_t commands=std::min(max_commands,64*1024+primitives*1024);
    return {commands,primitives,2};
}
} // namespace gt3r::render::deko
