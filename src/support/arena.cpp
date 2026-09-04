#include "support/arena.h"

#include <algorithm>
#include <cstdlib>

namespace lucb {

Arena::Arena(size_t chunk_size) : chunk_size_(std::max(chunk_size, size_t{256})) {}

void* Arena::allocate(size_t bytes, size_t alignment) {
    if (bytes == 0) {
        bytes = 1;
    }
    if (alignment == 0) {
        alignment = 1;
    }

    auto try_chunk = [&](Chunk& chunk) -> void* {
        size_t aligned = (chunk.used + alignment - 1) & ~(alignment - 1);
        if (aligned + bytes > chunk.data.size()) {
            return nullptr;
        }
        chunk.used = aligned + bytes;
        bytes_allocated_ += bytes;
        return chunk.data.data() + aligned;
    };

    if (!chunks_.empty()) {
        if (void* p = try_chunk(chunks_.back())) {
            return p;
        }
    }

    size_t cap = std::max(chunk_size_, bytes + alignment);
    Chunk chunk;
    chunk.data.resize(cap);
    chunk.used = 0;
    chunks_.push_back(std::move(chunk));
    void* p = try_chunk(chunks_.back());
    if (p == nullptr) {
        std::abort();
    }
    return p;
}

} // namespace lucb
