// Source text: UTF-8 bytes, positions, and the encoding gate of base.md §3.1.

#pragma once

#include "support/common.h"

namespace lucb {

struct DiagnosticBag;

struct Span {
    uint32_t start = 0;  // byte offset into original bytes
    uint32_t end = 0;
    uint32_t line = 1;   // 1-based
    uint32_t column = 1; // 1-based, BOM not counted
};

struct Source {
    static constexpr size_t max_bytes = 64 * 1024 * 1024;

    static Source from_bytes(string path, string bytes, DiagnosticBag& diagnostics);

    string_view path() const { return path_; }
    string_view bytes() const { return bytes_; }
    size_t size() const { return bytes_.size(); }
    bool ok() const { return ok_; }
    size_t scan_start() const { return scan_start_; }

    Span span_at(size_t byte, size_t end_byte) const;

private:
    string path_;
    string bytes_;
    size_t scan_start_ = 0;
    bool ok_ = true;
};

// Decode one UTF-8 scalar at `at`. Returns 0 on invalid or end.
// On success `codepoint` is set and the return is the byte width (1..4).
size_t utf8_next(string_view bytes, size_t at, char32_t& codepoint);

bool is_bidi_control(char32_t c);

// ASCII to write instead of a confusable codepoint, or nullptr.
const char* confusable_hint(char32_t c);

} // namespace lucb
