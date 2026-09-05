#include "check/check.h"
#include "emit/emit.h"
#include "emit/host.h"
#include "interp/interp.h"
#include "pkg/package.h"
#include "support/test.h"

#include <string>
#include <unistd.h>
#include <vector>

using lucb::Arena;
using lucb::DiagnosticBag;
using lucb::Program;
using lucb::check_program;
using lucb::compile_c;
using lucb::compile_c_object;
using lucb::emit_c;
using lucb::emit_program;
using lucb::eval_module;
using lucb::eval_tests;
using lucb::load_program;
using lucb::parse_manifest_text;
using lucb::run_exe;

static std::vector<lucb::Node*> mods_of(Program& p) {
    std::vector<lucb::Node*> m;
    for (size_t i = 0; i < p.files.size(); i++) {
        m.push_back(p.files[i].module);
    }
    return m;
}

TEST(manifest_name) {
    lucb::Manifest man;
    std::string err;
    CHECK(parse_manifest_text("[package]\nname = \"widgets\"\n", ".", &man, &err));
    CHECK(man.name == "widgets");
}

TEST(load_and_check_import) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/m9/app.lucb", program, arena, diagnostics));
    CHECK(diagnostics.empty());
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/m9/app.lucb"));
    CHECK(diagnostics.empty());
    lucb::EvalResult r = eval_module(program.entry());
    CHECK(r.ok);
    CHECK_EQ(r.answer, 42);
}

TEST(load_from_import) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/m9/fromapp.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/m9/fromapp.lucb"));
    CHECK(diagnostics.empty());
    lucb::EvalResult r = eval_module(program.entry());
    CHECK(r.ok);
    CHECK_EQ(r.answer, 3);
}

TEST(hidden_import_rejected) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/m9/fromhidden.lucb", program, arena, diagnostics));
    check_program(mods_of(program), arena, diagnostics, "testdata/m9/fromhidden.lucb");
    CHECK(diagnostics.has_code("lucb.check.import"));
}

TEST(check_unused_import) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/m9/unused.lucb", program, arena, diagnostics));
    check_program(mods_of(program), arena, diagnostics, "testdata/m9/unused.lucb");
    CHECK(diagnostics.has_code("lucb.check.import"));
}

TEST(eval_tests_pass_and_fail) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/m9/tests.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/m9/tests.lucb"));
    lucb::TestRun run = eval_tests(mods_of(program));
    CHECK_EQ(run.passed, 1);
    CHECK_EQ(run.failed, 1);
}

TEST(eval_tests_call_user_func) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/programs/user_tests.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/programs/user_tests.lucb"));
    lucb::TestRun run = eval_tests(mods_of(program));
    CHECK_EQ(run.passed, 3);
    CHECK_EQ(run.failed, 0);
}

TEST(import_diag_names_the_imported_file) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/check/import_diag/main.lucb", program, arena, diagnostics));
    CHECK(diagnostics.empty());
    check_program(mods_of(program), arena, diagnostics, "testdata/check/import_diag/main.lucb");
    CHECK(!diagnostics.empty());
    bool named = false;
    for (size_t i = 0; i < diagnostics.items.size(); i++) {
        if (diagnostics.items[i].path.find("other.lucb") != std::string::npos) {
            named = true;
        }
    }
    CHECK(named);
}

TEST(agree_program_qenum) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/programs/qenum/main.lucb", program, arena, diagnostics));
    CHECK(diagnostics.empty());
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/programs/qenum/main.lucb"));
    CHECK(diagnostics.empty());
    std::string c = emit_program(mods_of(program), program.entry());
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    std::string exe = std::string(dir) + "/prog";
    std::string err;
    CHECK(compile_c(c, exe, &err, true));
    lucb::RunResult native = run_exe(exe);
    lucb::EvalResult interp = eval_module(program.entry());
    CHECK(interp.ok);
    CHECK_EQ(interp.answer, 40);
    CHECK_EQ(native.exit_code, 0);
    std::string want = std::to_string(interp.answer) + "\n";
    CHECK(native.out == want);
}

