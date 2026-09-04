// lucb: the Luce Base bootstrap compiler driver.

#include "lex/lexer.h"
#include "source/source.h"
#include "support/diagnostics.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

const char* k_version = "0.1.0";

void print_help(std::ostream& out) {
    out << "lucb " << k_version << " — Luce Base bootstrap compiler\n"
        << "\n"
        << "Usage:\n"
        << "  lucb --version\n"
        << "  lucb --help\n"
        << "  lucb check <file.lucb>     lex the file and report diagnostics\n"
        << "  lucb lex   <file.lucb>     print tokens\n"
        << "\n"
        << "Not yet implemented: build, run, eval, test.\n";
}

std::string read_file(const std::string& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

int print_diagnostics(const lucb::DiagnosticBag& diagnostics) {
    for (const lucb::Diagnostic& item : diagnostics.items()) {
        std::cerr << item.format() << '\n';
    }
    return diagnostics.empty() ? 0 : 1;
}

int cmd_lex(const std::string& path, bool print_tokens) {
    std::string error;
    std::string bytes = read_file(path, error);
    if (!error.empty()) {
        std::cerr << error << '\n';
        return 1;
    }

    lucb::DiagnosticBag diagnostics;
    lucb::Source source = lucb::Source::from_bytes(path, std::move(bytes), diagnostics);
    if (!source.ok()) {
        return print_diagnostics(diagnostics);
    }

    std::vector<lucb::Token> tokens = lucb::tokenize(source, diagnostics);
    if (print_tokens && diagnostics.empty()) {
        for (const lucb::Token& token : tokens) {
            std::printf("%u:%u\t%-16s %.*s\n", token.span.line, token.span.column,
                        lucb::token_kind_name(token.kind), static_cast<int>(token.text.size()),
                        token.text.data());
        }
    }
    return print_diagnostics(diagnostics);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help(std::cerr);
        return 2;
    }

    const std::string_view arg1 = argv[1];
    if (arg1 == "--help" || arg1 == "-h") {
        print_help(std::cout);
        return 0;
    }
    if (arg1 == "--version" || arg1 == "-V") {
        std::cout << "lucb " << k_version << '\n';
        return 0;
    }

    if (argc < 3) {
        std::cerr << "lucb: missing file argument\n";
        print_help(std::cerr);
        return 2;
    }

    const std::string path = argv[2];
    if (arg1 == "lex") {
        return cmd_lex(path, true);
    }
    if (arg1 == "check") {
        return cmd_lex(path, false);
    }
    if (arg1 == "build" || arg1 == "run" || arg1 == "eval" || arg1 == "test") {
        std::cerr << "lucb: `" << arg1 << "` is not implemented yet\n";
        return 2;
    }

    std::cerr << "lucb: unknown command `" << arg1 << "`\n";
    print_help(std::cerr);
    return 2;
}
