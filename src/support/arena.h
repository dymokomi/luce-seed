// Chunked bump allocator. Pointers stay valid for the arena's life.
// Allocated objects are not destroyed.

#pragma once

#include "support/common.h"

namespace lucb {

struct Arena {
    Arena();
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* alloc(size_t bytes, size_t align = 8);

    template <typename T>
    T* make() {
        T* p = static_cast<T*>(alloc(sizeof(T), alignof(T)));
        *p = T{};
        return p;
    }

    size_t bytes_allocated() const { return used_; }

private:
    struct Chunk {
        vector<uint8_t> data;
        size_t used = 0;
    };

    size_t chunk_size_ = 64 * 1024;
    size_t used_ = 0;
    vector<Chunk> chunks_;
};

} // namespace lucb
