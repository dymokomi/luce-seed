// lucb: the Luce Base bootstrap compiler driver.

#include "lex/lexer.h"
#include "parse/parser.h"
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

const char* k_version = "0.1.0";

void print_help(ostream& out) {
    out << "lucb " << k_version << " — Luce Base bootstrap compiler\n"
        << "\n"
        << "Usage:\n"
        << "  lucb --version\n"
        << "  lucb --help\n"
        << "  lucb check <file.lucb>     lex and parse\n"
        << "  lucb lex   <file.lucb>     print tokens\n"
        << "  lucb dump  <file.lucb>     print the parse tree\n"
        << "\n"
        << "Not yet implemented: build, run, eval, test.\n";
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

int print_diagnostics(const lucb::DiagnosticBag& diagnostics) {
    for (size_t i = 0; i < diagnostics.items.size(); i++) {
        cerr << diagnostics.items[i].format() << '\n';
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

int cmd_check(const string& path) {
    lucb::DiagnosticBag diagnostics;
    lucb::Source source;
    vector<lucb::Token> tokens;
    if (!load_and_lex(path, source, tokens, diagnostics)) {
        return print_diagnostics(diagnostics);
    }
    lucb::Arena arena;
    lucb::parse(source, tokens, arena, diagnostics);
    return print_diagnostics(diagnostics);
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
    if (arg1 == "build" || arg1 == "run" || arg1 == "eval" || arg1 == "test") {
        cerr << "lucb: `" << arg1 << "` is not implemented yet\n";
        return 2;
    }

    cerr << "lucb: unknown command `" << arg1 << "`\n";
    print_help(cerr);
    return 2;
}
