//==============================================================================================
//
//   interp/interp - Public entry points of the oracle
//
//   DESCRIPTION:
//       The interpreter the tests compare against the compiled binary. It runs a checked
//       module directly from the syntax tree.
//
//==============================================================================================

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
    string err;
};

// Run `answer()` of `module`. `modules` lists every module of the program so
// their globals load and their functions resolve; empty means one module.
EvalResult eval_module(Node* module, const vector<Node*>& modules = {});

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
