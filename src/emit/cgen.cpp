//==============================================================================================
//
//   emit/cgen - C spellings
//
//   DESCRIPTION:
//       How Base names become C names: the `lb_` prefix, exported symbols kept bare, the
//       typedef names of arrays, tuples, optionals, results, and function pointers, and
//       `c_type` for every resolved type. Nothing here writes into the output; it only
//       spells.
//
//==============================================================================================

#include "emit/cgen.h"

#include "support/literal.h"

#include <cinttypes>
#include <cstdio>

namespace lucb {

// A user name that spells a runtime identifier (`span`, `str`, `error`, ...)
// gets a trailing underscore so the two never meet in the C.
static auto runtime_name(string_view name) -> bool {
    static const char* const names[] = {"span",  "str",  "error", "cspan", "out",  "fmtbuf",
                                        "hash_bytes", "hash_mix", "hash_seed", "utf8_ok",
                                        "r_str", "trap", "check_index", "unknown"};
    for (const char* n : names) {
        if (name == n) {
            return true;
        }
    }
    return false;
}

string ident(string_view prefix, string_view name) {
    string s;
    s.append(prefix.data(), prefix.size());
    s.append(name.data(), name.size());
    if (prefix == "lb_" && runtime_name(name)) {
        s.push_back('_');
    }
    return s;
}

// The module part of a declaration's C symbol: `front_token` for a declaration of the
// imported module `front.token`, empty for the entry module.
static string module_tag(const Node* decl) {
    string tag;
    if (decl == nullptr) {
        return tag;
    }
    for (size_t i = 0; i < decl->module.size(); i++) {
        char c = decl->module[i];
        tag += c == '.' ? '_' : c;
    }
    return tag;
}

string struct_ident(Node* st, string_view prefix) {
    if (st != nullptr &&
        (st->kind == NodeKind::ExternStruct || st->kind == NodeKind::ExternUnion)) {
        return string(st->text);
    }
    string tag = prefix.empty() ? module_tag(st) : string(prefix);
    if (!tag.empty()) {
        return ident("lb_", tag) + "_" + string(st->text);
    }
    return ident("lb_", st->text);
}

static string g_export_prefix;

void set_export_prefix(string_view prefix) {
    g_export_prefix = string(prefix);
}

string_view export_prefix() {
    return g_export_prefix;
}

string c_symbol(Node* fn) {
    if (fn == nullptr) {
        return "lb_unknown";
    }
    if (fn->kind == NodeKind::ExternFunc && fn->left != nullptr &&
        fn->left->kind == NodeKind::Literal) {
        string s = string(fn->left->text);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            return s.substr(1, s.size() - 2);
        }
        return s;
    }
    if (fn->kind == NodeKind::ExternFunc) {
        return string(fn->text);
    }
    if ((fn->flags & FlagExport) != 0) {
        return g_export_prefix + string(fn->text);
    }
    return {};
}

string func_ident(Node* fn, Node* owner, string_view prefix) {
    string ext = c_symbol(fn);
    if (!ext.empty() && owner == nullptr) {
        return ext;
    }
    if ((fn != nullptr && (fn->flags & FlagExport) != 0) && owner != nullptr) {
        return g_export_prefix + string(owner->text) + "_" + string(fn->text);
    }
    string tag = prefix.empty() ? module_tag(owner != nullptr ? owner : fn) : string(prefix);
    string p = tag.empty() ? string("lb_") : ident("lb_", tag) + "_";
    if (owner != nullptr) {
        return p + string(owner->text) + "_" + string(fn->text);
    }
    if (fn != nullptr && fn->text == "answer" && (fn->flags & FlagFallible) != 0) {
        return p + "answer_impl";
    }
    return p + string(fn->text);
}

string sanitize_type_name(const string& s) {
    string o;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            o += c;
        } else {
            o += '_';
        }
    }
    return o;
}

