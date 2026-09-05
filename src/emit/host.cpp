//==============================================================================================
//
//   emit/host - The host C compiler
//
//   DESCRIPTION:
//       Writes the generated C and the embedded runtime into a scratch directory, runs `cc`
//       with `-Wall -Werror` (and `-O2` for `--release`), and runs the result for the tests.
//       `CC` in the environment selects another compiler.
//
//==============================================================================================

#include "emit/host.h"
#include "emit/runtime_embed.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace lucb {
namespace {

string shell_quote(const string& s) {
    string out = "'";
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\'') {
            out += "'\\''";
        } else {
            out += s[i];
        }
    }
    out += "'";
    return out;
}

string slurp(const string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

string host_cc() {
    const char* cc = std::getenv("CC");
    if (cc != nullptr && cc[0] != '\0') {
        return cc;
    }
    return "cc";
}

bool write_text(const string& path, const char* text, string* error) {
    std::ofstream out(path);
    if (!out) {
        if (error != nullptr) {
            *error = "could not write " + path;
        }
        return false;
    }
    out << text;
    return true;
}

} // namespace

ScratchDir::ScratchDir() {
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (dir != nullptr) {
        path = dir;
    }
}

ScratchDir::~ScratchDir() {
    if (!path.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
}

bool compile_c(const string& c_source, const string& exe_path, string* error,
               bool link_answer_start, bool release) {
    ScratchDir scratch;
    if (!scratch.ok()) {
        if (error != nullptr) {
            *error = "could not create a temporary directory";
        }
        return false;
    }
    const string& dir_path = scratch.path;
    string src_path = dir_path + "/gen.c";
    if (!write_text(src_path, c_source.c_str(), error)) {
        return false;
    }
    if (!write_text(dir_path + "/lucb_rt.h", lucb_rt_h(), error)) {
        return false;
    }
    if (!write_text(dir_path + "/lucb_rt.c", lucb_rt_c(), error)) {
        return false;
    }
    if (link_answer_start && !write_text(dir_path + "/start.c", lucb_start_c(), error)) {
        return false;
    }

    const char* opt = release ? "-O2" : "-O0";
    string cmd = host_cc() + " -std=gnu11 -Wall -Werror " + opt + " -I " + shell_quote(dir_path) +
                 " " + shell_quote(src_path) + " " + shell_quote(dir_path + "/lucb_rt.c");
    if (link_answer_start) {
        cmd += " " + shell_quote(dir_path + "/start.c");
    }
    cmd += " -lm -pthread -o " + shell_quote(exe_path) + " 2> " + shell_quote(dir_path + "/cc.err");
    int status = std::system(cmd.c_str());
    if (status != 0) {
        if (error != nullptr) {
            *error = slurp(dir_path + "/cc.err");
            if (error->empty()) {
                *error = "C compile failed";
            }
        }
        return false;
    }
    return true;
}

bool compile_c_object(const string& c_source, string* error) {
    ScratchDir scratch;
    if (!scratch.ok()) {
        if (error != nullptr) {
            *error = "could not create a temporary directory";
        }
        return false;
    }
    const string& dir_path = scratch.path;
    string src_path = dir_path + "/gen.c";
    if (!write_text(src_path, c_source.c_str(), error)) {
        return false;
    }
    if (!write_text(dir_path + "/lucb_rt.h", lucb_rt_h(), error)) {
        return false;
    }
    string cmd = host_cc() + " -std=gnu11 -Wall -Werror -c -I " + shell_quote(dir_path) + " " +
                 shell_quote(src_path) + " -o " + shell_quote(dir_path + "/gen.o") + " 2> " +
                 shell_quote(dir_path + "/cc.err");
    int status = std::system(cmd.c_str());
    if (status != 0) {
        if (error != nullptr) {
            *error = slurp(dir_path + "/cc.err");
            if (error->empty()) {
                *error = "C compile failed";
            }
        }
        return false;
    }
    return true;
}

RunResult run_exe(const string& exe_path, const vector<string>& args) {
    RunResult result;
    ScratchDir scratch;
    if (!scratch.ok()) {
        result.err = "could not create a temporary directory";
        return result;
    }
    const string& dir_path = scratch.path;
    string out_path = dir_path + "/out";
    string err_path = dir_path + "/err";
    string cmd = shell_quote(exe_path);
    for (size_t i = 0; i < args.size(); i++) {
        cmd += " " + shell_quote(args[i]);
    }
    cmd += " > " + shell_quote(out_path) + " 2> " + shell_quote(err_path);
    int status = std::system(cmd.c_str());
    result.out = slurp(out_path);
    result.err = slurp(err_path);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else {
        result.exit_code = 1;
    }
    return result;
}

} // namespace lucb
