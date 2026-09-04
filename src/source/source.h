// Source text: UTF-8 bytes, positions, and the encoding gate of base.md §3.1.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lucb {

class DiagnosticBag;

struct Span {
    uint32_t start = 0;  // byte offset into original bytes
    uint32_t end = 0;
    uint32_t line = 1;   // 1-based
    uint32_t column = 1; // 1-based, BOM not counted
};

class Source {
public:
    static constexpr size_t max_bytes = 64 * 1024 * 1024;

    // `path` is the name diagnostics print. `bytes` are the file contents.
    static Source from_bytes(std::string path, std::string bytes, DiagnosticBag& diagnostics);

    std::string_view path() const { return path_; }
    std::string_view bytes() const { return bytes_; }
    size_t size() const { return bytes_.size(); }
    bool ok() const { return ok_; }

    // First byte the lexer should read: 0, or 3 when a UTF-8 BOM was skipped.
    size_t scan_start() const { return scan_start_; }

    Span span_at(size_t byte, size_t end_byte) const;

private:
    std::string path_;
    std::string bytes_;
    size_t scan_start_ = 0;
    bool ok_ = true;
};

// Decode one UTF-8 scalar at `at`. Returns 0 width on invalid or end.
// On success `codepoint` is set and the return is the byte width (1..4).
size_t utf8_next(std::string_view bytes, size_t at, char32_t& codepoint);

bool is_bidi_control(char32_t c);

// ASCII to write instead of a confusable codepoint, or nullptr.
const char* confusable_hint(char32_t c);

} // namespace lucb
