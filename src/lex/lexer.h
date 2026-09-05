//==============================================================================================
//
//   lex/lexer - Public entry point of the lexer
//
//   DESCRIPTION:
//       `tokenize` turns a `Source` into the token stream the parser consumes, reporting
//       encoding and lexical diagnostics.
//
//==============================================================================================

#pragma once

#include "lex/token.h"
#include "source/source.h"
#include "support/diagnostics.h"

#include <vector>

namespace lucb {

std::vector<Token> tokenize(const Source& source, DiagnosticBag& diagnostics);

} // namespace lucb
