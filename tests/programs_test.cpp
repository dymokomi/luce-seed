//==============================================================================================
//
//   tests/programs_test - Every program under testdata/ and examples/ is a test
//
//   DESCRIPTION:
//       Walks the program directories and proves each `.lucb` file, or each
//       package directory holding a `luce.toml`, by the two-execution rule:
//       the interpreter and the compiled binary must agree. What a program
//       is asked to prove is read from the program itself:
//
//         pub func answer() -> i64   both runs return the same value, 40 unless
//                                    a `# answer: N` line says otherwise
//         pub func main(...)         both runs match on stdout, stderr, and exit
//                                    status; `# args: a b` supplies arguments;
//                                    a sibling `NAME.expect` pins stdout
//         test "...":                the test runner passes every test
//         anything else              the file checks and its C compiles
//
//       A `# oracle: none` line skips the interpreter (base.md §8.9 and the
//       reinterpreted-pointer rule in DESIGN.md); `# link: NAME` marks a
//       program whose link needs a library the host may lack, so only its C
//       is compiled. The spec's §24 programs are re-extracted from base.md
//       and compared with testdata/spec/, so the two cannot drift apart.
//
//==============================================================================================

#include "check/check.h"
#include "emit/emit.h"
#include "emit/host.h"
#include "interp/interp.h"
#include "pkg/package.h"
#include "support/test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

using lucb::Arena;
using lucb::DiagnosticBag;
using lucb::Program;

namespace {

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// The value after `# key:` on a line of its own, or empty.
std::string directive(const std::string& text, const char* key) {
    std::string prefix = std::string("# ") + key + ":";
    size_t at = 0;
    while ((at = text.find(prefix, at)) != std::string::npos) {
        if (at == 0 || text[at - 1] == '\n') {
            size_t end = text.find('\n', at);
            std::string value = text.substr(at + prefix.size(), end - at - prefix.size());
            size_t first = value.find_first_not_of(' ');
            return first == std::string::npos ? std::string() : value.substr(first);
        }
        at += prefix.size();
    }
    return {};
}

// Split on spaces; a double-quoted run is one word.
std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> words;
    std::string w;
    bool quoted = false;
    bool have = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') {
            quoted = !quoted;
            have = true;
        } else if (c == ' ' && !quoted) {
            if (have) {
                words.push_back(w);
            }
            w.clear();
            have = false;
        } else {
            w += c;
            have = true;
        }
    }
    if (have) {
        words.push_back(w);
    }
    return words;
}

std::vector<lucb::Node*> mods_of(Program& p) {
    std::vector<lucb::Node*> m;
    for (size_t i = 0; i < p.files.size(); i++) {
        m.push_back(p.files[i].module);
    }
    return m;
}

bool print_diagnostics(const DiagnosticBag& diagnostics) {
    for (size_t i = 0; i < diagnostics.items.size(); i++) {
        std::fprintf(stderr, "      %s\n", diagnostics.items[i].format().c_str());
    }
    return false;
}

