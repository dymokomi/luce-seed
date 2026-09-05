//==============================================================================================
//
//   support/literal - Literal decoding
//
//   DESCRIPTION:
//       Integer spellings with radix and separators, float spellings, character escapes, and
//       string escapes, decoded once for the checker, oracle, and emitter (base.md §4).
//
//==============================================================================================

#include "support/literal.h"

#include <cstdlib>

namespace lucb {
namespace {

string_view strip_suffix_alpha(string_view text, size_t from) {
    if (from >= text.size()) {
        return {};
    }
    return text.substr(from);
}

} // namespace

ParsedInt parse_int_literal(string_view text) {
    ParsedInt out;
    if (text.empty()) {
        return out;
    }
    int base = 10;
    size_t i = 0;
    if (text.size() >= 2 && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'o' || text[1] == 'b')) {
        if (text[1] == 'x') {
            base = 16;
        } else if (text[1] == 'o') {
            base = 8;
        } else {
            base = 2;
        }
        i = 2;
    }

    uint64_t value = 0;
    bool any = false;
    for (; i < text.size(); i++) {
        char c = text[i];
        if (c == '_') {
            continue;
        }
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'z') {
            break;
        } else {
            return out;
        }
        if (digit < 0 || digit >= base) {
            return out;
        }
        any = true;
        if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) {
            return out;
        }
        value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
    }
    if (!any) {
        return out;
    }
    out.ok = true;
    out.value = value;
    out.suffix = strip_suffix_alpha(text, i);
    return out;
}

bool parse_i64_literal(string_view text, int64_t* out) {
    ParsedInt p = parse_int_literal(text);
    if (!p.ok || out == nullptr) {
        return false;
    }
    if (p.value > static_cast<uint64_t>(INT64_MAX)) {
        return false;
    }
    *out = static_cast<int64_t>(p.value);
    return true;
}

ParsedFloat parse_float_literal(string_view text) {
    ParsedFloat out;
    if (text.empty()) {
        return out;
    }
    string buf;
    size_t i = 0;
    for (; i < text.size(); i++) {
        char c = text[i];
        if (c == '_') {
            continue;
        }
        if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            buf += c;
            continue;
        }
        if (c >= 'a' && c <= 'z') {
            break;
        }
        return out;
    }
    if (buf.empty()) {
        return out;
    }
    char* end = nullptr;
    double v = strtod(buf.c_str(), &end);
    if (end == buf.c_str()) {
        return out;
    }
    out.ok = true;
    out.value = v;
    out.suffix = strip_suffix_alpha(text, i);
    return out;
}

bool parse_char_literal(string_view text, uint32_t* out) {
    if (out == nullptr || text.size() < 3 || text[0] != '\'' || text[text.size() - 1] != '\'') {
        return false;
    }
    string_view inner = text.substr(1, text.size() - 2);
    if (inner.empty()) {
        return false;
    }
    if (inner[0] != '\\') {
        unsigned char c = static_cast<unsigned char>(inner[0]);
        if (c < 0x80) {
            if (inner.size() != 1) {
                return false;
            }
            *out = c;
            return true;
        }
        // UTF-8 scalar
        uint32_t cp = 0;
        size_t n = 0;
        if ((c & 0xe0) == 0xc0 && inner.size() >= 2) {
            cp = (c & 0x1f) << 6 | (static_cast<unsigned char>(inner[1]) & 0x3f);
            n = 2;
        } else if ((c & 0xf0) == 0xe0 && inner.size() >= 3) {
            cp = (c & 0x0f) << 12 | (static_cast<unsigned char>(inner[1]) & 0x3f) << 6 |
                 (static_cast<unsigned char>(inner[2]) & 0x3f);
            n = 3;
        } else if ((c & 0xf8) == 0xf0 && inner.size() >= 4) {
            cp = (c & 0x07) << 18 | (static_cast<unsigned char>(inner[1]) & 0x3f) << 12 |
                 (static_cast<unsigned char>(inner[2]) & 0x3f) << 6 |
                 (static_cast<unsigned char>(inner[3]) & 0x3f);
            n = 4;
        } else {
            return false;
        }
        if (n != inner.size()) {
            return false;
        }
        *out = cp;
        return true;
    }
    if (inner.size() < 2) {
        return false;
    }
    char e = inner[1];
    uint32_t cp = 0;
    switch (e) {
    case 'n':
        cp = '\n';
        break;
    case 't':
        cp = '\t';
        break;
    case 'r':
        cp = '\r';
        break;
    case '0':
        cp = 0;
        break;
    case '\\':
        cp = '\\';
        break;
    case '\'':
        cp = '\'';
        break;
    case '"':
        cp = '"';
        break;
    case 'u': {
        if (inner.size() < 5 || inner[2] != '{') {
            return false;
        }
        size_t k = 3;
        uint32_t v = 0;
        bool any = false;
        while (k < inner.size() && inner[k] != '}') {
            char d = inner[k];
            int digit = -1;
            if (d >= '0' && d <= '9') {
                digit = d - '0';
            } else if (d >= 'a' && d <= 'f') {
                digit = d - 'a' + 10;
            } else if (d >= 'A' && d <= 'F') {
                digit = d - 'A' + 10;
            } else {
                return false;
            }
            any = true;
            if (v > 0x10FFFF / 16) {
                return false;
            }
            v = v * 16 + static_cast<uint32_t>(digit);
            k++;
        }
        if (!any || k >= inner.size() || inner[k] != '}' || k + 1 != inner.size()) {
            return false;
        }
        cp = v;
        *out = cp;
        return true;
    }
    default:
        return false;
    }
    if (inner.size() != 2) {
        return false;
    }
    *out = cp;
    return true;
}

} // namespace lucb
