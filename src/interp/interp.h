// HIR-less interpreter of a checked module. Scalar core only.

#pragma once

#include "parse/ast.h"

namespace lucb {

struct EvalResult {
    bool ok = false;
    bool trapped = false;
    string trap;
    bool has_answer = false;
    int64_t answer = 0;
    string output;
};

EvalResult eval_module(Node* module);

} // namespace lucb