// Prove one program. `entry` is the .lucb file; `source` is its text.
bool prove(const fs::path& entry) {
    std::string text = slurp(entry);
    bool has_answer = text.find("pub func answer") != std::string::npos;
    bool has_main = text.find("pub func main") != std::string::npos;
    bool has_tests = text.find("\ntest \"") != std::string::npos || text.rfind("test \"", 0) == 0;
    bool oracle = directive(text, "oracle") != "none";
    bool linkable = directive(text, "link").empty();
    // The spec's own programs carry no directives; two of them need one.
    std::string name = entry.filename().string();
    if (name == "intrusive_list.lucb") {
        oracle = false; // §24.5 reinterprets a pointer with offsetof (DESIGN.md)
    }
    if (name == "calling_c.lucb") {
        linkable = false; // §24.8 links SDL3
    }
    std::string args_line = directive(text, "args");
    if (name == "arena.lucb" && args_line.empty()) {
        args_line = "testdata/spec/arguments.lucb"; // §24.4 counts the words of a file
    }
    if (name == "error_handling.lucb" && args_line.empty()) {
        args_line = "testdata/no-such-config"; // §24.15 falls back to defaults
    }

    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    if (!lucb::load_program(entry.string(), program, arena, diagnostics)) {
        std::fprintf(stderr, "    load failed\n");
        return print_diagnostics(diagnostics);
    }
    std::vector<lucb::Node*> mods = mods_of(program);
    if (!lucb::check_program(mods, arena, diagnostics, entry.string())) {
        std::fprintf(stderr, "    check failed\n");
        return print_diagnostics(diagnostics);
    }
    if (!diagnostics.empty()) {
        std::fprintf(stderr, "    check produced warnings\n");
        return print_diagnostics(diagnostics);
    }
    std::string c = lucb::emit_program(mods, program.entry());
    std::string err;
    if (!lucb::compile_c_object(c, &err)) {
        std::fprintf(stderr, "    emitted C rejected by cc:\n%s\n", err.c_str());
        return false;
    }
    if (!linkable || (!has_answer && !has_main && !has_tests)) {
        return true;
    }

    if (has_tests && !has_answer && !has_main) {
        lucb::TestRun run = lucb::eval_tests(mods);
        if (run.trapped) {
            std::fprintf(stderr, "    tests trapped: %s\n", run.trap.c_str());
            return false;
        }
        if (run.failed != 0 || run.passed == 0) {
            std::fprintf(stderr, "    tests: %d passed, %d failed\n%s", run.passed, run.failed,
                         run.output.c_str());
            return false;
        }
        return true;
    }

    lucb::ScratchDir scratch;
    if (!scratch.ok()) {
        return false;
    }
    std::string exe = scratch.path + "/prog";
    if (!lucb::compile_c(c, exe, &err, has_answer)) {
        std::fprintf(stderr, "    link failed:\n%s\n", err.c_str());
        return false;
    }

    if (has_answer) {
        lucb::RunResult native = lucb::run_exe(exe);
        std::string want_answer = directive(text, "answer");
        if (want_answer.empty()) {
            want_answer = "40";
        }
        if (!oracle) {
            if (native.exit_code != 0 || native.out != want_answer + "\n") {
                std::fprintf(stderr, "    native: exit %d out=%s", native.exit_code,
                             native.out.c_str());
                return false;
            }
            return true;
        }
        lucb::EvalResult interp = lucb::eval_module(program.entry(), mods);
        if (interp.trapped) {
            if (native.exit_code == 0 || native.err.find(interp.trap) == std::string::npos) {
                std::fprintf(stderr, "    interp trapped (%s); native exit %d err=%s\n",
                             interp.trap.c_str(), native.exit_code, native.err.c_str());
                return false;
            }
            return true;
        }
        std::string want = interp.output;
        if (interp.has_answer) {
            want += std::to_string(interp.answer) + "\n";
        }
        if (!interp.ok || native.exit_code != 0 || want != native.out || interp.err != native.err) {
            std::fprintf(stderr, "    disagreement\n      interp: %s      native: %s (exit %d)\n",
                         want.c_str(), native.out.c_str(), native.exit_code);
            return false;
        }
        if (interp.has_answer && std::to_string(interp.answer) != want_answer) {
            std::fprintf(stderr, "    answer %lld, wanted %s\n",
                         static_cast<long long>(interp.answer), want_answer.c_str());
            return false;
        }
        return true;
    }

    // main
    std::vector<std::string> args = split_words(args_line);
    lucb::RunResult native = lucb::run_exe(exe, args);
    fs::path expect_path = entry;
    expect_path.replace_extension(".expect");
    if (fs::exists(expect_path)) {
        std::string expect = slurp(expect_path);
        if (native.out != expect) {
            std::fprintf(stderr, "    stdout differs from %s:\n%s", expect_path.c_str(),
                         native.out.c_str());
            return false;
        }
    }
    if (!oracle) {
        return native.exit_code == 0;
    }
    std::vector<std::string> argv_list;
    argv_list.push_back(exe);
    for (size_t i = 0; i < args.size(); i++) {
        argv_list.push_back(args[i]);
    }
    lucb::EvalResult interp;
    int32_t code = lucb::eval_main(mods, program.entry(), argv_list, &interp);
    if (interp.trapped) {
        std::fprintf(stderr, "    interp trapped: %s\n", interp.trap.c_str());
        return false;
    }
    if (code != native.exit_code || interp.output != native.out || interp.err != native.err) {
        std::fprintf(stderr,
                     "    disagreement\n      interp exit %d out=%s err=%s\n      native exit %d "
                     "out=%s err=%s\n",
                     code, interp.output.c_str(), interp.err.c_str(), native.exit_code,
                     native.out.c_str(), native.err.c_str());
        return false;
    }
    return true;
}

