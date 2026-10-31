#pragma once

#include <cstddef>
#include <cstring>
#include <vector>

namespace gt3r::render {

// CPU staging for immutable, aligned uniform ranges. clear() retains the storage for the
// next view; appending must never overwrite a range referenced by an earlier draw.
class UniformStaging {
public:
    void clear() { bytes_.clear(); }
    size_t append(const void* data, size_t size, size_t alignment) {
        const size_t remainder = bytes_.size() % alignment;
        const size_t offset = bytes_.size() + (remainder ? alignment - remainder : 0);
        bytes_.resize(offset + size);
        std::memcpy(bytes_.data() + offset, data, size);
        return offset;
    }
    const std::byte* data() const { return bytes_.data(); }
    size_t size() const { return bytes_.size(); }
private:
    std::vector<std::byte> bytes_;
};

} // namespace gt3r::render
