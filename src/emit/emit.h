// Checked AST → C for the scalar core.

#pragma once

#include "parse/ast.h"

namespace lucb {

string emit_c(Node* module);
string emit_program(const vector<Node*>& modules, Node* entry);

} // namespace lucb
