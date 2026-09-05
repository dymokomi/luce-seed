//==============================================================================================
//
//   source/source - Source text and positions
//
//   DESCRIPTION:
//       Loads bytes, normalises a leading BOM and CRLF, rejects NUL, invalid UTF-8, and
//       bidirectional controls (base.md §3.1), and maps byte offsets to lines and columns for
//       diagnostics.
//
//==============================================================================================

#include "source/source.h"

#include "support/diagnostics.h"

#include <sstream>

namespace lucb {
namespace {

void add_at(DiagnosticBag& diagnostics, std::string_view path, size_t byte, uint32_t line,
            uint32_t column, std::string code, std::string message) {
    Span span;
    span.start = static_cast<uint32_t>(byte);
    span.end = static_cast<uint32_t>(byte);
    span.line = line;
    span.column = column;
    diagnostics.add(std::move(code), std::string(path), span, std::move(message));
}

} // namespace

size_t utf8_next(std::string_view bytes, size_t at, char32_t& codepoint) {
    if (at >= bytes.size()) {
        return 0;
    }
    const unsigned char lead = static_cast<unsigned char>(bytes[at]);
    if (lead < 0x80) {
        codepoint = lead;
        return 1;
    }

    size_t width = 0;
    char32_t value = 0;
    if ((lead & 0xE0) == 0xC0) {
        width = 2;
        value = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        width = 3;
        value = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        width = 4;
        value = lead & 0x07;
    } else {
        return 0;
    }
    if (at + width > bytes.size()) {
        return 0;
    }
    for (size_t i = 1; i < width; i++) {
        const unsigned char cont = static_cast<unsigned char>(bytes[at + i]);
        if ((cont & 0xC0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (cont & 0x3F);
    }
    // Overlong, surrogate, or out of range.
    if (width == 2 && value < 0x80) {
        return 0;
    }
    if (width == 3 && value < 0x800) {
        return 0;
    }
    if (width == 4 && value < 0x10000) {
        return 0;
    }
    if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return 0;
    }
    codepoint = value;
    return width;
}

bool is_bidi_control(char32_t c) {
    if (c == 0x061C || c == 0x200E || c == 0x200F) {
        return true;
    }
    if (c >= 0x202A && c <= 0x202E) {
        return true;
    }
    return c >= 0x2066 && c <= 0x2069;
}

const char* confusable_hint(char32_t c) {
    // Cut down from the stage-0 table: quotes a word processor curls, dashes,
    // Unicode spaces, a few operators, fullwidth punctuation. base.md §3.1.
    if (c == 0x00A0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F ||
        c == 0x3000) {
        return "a Unicode space; write an ordinary space";
    }
    if (c == 0x00AD || c == 0x034F || (c >= 0x200B && c <= 0x200D) ||
        (c >= 0x2060 && c <= 0x2064) || c == 0xFEFF) {
        return "an invisible character; delete it";
    }
    if (c == 0x00AB || c == 0x00BB || (c >= 0x2018 && c <= 0x201F)) {
        return "a typographic quote; text is written \"like this\"";
    }
    if ((c >= 0x2010 && c <= 0x2015) || c == 0x2212) {
        return "write '-'";
    }
    if (c == 0x00D7) {
        return "write '*'";
    }
    if (c == 0x00F7 || c == 0x2215) {
        return "write '/'";
    }
    if (c == 0x2260) {
        return "write '!='";
    }
    if (c == 0x2264) {
        return "write '<='";
    }
    if (c == 0x2265) {
        return "write '>='";
    }
    if (c == 0x2192 || c == 0x21D2 || c == 0x27F6) {
        return "write '->'";
    }
    if (c == 0xFF08) {
        return "write '('";
    }
    if (c == 0xFF09) {
        return "write ')'";
    }
    if (c == 0xFF0B) {
        return "write '+'";
    }
    if (c == 0xFF0C || c == 0x3001) {
        return "write ','";
    }
    if (c == 0xFF1A) {
        return "write ':'";
    }
    if (c == 0xFF1D) {
        return "write '='";
    }
    if (c == 0x3002) {
        return "write '.'";
    }
    return nullptr;
}

namespace {

const char* named_foreign_bom(std::string_view bytes) {
    if (bytes.size() >= 4) {
        const unsigned char a = static_cast<unsigned char>(bytes[0]);
        const unsigned char b = static_cast<unsigned char>(bytes[1]);
        const unsigned char c = static_cast<unsigned char>(bytes[2]);
        const unsigned char d = static_cast<unsigned char>(bytes[3]);
        if (a == 0x00 && b == 0x00 && c == 0xFE && d == 0xFF) {
            return "UTF-32 (big-endian)";
        }
        if (a == 0xFF && b == 0xFE && c == 0x00 && d == 0x00) {
            return "UTF-32 (little-endian)";
        }
    }
    if (bytes.size() >= 2) {
        const unsigned char a = static_cast<unsigned char>(bytes[0]);
        const unsigned char b = static_cast<unsigned char>(bytes[1]);
        if (a == 0xFE && b == 0xFF) {
            return "UTF-16 (big-endian)";
        }
        if (a == 0xFF && b == 0xFE) {
            return "UTF-16 (little-endian)";
        }
    }
    return nullptr;
}

} // namespace

Source Source::from_bytes(std::string path, std::string bytes, DiagnosticBag& diagnostics) {
    Source source;
    source.path_ = std::move(path);
    source.bytes_ = std::move(bytes);
    source.ok_ = true;

    if (source.bytes_.size() > max_bytes) {
        add_at(diagnostics, source.path_, 0, 1, 1, "lucb.source.too_large",
               "source file is larger than 64 MiB");
        source.ok_ = false;
        return source;
    }

    if (const char* encoding = named_foreign_bom(source.bytes_)) {
        std::ostringstream message;
        message << encoding << "; save the file as UTF-8";
        add_at(diagnostics, source.path_, 0, 1, 1, "lucb.source.encoding", message.str());
        source.ok_ = false;
        return source;
    }

    size_t at = 0;
    if (source.bytes_.size() >= 3 && static_cast<unsigned char>(source.bytes_[0]) == 0xEF &&
        static_cast<unsigned char>(source.bytes_[1]) == 0xBB &&
        static_cast<unsigned char>(source.bytes_[2]) == 0xBF) {
        source.scan_start_ = 3;
        at = 3;
    }

    uint32_t line = 1;
    uint32_t column = 1;
    while (at < source.bytes_.size()) {
        char32_t cp = 0;
        const size_t width = utf8_next(source.bytes_, at, cp);
        if (width == 0) {
            add_at(diagnostics, source.path_, at, line, column, "lucb.source.utf8",
                   "invalid UTF-8");
            source.ok_ = false;
            return source;
        }
        if (cp == 0) {
            add_at(diagnostics, source.path_, at, line, column, "lucb.source.nul",
                   "NUL is not allowed in source");
            source.ok_ = false;
            return source;
        }
        if (cp == 0xFEFF) {
            add_at(diagnostics, source.path_, at, line, column, "lucb.source.bom",
                   "a UTF-8 BOM is allowed only at byte zero");
            source.ok_ = false;
            return source;
        }
        if (is_bidi_control(cp)) {
            add_at(diagnostics, source.path_, at, line, column, "lucb.source.bidi",
                   "bidirectional controls are not allowed in source");
            source.ok_ = false;
            return source;
        }
        if (cp == U'\t') {
            add_at(diagnostics, source.path_, at, line, column, "lucb.lex.tab",
                   "tabs are not allowed; use spaces");
            source.ok_ = false;
            return source;
        }
        if (cp == U'\r') {
            if (at + 1 >= source.bytes_.size() || source.bytes_[at + 1] != '\n') {
                add_at(diagnostics, source.path_, at, line, column, "lucb.source.cr",
                       "carriage return must be followed by newline");
                source.ok_ = false;
                return source;
            }
            at += 2;
            line += 1;
            column = 1;
            continue;
        }
        if (cp == U'\n') {
            at += 1;
            line += 1;
            column = 1;
            continue;
        }
        at += width;
        column += 1;
    }
    return source;
}

Span Source::span_at(size_t byte, size_t end_byte) const {
    Span span;
    span.start = static_cast<uint32_t>(byte);
    span.end = static_cast<uint32_t>(end_byte);
    uint32_t line = 1;
    uint32_t column = 1;
    size_t at = scan_start_;
    while (at < byte && at < bytes_.size()) {
        if (bytes_[at] == '\r') {
            at += (at + 1 < bytes_.size() && bytes_[at + 1] == '\n') ? 2 : 1;
            line += 1;
            column = 1;
            continue;
        }
        if (bytes_[at] == '\n') {
            at += 1;
            line += 1;
            column = 1;
            continue;
        }
        char32_t cp = 0;
        const size_t width = utf8_next(bytes_, at, cp);
        at += width == 0 ? 1 : width;
        column += 1;
    }
    span.line = line;
    span.column = column;
    return span;
}

} // namespace lucb