// Every entry under `root`: a .lucb file, or a directory with luce.toml
// whose entry is main.lucb.
std::vector<fs::path> program_entries(const fs::path& root) {
    std::vector<fs::path> entries;
    if (!fs::exists(root)) {
        return entries;
    }
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        if (item.is_regular_file() && item.path().filename() == "luce.toml") {
            entries.push_back(item.path().parent_path() / "main.lucb");
        } else if (item.is_regular_file() && item.path().extension() == ".lucb") {
            // A file inside a package directory belongs to that package.
            bool in_package = false;
            for (fs::path p = item.path().parent_path(); p != root && !p.empty();
                 p = p.parent_path()) {
                if (fs::exists(p / "luce.toml")) {
                    in_package = true;
                    break;
                }
            }
            if (!in_package) {
                entries.push_back(item.path());
            }
        }
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

// Prove every program under `root`, one line of output each.
void prove_directory(const char* root) {
    std::vector<fs::path> entries = program_entries(root);
    CHECK(!entries.empty());
    for (size_t i = 0; i < entries.size(); i++) {
        bool ok = prove(entries[i]);
        std::fprintf(stderr, "  %s %s\n", ok ? "ok  " : "FAIL", entries[i].c_str());
        CHECK(ok);
    }
}

} // namespace

TEST(programs_values) {
    prove_directory("testdata/programs/values");
}
TEST(programs_control) {
    prove_directory("testdata/programs/control");
}
TEST(programs_errors) {
    prove_directory("testdata/programs/errors");
}
TEST(programs_memory) {
    prove_directory("testdata/programs/memory");
}
TEST(programs_pointers) {
    prove_directory("testdata/programs/pointers");
}
TEST(programs_generics) {
    prove_directory("testdata/programs/generics");
}
TEST(programs_text) {
    prove_directory("testdata/programs/text");
}
TEST(programs_threads) {
    prove_directory("testdata/programs/threads");
}
TEST(programs_io) {
    prove_directory("testdata/programs/io");
}
TEST(programs_mains) {
    prove_directory("testdata/programs/mains");
}
TEST(programs_modules) {
    prove_directory("testdata/programs/modules");
}
TEST(programs_testing) {
    prove_directory("testdata/programs/testing");
}
TEST(spec_programs) {
    prove_directory("testdata/spec");
}
TEST(examples) {
    prove_directory("examples");
}

// The fifteen §24 programs in testdata/spec/ are the ones in base.md.
TEST(spec_programs_match_document) {
    static const char* names[15] = {
        "arguments",    "percentage",  "ring_buffer",    "arena",       "intrusive_list",
        "tagged_union", "flags",       "calling_c",      "exporting_c", "spinlock",
        "generic_span", "text_format", "string_builder", "hash_map",    "error_handling",
    };
    std::string doc = slurp("docs/language/base.md");
    CHECK(!doc.empty());
    for (int i = 0; i < 15; i++) {
        std::string heading = "### 24." + std::to_string(i + 1) + " ";
        size_t at = doc.find(heading);
        CHECK(at != std::string::npos);
        if (at == std::string::npos) {
            continue;
        }
        size_t open = doc.find("```luce\n", at);
        size_t close = doc.find("\n```", open + 8);
        CHECK(open != std::string::npos && close != std::string::npos);
        std::string body = doc.substr(open + 8, close + 1 - (open + 8));
        std::string pinned = slurp(std::string("testdata/spec/") + names[i] + ".lucb");
        if (body != pinned) {
            std::fprintf(stderr, "  testdata/spec/%s.lucb differs from base.md §24.%d\n", names[i],
                         i + 1);
        }
        CHECK(body == pinned);
    }
}
