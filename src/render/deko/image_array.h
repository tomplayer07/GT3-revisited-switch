#pragma once
#include <deko3d.h>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <limits>
#include <vector>

namespace gt3r::render::deko {
// Owns equally sized native images, normally one per fenced frame slot.
// The owner must finish GPU use before reset/destruction, and outlive the device.
class ImageArray final {
public:
    ImageArray() = default;
    ~ImageArray() { reset(); }
    ImageArray(const ImageArray&) = delete;
    ImageArray& operator=(const ImageArray&) = delete;
    void create(DkDevice device, unsigned width, unsigned height, unsigned count,
                DkImageFormat format, uint32_t flags = DkImageFlags_UsageRender) {
        if (memory_ || !device || !width || !height || !count)
            throw "Invalid native image array creation";
        DkImageLayoutMaker maker;
        dkImageLayoutMakerDefaults(&maker, device);
        maker.flags=flags; maker.format=format;
        maker.dimensions[0]=width; maker.dimensions[1]=height;
        DkImageLayout layout;
        dkImageLayoutInitialize(&layout,&maker);
        const uint64_t alignment=dkImageLayoutGetAlignment(&layout);
        const uint64_t stride=(uint64_t(dkImageLayoutGetSize(&layout))+alignment-1)/alignment*alignment;
        const uint64_t limit=std::numeric_limits<uint32_t>::max()-uint64_t(DK_MEMBLOCK_ALIGNMENT-1);
        if (!stride || stride>limit/count) throw "Native image array exceeds allocation limit";
        const uint64_t bytes=(stride*count+DK_MEMBLOCK_ALIGNMENT-1)/DK_MEMBLOCK_ALIGNMENT*DK_MEMBLOCK_ALIGNMENT;
        std::vector<DkImage> images(count);
        DkMemBlockMaker allocation;
        dkMemBlockMakerDefaults(&allocation,device,uint32_t(bytes));
        allocation.flags=DkMemBlockFlags_GpuCached|DkMemBlockFlags_Image|DkMemBlockFlags_ZeroFillInit;
        auto memory=dkMemBlockCreate(&allocation);
        if (!memory) throw "Native image array allocation failed";
        for (unsigned slot=0;slot<count;++slot)
            dkImageInitialize(&images[slot],&layout,memory,uint32_t(slot*stride));
        memory_=memory; images_=std::move(images); bytes_=bytes; width_=width; height_=height;
    }
    void reset() {
        if (memory_) dkMemBlockDestroy(memory_);
        memory_=nullptr; images_.clear(); bytes_=0; width_=height_=0;
    }
    DkImage* image(unsigned slot) {
        if (slot>=images_.size()) throw "Native image array slot out of range";
        return &images_[slot];
    }
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    size_t count() const { return images_.size(); }
    uint64_t bytes() const { return bytes_; }
private:
    DkMemBlock memory_{};
    std::vector<DkImage> images_;
    uint64_t bytes_=0;
    unsigned width_=0,height_=0;
};
} // namespace gt3r::render::deko
