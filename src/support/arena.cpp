//==============================================================================================
//
//   support/arena - Chunked bump allocator
//
//   DESCRIPTION:
//       Objects that live for the whole compilation come from here and are never freed
//       individually.
//
//==============================================================================================

#include "support/arena.h"

#include <cstdlib>

namespace lucb {

Arena::Arena() {}

void* Arena::alloc(size_t bytes, size_t align) {
    if (bytes == 0) {
        bytes = 1;
    }
    if (align == 0) {
        align = 1;
    }

    if (!chunks_.empty()) {
        Chunk& chunk = chunks_.back();
        size_t aligned = (chunk.used + align - 1) & ~(align - 1);
        if (aligned + bytes <= chunk.data.size()) {
            chunk.used = aligned + bytes;
            used_ += bytes;
            return chunk.data.data() + aligned;
        }
    }

    size_t cap = chunk_size_;
    if (cap < bytes + align) {
        cap = bytes + align;
    }
    Chunk chunk;
    chunk.data.resize(cap);
    chunk.used = 0;
    chunks_.push_back(chunk);

    Chunk& fresh = chunks_.back();
    size_t aligned = (fresh.used + align - 1) & ~(align - 1);
    if (aligned + bytes > fresh.data.size()) {
        std::abort();
    }
    fresh.used = aligned + bytes;
    used_ += bytes;
    return fresh.data.data() + aligned;
}

} // namespace lucb
