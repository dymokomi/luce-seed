//==============================================================================================
//
//   check/check - Public entry points of the checker
//
//   DESCRIPTION:
//       `check_module` checks one module; `check_program` checks a package in dependency
//       order. Both record typed facts on the syntax tree in place and report diagnostics
//       with stable codes.
//
//==============================================================================================

#pragma once

#include "parse/ast.h"
#include "support/diagnostics.h"

namespace lucb {

bool check_module(Node* module, Arena& arena, DiagnosticBag& diagnostics, string_view path = {});

bool check_program(const vector<Node*>& modules, Arena& arena, DiagnosticBag& diagnostics,
                   string_view path = {});

} // namespace lucb
