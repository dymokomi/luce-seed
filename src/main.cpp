//==============================================================================================
//
//   main - The lucb command
//
//   DESCRIPTION:
//       The driver: `check`, `lex`, `dump`, `eval`, `build` (with `--emit=c` and
//       `--release`), `header`, and `test`. It loads a program, runs the pipeline, prints
//       diagnostics, and maps outcomes to exit codes.
//
//==============================================================================================

#include "check/check.h"
#include "emit/emit.h"
#include "emit/host.h"
#include "interp/interp.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "pkg/package.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace std;

namespace {

const char* k_version = "0.8";

void print_help(ostream& out) {
    out << "lucb " << k_version << " — luce-seed, the Luce Base bootstrap compiler\n"
        << "\n"
        << "Usage:\n"
        << "  lucb --version\n"
        << "  lucb --help\n"
        << "  lucb check <file.lucb>     lex, parse, and typecheck\n"
        << "  lucb lex   <file.lucb>     print tokens\n"
        << "  lucb dump  <file.lucb>     print the parse tree\n"
        << "  lucb eval  <file.lucb> [args]  run `answer()` or `main` in the interpreter\n"
        << "  lucb build <file.lucb> -o <exe>   emit C and compile\n"
        << "  lucb build <file.lucb> --release -o <exe>\n"
        << "  lucb build <file.lucb> --emit=c -o <file.c>\n"
        << "  lucb header <file.lucb> [-o file.h]  write the export header\n"
        << "  lucb test  <file.lucb>     run `test` declarations\n";
}

string read_file(const string& path, string& error) {
    ifstream in(path, ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return {};
    }
    ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// `-W` on any command prints the checker's warnings; without it they are silent.
static bool g_show_warnings = false;

int print_diagnostics(const lucb::DiagnosticBag& diagnostics) {
    for (size_t i = 0; i < diagnostics.items.size(); i++) {
        cerr << diagnostics.items[i].format() << '\n';
    }
    if (g_show_warnings) {
        for (size_t i = 0; i < diagnostics.warnings.size(); i++) {
            cerr << diagnostics.warnings[i].format() << '\n';
        }
    }
    return diagnostics.empty() ? 0 : 1;
}

bool load_and_lex(const string& path, lucb::Source& source, vector<lucb::Token>& tokens,
                  lucb::DiagnosticBag& diagnostics) {
    string error;
    string bytes = read_file(path, error);
    if (!error.empty()) {
        cerr << error << '\n';
        return false;
    }
    source = lucb::Source::from_bytes(path, bytes, diagnostics);
    if (!source.ok()) {
        return false;
    }
    tokens = lucb::tokenize(source, diagnostics);
    return diagnostics.empty();
}

int cmd_lex(const string& path) {
    lucb::DiagnosticBag diagnostics;
    lucb::Source source;
    vector<lucb::Token> tokens;
    if (!load_and_lex(path, source, tokens, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    for (size_t i = 0; i < tokens.size(); i++) {
        const lucb::Token& token = tokens[i];
        printf("%u:%u\t%-16s %.*s\n", token.span.line, token.span.column,
               lucb::token_kind_name(token.kind), static_cast<int>(token.text.size()),
               token.text.data());
    }
    return 0;
}

bool has_func(lucb::Node* mod, const char* name) {
    if (mod == nullptr) {
        return false;
    }
    for (lucb::Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == lucb::NodeKind::Func && d->text == name) {
            return true;
        }
    }
    return false;
}

vector<lucb::Node*> program_modules(lucb::Program& program) {
    vector<lucb::Node*> mods;
    for (size_t i = 0; i < program.files.size(); i++) {
        mods.push_back(program.files[i].module);
    }
    return mods;
}

int cmd_check(const string& path) {
    lucb::DiagnosticBag diagnostics;
    lucb::Arena arena;
    lucb::Program program;
    if (!lucb::load_program(path, program, arena, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    lucb::check_program(program_modules(program), arena, diagnostics, path);
    return print_diagnostics(diagnostics);
}

int cmd_eval(const string& path, const vector<string>& args) {
    lucb::DiagnosticBag diagnostics;
    lucb::Arena arena;
    lucb::Program program;
    if (!lucb::load_program(path, program, arena, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    vector<lucb::Node*> mods = program_modules(program);
    if (!lucb::check_program(mods, arena, diagnostics, path)) {
        return print_diagnostics(diagnostics);
    }
    lucb::Node* entry = program.entry();
    if (has_func(entry, "answer")) {
        lucb::EvalResult result = lucb::eval_module(entry, mods);
        cout << result.output;
        if (result.trapped) {
            cerr << "trap: " << result.trap << '\n';
            return 1;
        }
        if (result.has_answer) {
            cout << result.answer << '\n';
        }
        return result.ok ? 0 : 1;
    }
    lucb::EvalResult result;
    vector<string> argv_list;
    argv_list.push_back(path);
    for (size_t i = 0; i < args.size(); i++) {
        argv_list.push_back(args[i]);
    }
    int32_t code = lucb::eval_main(mods, entry, argv_list, &result);
    cout << result.output;
    cerr << result.err;
    if (result.trapped) {
        cerr << "trap: " << result.trap << '\n';
        return 1;
    }
    return code;
}

int cmd_build(int argc, char** argv) {
    string in_path;
    string out_path;
    bool emit_c_only = false;
    bool release = false;
    for (int i = 2; i < argc; i++) {
        string_view a = argv[i];
        if (a == "--emit=c") {
            emit_c_only = true;
        } else if (a == "--release") {
            release = true;
        } else if (a == "-o") {
            if (i + 1 >= argc) {
                cerr << "lucb: -o needs an argument\n";
                return 2;
            }
            out_path = argv[++i];
        } else if (in_path.empty() && a.size() > 0 && a[0] != '-') {
            in_path = argv[i];
        } else {
            cerr << "lucb: unexpected argument `" << argv[i] << "`\n";
            return 2;
        }
    }
    if (in_path.empty()) {
        cerr << "lucb: missing file argument\n";
        return 2;
    }
    if (out_path.empty()) {
        out_path = emit_c_only ? "out.c" : "a.out";
    }

    lucb::DiagnosticBag diagnostics;
    lucb::Arena arena;
    lucb::Program program;
    if (!lucb::load_program(in_path, program, arena, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    vector<lucb::Node*> mods = program_modules(program);
    if (!lucb::check_program(mods, arena, diagnostics, in_path)) {
        return print_diagnostics(diagnostics);
    }
    lucb::Node* entry = program.entry();
    string c = mods.size() > 1 ? lucb::emit_program(mods, entry) : lucb::emit_c(entry);
    bool link_answer = !has_func(entry, "main");
    if (emit_c_only) {
        ofstream out(out_path);
        if (!out) {
            cerr << "lucb: cannot write " << out_path << '\n';
            return 1;
        }
        out << c;
        return 0;
    }
    string error;
    if (!lucb::compile_c(c, out_path, &error, link_answer, release)) {
        cerr << error;
        if (error.empty() || error.back() != '\n') {
            cerr << '\n';
        }
        return 1;
    }
    return 0;
}

int cmd_header(int argc, char** argv) {
    string in_path;
    string out_path;
    for (int i = 2; i < argc; i++) {
        string_view a = argv[i];
        if (a == "-o") {
            if (i + 1 >= argc) {
                cerr << "lucb: -o needs an argument\n";
                return 2;
            }
            out_path = argv[++i];
        } else if (in_path.empty() && a.size() > 0 && a[0] != '-') {
            in_path = argv[i];
        } else {
            cerr << "lucb: unexpected argument `" << argv[i] << "`\n";
            return 2;
        }
    }
    if (in_path.empty()) {
        cerr << "lucb: missing file argument\n";
        return 2;
    }

    lucb::DiagnosticBag diagnostics;
    lucb::Arena arena;
    lucb::Program program;
    if (!lucb::load_program(in_path, program, arena, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    vector<lucb::Node*> mods = program_modules(program);
    if (!lucb::check_program(mods, arena, diagnostics, in_path)) {
        return print_diagnostics(diagnostics);
    }
    lucb::Node* entry = program.entry();
    string h = lucb::emit_header(entry);
    if (out_path.empty()) {
        cout << h;
        return 0;
    }
    ofstream out(out_path);
    if (!out) {
        cerr << "lucb: cannot write " << out_path << '\n';
        return 1;
    }
    out << h;
    return 0;
}

int cmd_test(const string& path) {
    lucb::DiagnosticBag diagnostics;
    lucb::Arena arena;
    lucb::Program program;
    if (!lucb::load_program(path, program, arena, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    vector<lucb::Node*> mods = program_modules(program);
    if (!lucb::check_program(mods, arena, diagnostics, path)) {
        return print_diagnostics(diagnostics);
    }
    lucb::TestRun run = lucb::eval_tests(mods);
    cout << run.output;
    int total = run.passed + run.failed;
    if (run.failed != 0) {
        cout << run.failed << " failed, " << run.passed << " passed\n";
        return 1;
    }
    cout << total << " passed\n";
    return 0;
}

int cmd_dump(const string& path) {
    lucb::DiagnosticBag diagnostics;
    lucb::Source source;
    vector<lucb::Token> tokens;
    if (!load_and_lex(path, source, tokens, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    lucb::Arena arena;
    lucb::ParseResult parsed = lucb::parse(source, tokens, arena, diagnostics);
    int status = print_diagnostics(diagnostics);
    if (parsed.module != nullptr) {
        cout << lucb::dump_tree(parsed.module) << '\n';
    }
    return status;
}

} // namespace

int main(int argc, char** argv) {
    // `-W` may appear anywhere; it is taken out before the command sees its arguments
    static vector<char*> kept;
    for (int i = 0; i < argc; i++) {
        if (string_view(argv[i]) == "-W") {
            g_show_warnings = true;
        } else {
            kept.push_back(argv[i]);
        }
    }
    kept.push_back(nullptr);
    argc = static_cast<int>(kept.size()) - 1;
    argv = kept.data();
    if (argc < 2) {
        print_help(cerr);
        return 2;
    }

    const string_view arg1 = argv[1];
    if (arg1 == "--help" || arg1 == "-h") {
        print_help(cout);
        return 0;
    }
    if (arg1 == "--version" || arg1 == "-V") {
        cout << "lucb " << k_version << '\n';
        return 0;
    }

    if (argc < 3) {
        cerr << "lucb: missing file argument\n";
        print_help(cerr);
        return 2;
    }

    if (arg1 == "build") {
        return cmd_build(argc, argv);
    }
    if (arg1 == "header") {
        return cmd_header(argc, argv);
    }

    const string path = argv[2];
    if (arg1 == "lex") {
        return cmd_lex(path);
    }
    if (arg1 == "check") {
        return cmd_check(path);
    }
    if (arg1 == "dump") {
        return cmd_dump(path);
    }
    if (arg1 == "eval") {
        vector<string> args;
        for (int i = 3; i < argc; i++) {
            args.push_back(argv[i]);
        }
        return cmd_eval(path, args);
    }
    if (arg1 == "test") {
        return cmd_test(path);
    }
    if (arg1 == "run") {
        cerr << "lucb: `run` is not implemented yet\n";
        return 2;
    }

    cerr << "lucb: unknown command `" << arg1 << "`\n";
    print_help(cerr);
    return 2;
}