// The spelling of a type inside a generated typedef name: `type_name` with every nominal
// type qualified by its module, so that `a.Box?` and `b.Box?` get distinct typedefs.
static string mangled(const Type* t) {
    if (t == nullptr) {
        return type_name(t);
    }
    if (t->decl != nullptr && !t->decl->module.empty()) {
        return module_tag(t->decl) + "_" + type_name(t);
    }
    switch (t->kind) {
    case TypeKind::Pointer:
        return string(t->is_const ? "const " : "") + (t->is_volatile ? "volatile " : "") +
               mangled(t->elem) + "*" + (t->is_nullable ? "?" : "");
    case TypeKind::Array:
        return mangled(t->elem) + "[" + std::to_string(t->length) + "]";
    case TypeKind::Span:
        return string(t->is_const ? "const " : "") + mangled(t->elem) + "[]";
    case TypeKind::Optional:
        return mangled(t->elem) + "?";
    case TypeKind::Fallible:
        return mangled(t->elem) + "!";
    case TypeKind::Atomic:
        return "@" + mangled(t->elem);
    case TypeKind::Tuple:
    case TypeKind::Func: {
        string s = t->kind == TypeKind::Func ? "func(" : "(";
        for (int i = 0; i < t->ntargs; i++) {
            s += (i != 0 ? ", " : "") + mangled(t->args[i]);
        }
        s += ")";
        if (t->kind == TypeKind::Func) {
            s += " -> " + mangled(t->elem);
        }
        return s;
    }
    default:
        return type_name(t);
    }
}

string array_c_name(Type* t) {
    return "lb_a_" + sanitize_type_name(mangled(t));
}

string opt_c_name(Type* t) {
    Type* e = t != nullptr && is_opt(t) ? t->elem : t;
    return "lb_o_" + sanitize_type_name(mangled(e));
}

string fail_c_name(Type* t) {
    if (is_fail(t)) {
        t = t->elem;
    }
    if (t == nullptr || t->kind == TypeKind::Unit || t->kind == TypeKind::Never) {
        return "lb_r_unit";
    }
    return "lb_r_" + sanitize_type_name(mangled(t));
}

string tup_c_name(Type* t) {
    string s = "lb_t";
    if (t == nullptr) {
        return s;
    }
    for (int i = 0; i < t->ntargs; i++) {
        s += "_";
        s += sanitize_type_name(mangled(t->args[i]));
    }
    return s;
}

string fn_c_name(Type* t) {
    string s = "lb_fn";
    if (t == nullptr) {
        return s;
    }
    for (int i = 0; i < t->ntargs; i++) {
        s += "_";
        s += sanitize_type_name(mangled(t->args[i]));
    }
    s += "_to_";
    Type* r = t->elem;
    if (r == nullptr || r->kind == TypeKind::Unit || r->kind == TypeKind::Never) {
        s += "unit";
    } else {
        s += sanitize_type_name(mangled(r));
    }
    return s;
}

// True for the checker-synthesized records of the standard modules; a user
// struct that happens to be named `Location` keeps its own C definition.
static bool builtin_decl(Type* t) {
    return t->decl == nullptr || (t->decl->flags & FlagBuiltin) != 0;
}

