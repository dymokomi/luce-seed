// Name resolution and type checking for the M3 scalar core.
// Annotates Node::ty and Node::resolved. base.md §§5–10 (subset).

#pragma once

#include "parse/ast.h"
#include "support/diagnostics.h"

namespace lucb {

bool check_module(Node* module, Arena& arena, DiagnosticBag& diagnostics,
                  string_view path = {});

} // namespace lucb
