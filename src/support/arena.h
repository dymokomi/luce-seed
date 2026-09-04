// Arena bump allocator. Chunked so pointers stay valid for the arena's life.
// Objects allocated here are not destroyed; do not put std::string in them.

#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>
#include <vector>

namespace lucb {

class Arena {
public:
    explicit Arena(size_t chunk_size = 64 * 1024);
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    ~Arena() = default;

    void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t));

    template <typename T, typename... Args>
    T* make(Args&&... args) {
        void* storage = allocate(sizeof(T), alignof(T));
        return new (storage) T(std::forward<Args>(args)...);
    }

    size_t bytes_allocated() const { return bytes_allocated_; }

private:
    struct Chunk {
        std::vector<uint8_t> data;
        size_t used = 0;
    };

    size_t chunk_size_;
    size_t bytes_allocated_ = 0;
    std::vector<Chunk> chunks_;
};

} // namespace lucb
