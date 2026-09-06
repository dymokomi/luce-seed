//==============================================================================================
//
//   pkg/package - Package loading
//
//   DESCRIPTION:
//       `load_program` turns a file or package directory into the modules the checker and
//       emitter consume.
//
//==============================================================================================

#pragma once

#include "lex/token.h"
#include "parse/parser.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"

namespace lucb {

struct Manifest {
    string name = "app";
    string root;
    // `[package] symbol_prefix`: what every exported symbol starts with (§17.6)
    string symbol_prefix;
    // `[native]`: C sources compiled into the artifact, and what the link step is told (§17.4)
    vector<string> sources;
    vector<string> libraries;
    vector<string> link_search;
    vector<string> frameworks;
    vector<string> pkg_config;
};

struct LoadedModule {
    string path;
    string name;
    Source source;
    vector<Token> tokens;
    Node* module = nullptr;
};

struct Program {
    Manifest manifest;
    Arena* arena = nullptr;
    vector<LoadedModule> files;

    Node* entry() const {
        return files.empty() ? nullptr : files[0].module;
    }
};

bool parse_manifest_text(const string& text, const string& root, Manifest* out, string* error);

// Load `path` (.lucb file or package directory) and every imported module.
bool load_program(const string& path, Program& program, Arena& arena, DiagnosticBag& diagnostics);

string module_alias(string_view path);
string dotted_to_path(string_view dotted);

} // namespace lucb