TEST(agree_program_compile) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/programs/compile/main.lucb", program, arena, diagnostics));
    CHECK(diagnostics.empty());
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/programs/compile/main.lucb"));
    CHECK(diagnostics.empty());
    std::string c = emit_program(mods_of(program), program.entry());
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    std::string exe = std::string(dir) + "/prog";
    std::string err;
    CHECK(compile_c(c, exe, &err, true));
    lucb::RunResult native = run_exe(exe);
    lucb::EvalResult interp = eval_module(program.entry());
    CHECK(interp.ok);
    CHECK_EQ(interp.answer, 40);
    CHECK_EQ(native.exit_code, 0);
    std::string want = std::to_string(interp.answer) + "\n";
    CHECK(native.out == want);
}

TEST(agree_imported_add) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/m9/app.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/m9/app.lucb"));
    std::string c = emit_program(mods_of(program), program.entry());
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    std::string exe = std::string(dir) + "/prog";
    std::string err;
    CHECK(compile_c(c, exe, &err, true));
    lucb::RunResult native = run_exe(exe);
    lucb::EvalResult interp = eval_module(program.entry());
    CHECK(interp.ok);
    CHECK_EQ(native.exit_code, 0);
    std::string want = std::to_string(interp.answer) + "\n";
    CHECK(native.out == want);
}

TEST(agree_main_hello) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/m9/main.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics, "testdata/m9/main.lucb"));
    std::string c = emit_c(program.entry());
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    CHECK(dir != nullptr);
    std::string exe = std::string(dir) + "/prog";
    std::string err;
    CHECK(compile_c(c, exe, &err, false));
    lucb::RunResult native = run_exe(exe);
    CHECK_EQ(native.exit_code, 0);
    CHECK(native.out.find("hello") != std::string::npos);
}

static bool spec24_compiles(const char* path) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    if (!load_program(path, program, arena, diagnostics)) {
        std::fprintf(stderr, "    load failed %s\n", path);
        for (size_t i = 0; i < diagnostics.items.size(); i++) {
            std::fprintf(stderr, "    %s\n", diagnostics.items[i].format().c_str());
        }
        return false;
    }
    if (!check_program(mods_of(program), arena, diagnostics, path)) {
        std::fprintf(stderr, "    check failed %s\n", path);
        for (size_t i = 0; i < diagnostics.items.size(); i++) {
            std::fprintf(stderr, "    %s\n", diagnostics.items[i].format().c_str());
        }
        return false;
    }
    std::string c = emit_c(program.entry());
    std::string err;
    if (!compile_c_object(c, &err)) {
        std::fprintf(stderr, "    cc -c failed %s\n%s\n", path, err.c_str());
        return false;
    }
    return true;
}

TEST(spec24_ex01) { CHECK(spec24_compiles("testdata/spec24/ex01.lucb")); }
TEST(spec24_ex02) { CHECK(spec24_compiles("testdata/spec24/ex02.lucb")); }
TEST(spec24_ex03) { CHECK(spec24_compiles("testdata/spec24/ex03.lucb")); }
TEST(spec24_ex04) { CHECK(spec24_compiles("testdata/spec24/ex04.lucb")); }
TEST(spec24_ex05) { CHECK(spec24_compiles("testdata/spec24/ex05.lucb")); }
TEST(spec24_ex06) { CHECK(spec24_compiles("testdata/spec24/ex06.lucb")); }
TEST(spec24_ex07) { CHECK(spec24_compiles("testdata/spec24/ex07.lucb")); }
TEST(spec24_ex08) { CHECK(spec24_compiles("testdata/spec24/ex08.lucb")); }
TEST(spec24_ex09) { CHECK(spec24_compiles("testdata/spec24/ex09.lucb")); }
TEST(spec24_ex10) { CHECK(spec24_compiles("testdata/spec24/ex10.lucb")); }
TEST(spec24_ex11) { CHECK(spec24_compiles("testdata/spec24/ex11.lucb")); }
TEST(spec24_ex12) { CHECK(spec24_compiles("testdata/spec24/ex12.lucb")); }
TEST(spec24_ex13) { CHECK(spec24_compiles("testdata/spec24/ex13.lucb")); }
TEST(spec24_ex14) { CHECK(spec24_compiles("testdata/spec24/ex14.lucb")); }
TEST(spec24_ex15) { CHECK(spec24_compiles("testdata/spec24/ex15.lucb")); }

TEST(emit_defer_order) { CHECK(spec24_compiles("testdata/programs/defer_order.lucb")); }
