// Host C compiler: write generated C, compile with runtime, run the exe.

#pragma once

#include "support/common.h"

namespace lucb {

struct RunResult {
    int exit_code = 1;
    string out;
    string err;
};

string runtime_dir();

bool compile_c(const string& c_source, const string& exe_path, string* error,
               bool link_answer_start = true, bool release = false);

// Typecheck generated C with -Wall -Werror without linking.
bool compile_c_object(const string& c_source, string* error);

RunResult run_exe(const string& exe_path, const vector<string>& args = {});

} // namespace lucb
