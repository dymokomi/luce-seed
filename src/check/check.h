// Name resolution and type checking for the scalar language.
// Annotates Node::ty and Node::resolved. base.md §§5–10 (subset).

#pragma once

#include "parse/ast.h"
#include "support/diagnostics.h"

namespace lucb {

bool check_module(Node* module, Arena& arena, DiagnosticBag& diagnostics,
                  string_view path = {});

bool check_program(const vector<Node*>& modules, Arena& arena, DiagnosticBag& diagnostics,
                   string_view path = {});

} // namespace lucb
