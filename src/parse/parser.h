// Recursive-descent parser for base.md §21. Expressions use layered
// precedence (catch / else / conditional / binary / unary / postfix).

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
