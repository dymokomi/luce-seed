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

// The identity of a package's error codes (§11.3): sixteen bits of its name, in the high half
// of every `ErrorCode.package(n)` of that package.
uint32_t package_identity(string_view name);

bool check_program(const vector<Node*>& modules, Arena& arena, DiagnosticBag& diagnostics,
                   string_view path = {}, string_view package_name = "app");

} // namespace lucb
