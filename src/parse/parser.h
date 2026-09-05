//==============================================================================================
//
//   parse/parser - Public entry point of the parser
//
//   DESCRIPTION:
//       `parse` turns a token stream into a module tree, reporting syntax diagnostics with
//       stable codes.
//
//==============================================================================================

#pragma once

#include "parse/ast.h"
#include "source/source.h"
#include "support/diagnostics.h"

namespace lucb {

struct ParseResult {
    Node* module = nullptr;
    bool ok = false;
};

ParseResult parse(const Source& source, const vector<Token>& tokens, Arena& arena,
                  DiagnosticBag& diagnostics);

} // namespace lucb
