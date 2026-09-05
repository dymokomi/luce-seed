#include "emit/host.h"

#include <cstdio>
#include <cstdlib>
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

} // namespace

string runtime_dir() {
#ifdef LUCB_RUNTIME_DIR
    return LUCB_RUNTIME_DIR;
#else
    return "src/runtime";
#endif
}

bool compile_c(const string& c_source, const string& exe_path, string* error,
               bool link_answer_start) {
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (dir == nullptr) {
        if (error != nullptr) {
            *error = "could not create a temporary directory";
        }
        return false;
    }
    string dir_path = dir;
    string src_path = dir_path + "/gen.c";
    std::ofstream out(src_path);
    if (!out) {
        if (error != nullptr) {
            *error = "could not write generated C";
        }
        return false;
    }
    out << c_source;
    out.close();

    string rt = runtime_dir();
    string cmd = host_cc() + " -std=gnu11 -O0 -I " + shell_quote(rt) + " " + shell_quote(src_path) +
                 " " + shell_quote(rt + "/lucb_rt.c");
    if (link_answer_start) {
        cmd += " " + shell_quote(rt + "/start.c");
    }
    cmd += " -lm -o " + shell_quote(exe_path) + " 2> " + shell_quote(dir_path + "/cc.err");
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
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (dir == nullptr) {
        result.err = "could not create a temporary directory";
        return result;
    }
    string dir_path = dir;
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
