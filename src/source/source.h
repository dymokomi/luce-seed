//==============================================================================================
//
//   source/source - Source text
//
//   DESCRIPTION:
//       A `Source` owns the bytes of one file and answers position queries.
//
//==============================================================================================

#pragma once

#include "support/common.h"

namespace lucb {

struct DiagnosticBag;

struct Span {
    uint32_t start = 0; // byte offset into original bytes
    uint32_t end = 0;
    uint32_t line = 1;   // 1-based
    uint32_t column = 1; // 1-based, BOM not counted
};
// A position directive (§3.3): a line holding only `#: file:line:column` says that the
// lines after it, until the next directive, were compiled from that position of another
// source; a bare `#:` restores the file's own positions (`file` empty). A trap on such a
// line reports the mapped position (§11.5).
struct Directive {
    uint32_t base_line = 0; // the first line the directive applies to
    string_view file;       // into the source bytes; empty for a bare `#:`
    uint32_t line = 0;
    uint32_t column = 0;
    // A fragment's first line in a module assembled from a directory (§16.1): the lines
    // from `base_line` on are the fragment `file`'s, one for one from its line 1, and any
    // earlier directive's reach ends here.
    bool fragment = false;
};

// One fragment of a module assembled from a directory's `ORDER` (§16.1): its file, and the
// line of the assembled text it begins at.
struct Segment {
    string path;
    uint32_t start_line = 1;
};

struct Source {
    static constexpr size_t max_bytes = 64 * 1024 * 1024;

    static Source from_bytes(string path, string bytes, DiagnosticBag& diagnostics);
    // The fragments `paths` names, read as `texts`, as one module source at `path`; each
    // fragment ends with a newline of its own, so the next begins on a fresh line.
    static Source assembled(string path, const vector<string>& paths, const vector<string>& texts,
                            DiagnosticBag& diagnostics);

    string_view path() const {
        return path_;
    }
    string_view bytes() const {
        return bytes_;
    }
    size_t size() const {
        return bytes_.size();
    }
    bool ok() const {
        return ok_;
    }
    size_t scan_start() const {
        return scan_start_;
    }

    Span span_at(size_t byte, size_t end_byte) const;
    const vector<Directive>& directives() const {
        return directives_;
    }
    // The fragments of an assembled module, in order; empty for one file.
    const vector<Segment>& segments() const {
        return segments_;
    }
    // Which fragment a line of an assembled module belongs to; 0 for one file.
    size_t segment_of(uint32_t line) const;

  private:
    string path_;
    string bytes_;
    vector<Directive> directives_;
    vector<Segment> segments_;
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
