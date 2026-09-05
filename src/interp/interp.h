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

struct TestRun {
    int passed = 0;
    int failed = 0;
    string output;
    bool trapped = false;
    string trap;
};

TestRun eval_tests(const vector<Node*>& modules);
int32_t eval_main(const vector<Node*>& modules, Node* entry, const vector<string>& args,
                  EvalResult* result);

} // namespace lucb