string c_type(Type* t) {
    if (t == nullptr) {
        return "void";
    }
    if (t->kind == TypeKind::Struct && t->name == "FixedBuffer" && builtin_decl(t)) {
        return "lb_fixed";
    }
    if (t->kind == TypeKind::Struct && t->name == "Location" && builtin_decl(t)) {
        return "lb_Location";
    }
    if (t->kind == TypeKind::Struct && t->name == "Handle" && builtin_decl(t)) {
        return "lb_Handle";
    }
    if (t->kind == TypeKind::Struct && t->name == "Mutex" && builtin_decl(t)) {
        return "lb_Mutex";
    }
    if (t->kind == TypeKind::Struct && t->name == "Condition" && builtin_decl(t)) {
        return "lb_Cond";
    }
    if (t->kind == TypeKind::Struct && t->name == "Once" && builtin_decl(t)) {
        return "lb_Once";
    }
    if (t->kind == TypeKind::Struct && t->name == "Semaphore" && builtin_decl(t)) {
        return "lb_Sem";
    }
    if (t->kind == TypeKind::Atomic) {
        return "_Atomic(" + c_type(t->elem) + ")";
    }
    if (t->kind == TypeKind::Tuple) {
        return tup_c_name(t);
    }
    if (t->kind == TypeKind::Func) {
        return fn_c_name(t);
    }
    if (t->kind == TypeKind::Struct || t->kind == TypeKind::Union ||
        (t->kind == TypeKind::Enum && !is_int_enum(t))) {
        if (t->decl != nullptr) {
            return struct_ident(t->decl);
        }
        return ident("lb_", t->name);
    }
    if (is_int_enum(t)) {
        return c_type(t->elem);
    }
    if (t->kind == TypeKind::Array) {
        return array_c_name(t);
    }
    if (t->kind == TypeKind::Pointer) {
        if (t->elem != nullptr && t->elem->kind == TypeKind::Void) {
            return "void*";
        }
        string q;
        if (t->is_const) {
            q += "const ";
        }
        if (t->is_volatile) {
            q += "volatile ";
        }
        Type* e = t->elem;
        if (e != nullptr && (e->kind == TypeKind::Struct || e->kind == TypeKind::Union ||
                             (e->kind == TypeKind::Enum && !is_int_enum(e)))) {
            const char* tag = e->kind == TypeKind::Union ? "union " : "struct ";
            return q + tag + c_type(e) + "*";
        }
        return q + c_type(e) + "*";
    }
    if (is_opt(t)) {
        Type* e = t->elem;
        if (is_span(e) && e->elem != nullptr && e->elem->kind == TypeKind::U8 && !e->is_const) {
            return "lb_span_opt";
        }
        return opt_c_name(t);
    }
    if (is_fail(t)) {
        return fail_c_name(t);
    }
    return c_type_name(t);
}

uint64_t emit_case_int(Node* en, Node* cse) {
    uint64_t next = 0;
    if (en == nullptr) {
        return 0;
    }
    for (Node* m = en->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::EnumCase) {
            continue;
        }
        uint64_t v = next;
        if (m->left != nullptr && m->left->kind == NodeKind::Literal &&
            m->left->op == TokenKind::IntLit) {
            ParsedInt p = parse_int_literal(m->left->text);
            if (p.ok) {
                v = p.value;
            }
        }
        if (m == cse) {
            return v;
        }
        next = v + 1;
    }
    return 0;
}

int emit_case_tag(Node* en, Node* cse) {
    int i = 0;
    if (en == nullptr) {
        return 0;
    }
    for (Node* m = en->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::EnumCase) {
            continue;
        }
        if (m == cse) {
            return i;
        }
        i++;
    }
    return 0;
}

string fn_c_ret(Node* fn) {
    if (fn != nullptr && (fn->flags & FlagFallible) != 0) {
        return fail_c_name(fn->ty);
    }
    if (fn != nullptr && fn->ty != nullptr && fn->ty->kind == TypeKind::Unit) {
        return "void";
    }
    return c_type(fn != nullptr ? fn->ty : nullptr);
}

string word_cast(Type* t, const string& e) {
    if (t != nullptr && is_unsigned_int(t)) {
        return "(uint64_t)(" + e + ")";
    }
    return "(int64_t)(" + e + ")";
}

string bits_lit(Type* t) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", int_bits(t));
    return buf;
}

string down_cast(Type* t, const string& e) {
    return "(" + c_type(t) + ")(" + e + ")";
}

string c_escape(string_view s) {
    string out = "\"";
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\\' || c == '"') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

string decode_lit(string_view tok) {
    return decode_string_literal(tok);
}

} // namespace lucb
