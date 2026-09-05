//==============================================================================================
//
//   support/literal - Decode literal spellings
//
//   DESCRIPTION:
//       The decoders for integer, float, character, and string literals.
//
//==============================================================================================

#pragma once

#include "support/common.h"

namespace lucb {

struct ParsedInt {
    bool ok = false;
    uint64_t value = 0;
    string_view suffix;
};

struct ParsedFloat {
    bool ok = false;
    double value = 0;
    string_view suffix;
};

bool parse_i64_literal(string_view text, int64_t* out);
ParsedInt parse_int_literal(string_view text);
ParsedFloat parse_float_literal(string_view text);
bool parse_char_literal(string_view text, uint32_t* out);

inline string unescape_format_braces(string_view s) {
    string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (i + 1 < s.size() &&
            ((s[i] == '{' && s[i + 1] == '{') || (s[i] == '}' && s[i + 1] == '}'))) {
            o += s[i];
            i++;
        } else {
            o += s[i];
        }
    }
    return o;
}

} // namespace lucb
