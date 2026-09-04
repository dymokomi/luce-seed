// Decode integer, float, and character literal spellings. base.md §4.

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

} // namespace lucb
