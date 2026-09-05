#include "emit/cgen.h"

#include "support/literal.h"

#include <cinttypes>
#include <cstdio>

namespace lucb {

string ident(string_view prefix, string_view name) {
    string s;
    s.append(prefix.data(), prefix.size());
    s.append(name.data(), name.size());
    return s;
}

string struct_ident(Node* st, string_view prefix) {
    if (st != nullptr &&
        (st->kind == NodeKind::ExternStruct || st->kind == NodeKind::ExternUnion)) {
        return string(st->text);
    }
    if (!prefix.empty()) {
        return ident("lb_", prefix) + "_" + string(st->text);
    }
    return ident("lb_", st->text);
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
        return string(fn->text);
    }
    return {};
}

string func_ident(Node* fn, Node* owner, string_view prefix) {
    string ext = c_symbol(fn);
    if (!ext.empty() && owner == nullptr) {
        return ext;
    }
    if ((fn != nullptr && (fn->flags & FlagExport) != 0) && owner != nullptr) {
        return string(owner->text) + "_" + string(fn->text);
    }
    string p = prefix.empty() ? string() : ident("lb_", prefix) + "_";
    if (p.empty()) {
        p = "lb_";
    }
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

string array_c_name(Type* t) {
    return "lb_a_" + sanitize_type_name(type_name(t));
}

string opt_c_name(Type* t) {
    Type* e = t != nullptr && is_opt(t) ? t->elem : t;
    return "lb_o_" + sanitize_type_name(type_name(e));
}

string fail_c_name(Type* t) {
    if (is_fail(t)) {
        t = t->elem;
    }
    if (t == nullptr || t->kind == TypeKind::Unit || t->kind == TypeKind::Never) {
        return "lb_r_unit";
    }
    return "lb_r_" + sanitize_type_name(type_name(t));
}

string c_type(Type* t) {
    if (t == nullptr) {
        return "void";
    }
    if (t->kind == TypeKind::Struct && t->name == "FixedBuffer") {
        return "lb_fixed";
    }
    if (t->kind == TypeKind::Struct && t->name == "Location") {
        return "lb_Location";
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
        return q + c_type(t->elem) + "*";
    }
    if (is_opt(t)) {
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
    if (tok.size() >= 2 && tok[0] == '"') {
        tok = tok.substr(1, tok.size() - 2);
    }
    string out;
    for (size_t i = 0; i < tok.size(); i++) {
        if (tok[i] == '\\' && i + 1 < tok.size()) {
            char e = tok[i + 1];
            if (e == 'n') {
                out += '\n';
            } else if (e == 't') {
                out += '\t';
            } else {
                out += e;
            }
            i++;
        } else {
            out += tok[i];
        }
    }
    return out;
}

} // namespace lucb
