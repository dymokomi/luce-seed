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
string emit_header(Node* module);

} // namespace lucb
