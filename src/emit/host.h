//==============================================================================================
//
//   emit/host - Compile and run through the host toolchain
//
//   DESCRIPTION:
//       `compile_c` links an executable, `compile_c_object` type-checks generated C without
//       linking, `run_exe` captures a program's streams and exit status.
//
//==============================================================================================

#pragma once

#include "support/common.h"

namespace lucb {

// A temporary directory that is removed, with everything in it, when the
// object goes out of scope. Every compile and run uses one, so nothing is
// left behind under /tmp.
struct ScratchDir {
    string path;
    ScratchDir();
    ~ScratchDir();
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
    bool ok() const {
        return !path.empty();
    }
};

struct RunResult {
    int exit_code = 1;
    string out;
    string err;
};

// A package's `[native]` inputs (base.md §17.4): C sources compiled into the artifact, and what
// the link step is told; paths are relative to `root`.
struct NativeInputs {
    string root;
    vector<string> sources;
    vector<string> libraries;
    vector<string> link_search;
    vector<string> frameworks;
    vector<string> pkg_config;
};

bool compile_c(const string& c_source, const string& exe_path, string* error,
               bool link_answer_start = true, bool release = false,
               const NativeInputs* native = nullptr);

// Typecheck generated C with -Wall -Werror without linking.
bool compile_c_object(const string& c_source, string* error);

RunResult run_exe(const string& exe_path, const vector<string>& args = {});

} // namespace lucb
