//==============================================================================================
//
//   emit/emit - Public entry points of the C backend
//
//   DESCRIPTION:
//       A checked module or program becomes one C translation unit; an exported module
//       becomes a C11 header.
//
//==============================================================================================

#pragma once

#include "parse/ast.h"

namespace lucb {

string emit_c(Node* module);
string emit_program(const vector<Node*>& modules, Node* entry);
// The manifest's `symbol_prefix`, put before every exported symbol (§17.6); set once per build.
void set_export_prefix(string_view prefix);
string emit_header(Node* module);

} // namespace lucb
