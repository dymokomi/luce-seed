// Checked AST → C for the scalar core.

#pragma once

#include "parse/ast.h"

namespace lucb {

string emit_c(Node* module);

} // namespace lucb
