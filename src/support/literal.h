// Decode a Luce integer literal spelling into i64. Underscores allowed.
// Returns false if the spelling is not a whole i64 (bad digits or overflow).

#pragma once

#include "support/common.h"

namespace lucb {

bool parse_i64_literal(string_view text, int64_t* out);

} // namespace lucb
