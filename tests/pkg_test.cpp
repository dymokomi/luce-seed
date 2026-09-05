//==============================================================================================
//
//   tests/pkg_test - Packages, imports, tests, and exported headers
//
//   DESCRIPTION:
//       Manifest parsing, import resolution and visibility, unused-import diagnostics, `lucb
//       test`, and the C header of an exported module.
//
//==============================================================================================

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
using lucb::check_program;
using lucb::compile_c;
using lucb::compile_c_object;
using lucb::DiagnosticBag;
using lucb::emit_c;
using lucb::emit_program;
using lucb::eval_module;
using lucb::eval_tests;
using lucb::load_program;
using lucb::parse_manifest_text;
using lucb::Program;
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
    CHECK(load_program("testdata/programs/modules/imports/app.lucb", program, arena, diagnostics));
    CHECK(diagnostics.empty());
    CHECK(check_program(mods_of(program), arena, diagnostics,
                        "testdata/programs/modules/imports/app.lucb"));
    CHECK(diagnostics.empty());
    lucb::EvalResult r = eval_module(program.entry());
    CHECK(r.ok);
    CHECK_EQ(r.answer, 42);
}

TEST(load_from_import) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/programs/modules/imports/fromapp.lucb", program, arena,
                       diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics,
                        "testdata/programs/modules/imports/fromapp.lucb"));
    CHECK(diagnostics.empty());
    lucb::EvalResult r = eval_module(program.entry());
    CHECK(r.ok);
    CHECK_EQ(r.answer, 3);
}

TEST(hidden_import_rejected) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/programs/modules/imports/fromhidden.lucb", program, arena,
                       diagnostics));
    check_program(mods_of(program), arena, diagnostics,
                  "testdata/programs/modules/imports/fromhidden.lucb");
    CHECK(diagnostics.has_code("lucb.check.import"));
}

TEST(check_unused_import) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(
        load_program("testdata/programs/modules/imports/unused.lucb", program, arena, diagnostics));
    check_program(mods_of(program), arena, diagnostics,
                  "testdata/programs/modules/imports/unused.lucb");
    CHECK(diagnostics.has_code("lucb.check.import"));
}

TEST(eval_tests_pass_and_fail) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(
        load_program("testdata/programs/modules/imports/tests.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics,
                        "testdata/programs/modules/imports/tests.lucb"));
    lucb::TestRun run = eval_tests(mods_of(program));
    CHECK_EQ(run.passed, 1);
    CHECK_EQ(run.failed, 1);
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

TEST(agree_imported_add) {
    DiagnosticBag diagnostics;
    Arena arena;
    Program program;
    CHECK(load_program("testdata/programs/modules/imports/app.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics,
                        "testdata/programs/modules/imports/app.lucb"));
    std::string c = emit_program(mods_of(program), program.entry());
    lucb::ScratchDir scratch;
    CHECK(scratch.ok());
    std::string exe = scratch.path + "/prog";
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
    CHECK(load_program("testdata/programs/modules/imports/main.lucb", program, arena, diagnostics));
    CHECK(check_program(mods_of(program), arena, diagnostics,
                        "testdata/programs/modules/imports/main.lucb"));
    std::string c = emit_c(program.entry());
    lucb::ScratchDir scratch;
    CHECK(scratch.ok());
    std::string exe = scratch.path + "/prog";
    std::string err;
    CHECK(compile_c(c, exe, &err, false));
    lucb::RunResult native = run_exe(exe);
    CHECK_EQ(native.exit_code, 0);
    CHECK(native.out.find("hello") != std::string::npos);
}
