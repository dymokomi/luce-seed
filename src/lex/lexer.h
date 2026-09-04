// Source text into tokens, including layout. base.md §3, §4, §21.

#pragma once

#include "lex/token.h"
#include "source/source.h"
#include "support/diagnostics.h"

#include <vector>

namespace lucb {

std::vector<Token> tokenize(const Source& source, DiagnosticBag& diagnostics);

} // namespace lucb
