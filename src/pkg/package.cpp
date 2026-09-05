#include "pkg/package.h"

#include "lex/lexer.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace lucb {
namespace {

string slurp_file(const string& path, string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) {
            *error = "cannot open " + path;
        }
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool is_dir(const string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_file(const string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

string dirname_of(const string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

string join_path(const string& a, const string& b) {
    if (a.empty() || a == ".") {
        return b;
    }
    if (a.back() == '/') {
        return a + b;
    }
    return a + "/" + b;
}

string trim(string_view s) {
    size_t i = 0;
    size_t j = s.size();
    while (i < j && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) {
        i++;
    }
    while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t' || s[j - 1] == '\r')) {
        j--;
    }
    return string(s.substr(i, j - i));
}

string unquote(const string& s) {
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') ||
                          (s.front() == '\'' && s.back() == '\''))) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

string find_manifest(const string& start) {
    string dir = is_dir(start) ? start : dirname_of(start);
    for (int i = 0; i < 16; i++) {
        string cand = join_path(dir, "luce.toml");
        if (is_file(cand)) {
            return cand;
        }
        if (dir == "/" || dir == ".") {
            break;
        }
        string parent = dirname_of(dir);
        if (parent == dir) {
            break;
        }
        dir = parent;
    }
    return {};
}

LoadedModule* find_loaded(Program& program, string_view name) {
    for (size_t i = 0; i < program.files.size(); i++) {
        if (program.files[i].name == name) {
            return &program.files[i];
        }
    }
    return nullptr;
}

string resolve_file(const Program& program, string_view dotted) {
    string rel = dotted_to_path(dotted) + ".lucb";
    string a = join_path(program.manifest.root, rel);
    if (is_file(a)) {
        return a;
    }
    string b = join_path(join_path(program.manifest.root, "src"), rel);
    if (is_file(b)) {
        return b;
    }
    return {};
}

bool load_one(Program& program, const string& path, const string& name, DiagnosticBag& diagnostics,
              vector<string>& stack);

bool load_imports(Program& program, size_t idx, DiagnosticBag& diagnostics, vector<string>& stack) {
    Node* module = program.files[idx].module;
    if (module == nullptr) {
        return false;
    }
    for (Node* d = module->body; d != nullptr; d = d->next) {
        if (d->kind != NodeKind::Import && d->kind != NodeKind::FromImport) {
            continue;
        }
        string dep = string(d->text);
        string here = program.files[idx].path;
        for (size_t i = 0; i < stack.size(); i++) {
            if (stack[i] == dep) {
                diagnostics.add("lucb.check.import", here, d->span, "module cycle");
                return false;
            }
        }
        LoadedModule* existing = find_loaded(program, dep);
        if (existing == nullptr) {
            string path = resolve_file(program, dep);
            if (path.empty()) {
                diagnostics.add("lucb.check.import", here, d->span,
                                "cannot find module `" + dep + "`");
                return false;
            }
            if (!load_one(program, path, dep, diagnostics, stack)) {
                return false;
            }
            existing = find_loaded(program, dep);
        }
        if (existing != nullptr) {
            d->resolved = existing->module;
        }
    }
    return diagnostics.empty();
}

bool load_one(Program& program, const string& path, const string& name, DiagnosticBag& diagnostics,
              vector<string>& stack) {
    if (find_loaded(program, name) != nullptr) {
        return true;
    }
    string error;
    string bytes = slurp_file(path, &error);
    if (!error.empty()) {
        diagnostics.add("lucb.check.import", path, Span{}, error);
        return false;
    }
    LoadedModule loaded;
    loaded.path = path;
    loaded.name = name;
    loaded.source = Source::from_bytes(path, bytes, diagnostics);
    if (!loaded.source.ok()) {
        return false;
    }
    loaded.tokens = tokenize(loaded.source, diagnostics);
    if (!diagnostics.empty()) {
        return false;
    }
    ParseResult parsed = parse(loaded.source, loaded.tokens, *program.arena, diagnostics);
    if (!diagnostics.empty() || parsed.module == nullptr) {
        return false;
    }
    loaded.module = parsed.module;
    char* p = static_cast<char*>(program.arena->alloc(name.size() + 1, 1));
    memcpy(p, name.data(), name.size());
    p[name.size()] = 0;
    loaded.module->text = {p, name.size()};
    program.files.push_back(std::move(loaded));
    size_t idx = program.files.size() - 1;
    stack.push_back(name);
    bool ok = load_imports(program, idx, diagnostics, stack);
    stack.pop_back();
    return ok && diagnostics.empty();
}

} // namespace

string module_alias(string_view path) {
    size_t dot = path.rfind('.');
    if (dot == string_view::npos) {
        return string(path);
    }
    return string(path.substr(dot + 1));
}

string dotted_to_path(string_view dotted) {
    string s(dotted);
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '.') {
            s[i] = '/';
        }
    }
    return s;
}

bool parse_manifest_text(const string& text, const string& root, Manifest* out, string* error) {
    (void)error;
    if (out == nullptr) {
        return false;
    }
    out->root = root;
    out->name = "app";
    bool in_package = false;
    std::istringstream in(text);
    string line;
    while (std::getline(in, line)) {
        string t = trim(line);
        if (t.empty() || t[0] == '#') {
            continue;
        }
        if (t[0] == '[') {
            in_package = t == "[package]";
            continue;
        }
        if (!in_package) {
            continue;
        }
        size_t eq = t.find('=');
        if (eq == string::npos) {
            continue;
        }
        string key = trim(t.substr(0, eq));
        string val = unquote(trim(t.substr(eq + 1)));
        if (key == "name") {
            out->name = val;
        }
    }
    return true;
}

bool load_program(const string& path, Program& program, Arena& arena, DiagnosticBag& diagnostics) {
    program.arena = &arena;
    program.files.clear();
    string entry = path;
    if (is_dir(path)) {
        string toml = join_path(path, "luce.toml");
        if (is_file(toml)) {
            string err;
            string text = slurp_file(toml, &err);
            parse_manifest_text(text, path, &program.manifest, &err);
        } else {
            program.manifest.root = path;
            program.manifest.name = "app";
        }
        string main_path = join_path(path, "main.lucb");
        if (!is_file(main_path)) {
            main_path = join_path(join_path(path, "src"), "main.lucb");
        }
        if (!is_file(main_path)) {
            diagnostics.add("lucb.check.import", path, Span{}, "no main.lucb in this package");
            return false;
        }
        entry = main_path;
    } else {
        string toml = find_manifest(path);
        if (!toml.empty()) {
            string err;
            string text = slurp_file(toml, &err);
            parse_manifest_text(text, dirname_of(toml), &program.manifest, &err);
        } else {
            program.manifest.root = dirname_of(path);
            program.manifest.name = "app";
        }
    }
    vector<string> stack;
    string name = "main";
    // Derive a dotted name from the path relative to the package root when we can.
    if (entry.size() > program.manifest.root.size() &&
        entry.compare(0, program.manifest.root.size(), program.manifest.root) == 0) {
        string rel = entry.substr(program.manifest.root.size());
        if (!rel.empty() && rel[0] == '/') {
            rel = rel.substr(1);
        }
        if (rel.size() >= 5 && rel.compare(rel.size() - 5, 5, ".lucb") == 0) {
            rel = rel.substr(0, rel.size() - 5);
        }
        for (size_t i = 0; i < rel.size(); i++) {
            if (rel[i] == '/') {
                rel[i] = '.';
            }
        }
        if (!rel.empty()) {
            name = rel;
        }
    }
    return load_one(program, entry, name, diagnostics, stack);
}

} // namespace lucb
