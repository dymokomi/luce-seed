#include "emit/emit.h"

#include "check/type.h"
#include "support/literal.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

namespace lucb {
namespace {

string ident(string_view prefix, string_view name) {
    string s;
    s.append(prefix.data(), prefix.size());
    s.append(name.data(), name.size());
    return s;
}

string struct_ident(Node* st, string_view prefix = {}) {
    if (!prefix.empty()) {
        return ident("lb_", prefix) + "_" + string(st->text);
    }
    return ident("lb_", st->text);
}

string func_ident(Node* fn, Node* owner, string_view prefix = {}) {
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

struct Emitter {
    string out;
    int indent = 0;
    vector<Type*> arrays;
    vector<Type*> opts;
    vector<Type*> fails;
    Node* current_fn = nullptr;
    int temps = 0;
    string catch_var;

    struct Scope {
        vector<Node*> defers;
        bool loop = false;
        string_view label;
        bool restore_alloc = false;
        string alloc_save;
    };
    vector<Scope> scopes;

    void pad() {
        for (int i = 0; i < indent; i++) {
            out += "    ";
        }
    }

    void line(const string& s) {
        pad();
        out += s;
        out += '\n';
    }

    int tmp() { return temps++; }

    bool produces_opt(Node* n) {
        if (n == nullptr || !is_opt(n->ty)) {
            return false;
        }
        if (n->kind == NodeKind::Group) {
            return produces_opt(n->left);
        }
        if (n->kind == NodeKind::Literal && n->op == TokenKind::KwNone) {
            return true;
        }
        if (n->kind == NodeKind::Binary &&
            (n->op == TokenKind::PlusQuestion || n->op == TokenKind::MinusQuestion ||
             n->op == TokenKind::StarQuestion)) {
            return true;
        }
        if ((n->kind == NodeKind::Name || n->kind == NodeKind::Member ||
             n->kind == NodeKind::Call) &&
            n->resolved != nullptr && is_opt(n->resolved->ty)) {
            return true;
        }
        return false;
    }

    string wrap_opt(Type* t, const string& e) {
        Type* elem = t != nullptr ? t->elem : nullptr;
        return "((" + opt_c_name(t) + "){ .value = (" + c_type(elem) + ")(" + e +
               "), .present = true })";
    }

    string none_opt(Type* t) { return "((" + opt_c_name(t) + "){ .present = false })"; }

    bool fn_fallible() {
        return current_fn != nullptr && (current_fn->flags & FlagFallible) != 0;
    }

    string wrap_ok(const string& e) {
        Type* t = current_fn != nullptr ? current_fn->ty : nullptr;
        string rty = fail_c_name(t);
        if (t == nullptr || t->kind == TypeKind::Unit) {
            return "((" + rty + "){ .failed = false })";
        }
        return "((" + rty + "){ .value = (" + e + "), .failed = false })";
    }

    string wrap_err(const string& code, const string& msg) {
        string rty = fail_c_name(current_fn != nullptr ? current_fn->ty : nullptr);
        return "((" + rty + "){ .error = { .code = (int32_t)(" + code + "), .message = " + msg +
               " }, .failed = true })";
    }

    void run_defers(const vector<Node*>& d) {
        for (int i = static_cast<int>(d.size()) - 1; i >= 0; i--) {
            Node* dn = d[static_cast<size_t>(i)];
            if (dn->kind == NodeKind::Errdefer) {
                continue;
            }
            line(emit_expr(dn->left) + ";");
        }
    }

    void unwind_scope(const Scope& sc) {
        run_defers(sc.defers);
        if (sc.restore_alloc) {
            line("lb_set_alloc(" + sc.alloc_save + ");");
        }
    }

    void run_defers_from(int from) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= from; i--) {
            unwind_scope(scopes[static_cast<size_t>(i)]);
        }
    }

    string snapshot_defers() {
        string saved = out;
        int saved_indent = indent;
        out = {};
        indent = 0;
        run_defers_from(0);
        string s = out;
        out = saved;
        indent = saved_indent;
        return s;
    }

    bool is_error_call(Node* n) {
        return n != nullptr && n->kind == NodeKind::Call && n->left != nullptr &&
               n->left->kind == NodeKind::Name && n->left->text == "error";
    }

    string emit_enum_value(Node* n) {
        Node* cse = n->resolved;
        Type* t = n->ty;
        if (cse == nullptr) {
            return "0";
        }
        if (is_int_enum(t)) {
            uint64_t v = emit_case_int(t->decl, cse);
            return "((" + c_type(t) + ")" + std::to_string(v) + "u)";
        }
        int tag = emit_case_tag(t != nullptr ? t->decl : nullptr, cse);
        string s = "((" + c_type(t) + "){ .tag = " + std::to_string(tag);
        if (n->body != nullptr && cse->body != nullptr) {
            s += ", .u." + string(cse->text) + " = {";
            bool first = true;
            Node* p = cse->body;
            Node* a = n->body;
            while (p != nullptr && a != nullptr) {
                if (!first) {
                    s += ", ";
                }
                first = false;
                s += "." + string(p->text) + " = " + emit_expr(a->left);
                p = p->next;
                a = a->next;
            }
            s += "}";
        }
        s += " })";
        return s;
    }

    string emit_try(Node* n) {
        int id = tmp();
        string rn = "_lb_r" + std::to_string(id);
        Type* ft = n->left != nullptr ? n->left->ty : nullptr;
        string rty = fail_c_name(ft);
        Type* payload = is_fail(ft) ? ft->elem : ft;
        string s = "({ ";
        s += rty + " " + rn + " = " + emit_expr(n->left) + "; ";
        s += "if (" + rn + ".failed) { " + snapshot_defers();
        string fnr = fn_c_ret(current_fn);
        if (fn_fallible() && fnr != rty) {
            s += "return ((" + fnr + "){ .error = " + rn + ".error, .failed = true }); } ";
        } else {
            s += "return " + rn + "; } ";
        }
        if (payload == nullptr || payload->kind == TypeKind::Unit) {
            s += "(void)0; })";
        } else {
            s += rn + ".value; })";
        }
        return s;
    }

    string emit_else(Node* n) {
        int id = tmp();
        string on = "_lb_o" + std::to_string(id);
        Type* lt = n->left != nullptr ? n->left->ty : nullptr;
        string s = "({ ";
        s += c_type(lt) + " " + on + " = " + emit_expr(n->left) + "; ";
        if (is_opt(lt)) {
            s += on + ".present ? " + on + ".value : (" + emit_expr(n->right) + "); })";
        } else {
            s += on + " ? " + on + " : (" + emit_expr(n->right) + "); })";
        }
        return s;
    }

    string emit_catch(Node* n) {
        int id = tmp();
        string rn = "_lb_r" + std::to_string(id);
        string vn = "_lb_v" + std::to_string(id);
        Type* ft = n->left != nullptr ? n->left->ty : nullptr;
        string rty = fail_c_name(ft);
        string vty = c_type(n->ty);
        if (vty == "void") {
            vty = "int";
        }
        string saved_catch = catch_var;
        string saved_out = out;
        int saved_indent = indent;
        catch_var = vn;
        out = {};
        indent = 0;
        emit_stmt(n->body);
        string body = out;
        out = saved_out;
        indent = saved_indent;
        catch_var = saved_catch;
        Type* payload = is_fail(ft) ? ft->elem : nullptr;
        string s = "({ ";
        s += rty + " " + rn + " = " + emit_expr(n->left) + "; ";
        s += vty + " " + vn + "; ";
        s += "if (" + rn + ".failed) { ";
        if (!n->text.empty()) {
            s += "lb_error " + ident("lb_", n->text) + " = " + rn + ".error; ";
        }
        s += body;
        s += " } else { ";
        if (payload != nullptr && payload->kind != TypeKind::Unit) {
            s += vn + " = " + rn + ".value; ";
        }
        s += "} ";
        s += vn + "; })";
        return s;
    }

    string emit_expr(Node* n) {
        if (n == nullptr) {
            return "0";
        }
        string e = emit_expr_inner(n);
        if (is_opt(n->ty) && !produces_opt(n)) {
            return wrap_opt(n->ty, e);
        }
        return e;
    }

    string emit_expr_inner(Node* n) {
        switch (n->kind) {
        case NodeKind::Literal:
            return emit_literal(n);
        case NodeKind::Name:
            if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
                return emit_enum_value(n);
            }
            if (is_span(n->ty) && n->resolved != nullptr && is_array(n->resolved->ty)) {
                string nm = ident("lb_", n->text);
                char nbuf[32];
                snprintf(nbuf, sizeof(nbuf), "%lluULL",
                         static_cast<unsigned long long>(n->resolved->ty->length));
                string sty = c_type(n->ty);
                return "((" + sty + "){" + nm + ".d, " + nbuf + "})";
            }
            return ident("lb_", n->text);
        case NodeKind::Self:
            return "(*self)";
        case NodeKind::Group:
            return "(" + emit_expr(n->left) + ")";
        case NodeKind::Unit:
            return "((void)0)";
        case NodeKind::Unary:
            return emit_unary(n);
        case NodeKind::Binary:
            return emit_binary(n);
        case NodeKind::Call:
            return emit_call(n);
        case NodeKind::Member:
            if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
                return emit_enum_value(n);
            }
            return emit_member(n);
        case NodeKind::Conditional:
            return "(" + emit_expr(n->type) + " ? " + emit_expr(n->left) + " : " +
                   emit_expr(n->right) + ")";
        case NodeKind::Cast:
            return emit_conv(n->left, n->ty, false);
        case NodeKind::Index:
            return emit_index(n);
        case NodeKind::Slice:
            return emit_slice(n);
        case NodeKind::ArrayLit:
            return emit_array_lit(n);
        case NodeKind::SpanMake:
            return emit_span_make(n);
        case NodeKind::CaseValue:
            return emit_enum_value(n);
        case NodeKind::Else:
            return emit_else(n);
        case NodeKind::Catch:
            return emit_catch(n);
        case NodeKind::New:
            return emit_new(n);
        case NodeKind::Alloc:
            return emit_alloc(n);
        case NodeKind::Match:
        case NodeKind::MatchExpr:
            return "/* match-expr */ 0";
        default:
            return "/* unsupported expr */ 0";
        }
    }

    string emit_literal(Node* n) {
        if (n->op == TokenKind::KwTrue) {
            return "true";
        }
        if (n->op == TokenKind::KwFalse) {
            return "false";
        }
        if (n->op == TokenKind::StringLit) {
            string d = decode_lit(n->text);
            char buf[32];
            snprintf(buf, sizeof(buf), "%zu", d.size());
            return "((lb_str){" + c_escape(d) + ", " + buf + "})";
        }
        if (n->op == TokenKind::KwNone) {
            if (is_opt(n->ty)) {
                return none_opt(n->ty);
            }
            return "((void*)0)";
        }
        if (n->op == TokenKind::CharLit) {
            uint32_t cp = 0;
            parse_char_literal(n->text, &cp);
            char buf[32];
            snprintf(buf, sizeof(buf), "%uu", cp);
            return buf;
        }
        if (n->op == TokenKind::FloatLit) {
            ParsedFloat p = parse_float_literal(n->text);
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", p.value);
            if (n->ty != nullptr && n->ty->kind == TypeKind::F32) {
                return string(buf) + "f";
            }
            return buf;
        }
        ParsedInt p = parse_int_literal(n->text);
        Type* t = n->ty;
        char buf[48];
        if (t != nullptr && is_unsigned_int(t)) {
            snprintf(buf, sizeof(buf), "%" PRIu64 "ULL", p.value);
        } else {
            snprintf(buf, sizeof(buf), "%" PRId64 "LL", static_cast<int64_t>(p.value));
        }
        return buf;
    }

    string emit_unary(Node* n) {
        if (n->op == TokenKind::KwTry) {
            return emit_try(n);
        }
        if (n->op == TokenKind::KwNot) {
            return "(!" + emit_expr(n->left) + ")";
        }
        if (n->op == TokenKind::Amp) {
            return emit_addr(n->left);
        }
        if (n->op == TokenKind::Star) {
            return "(*(" + emit_expr(n->left) + "))";
        }
        if (n->op == TokenKind::Plus) {
            return emit_expr(n->left);
        }
        Type* t = n->ty;
        string x = emit_expr(n->left);
        if (n->op == TokenKind::Tilde) {
            if (t != nullptr && is_unsigned_int(t)) {
                return down_cast(t, "lb_not_u(" + word_cast(t, x) + ", " + bits_lit(t) + ")");
            }
            return down_cast(t, "~(" + x + ")");
        }
        if (is_float(t)) {
            return "(-(" + x + "))";
        }
        if (n->op == TokenKind::MinusPercent) {
            const char* h = is_signed_int(t) ? "lb_negw_s" : "lb_negw_u";
            return down_cast(t, string(h) + "(" + word_cast(t, x) + ", " + bits_lit(t) + ")");
        }
        if (n->op == TokenKind::Minus) {
            if (n->left != nullptr && n->left->kind == NodeKind::Literal &&
                n->left->op == TokenKind::IntLit) {
                ParsedInt p = parse_int_literal(n->left->text);
                if (p.ok && t != nullptr &&
                    p.value == static_cast<uint64_t>(int_max_signed(int_bits(t))) + 1) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "(int64_t)(1ULL << %d)", int_bits(t) - 1);
                    return down_cast(t, buf);
                }
            }
            return down_cast(t, "lb_neg_s(" + word_cast(t, x) + ", " + bits_lit(t) + ")");
        }
        return "0";
    }

    string emit_helper(const char* name, Type* t, const string& L, const string& R) {
        string h = "lb_";
        h += name;
        h += is_signed_int(t) ? "_s(" : "_u(";
        h += word_cast(t, L);
        h += ", ";
        h += word_cast(t, R);
        h += ", ";
        h += bits_lit(t);
        h += ")";
        return down_cast(t, h);
    }

    string emit_binary(Node* n) {
        string L = emit_expr(n->left);
        string R = emit_expr(n->right);
        TokenKind op = n->op;
        if (op == TokenKind::KwAnd) {
            return "(" + L + " && " + R + ")";
        }
        if (op == TokenKind::KwOr) {
            return "(" + L + " || " + R + ")";
        }
        Type* t = n->ty;
        Type* result = t;
        if (is_opt(t) && op != TokenKind::PlusQuestion && op != TokenKind::MinusQuestion &&
            op != TokenKind::StarQuestion) {
            t = t->elem;
        }
        Type* lt = n->left != nullptr ? n->left->ty : nullptr;
        Type* rt = n->right != nullptr ? n->right->ty : nullptr;
        if (is_ptr(lt) || is_ptr(rt)) {
            if (op == TokenKind::EqEq) {
                return "(" + L + " == " + R + ")";
            }
            if (op == TokenKind::NotEq) {
                return "(" + L + " != " + R + ")";
            }
            if (op == TokenKind::Plus) {
                return "(" + L + " + (ptrdiff_t)(" + R + "))";
            }
            if (op == TokenKind::Minus && is_ptr(rt)) {
                return "((intptr_t)(" + L + " - " + R + "))";
            }
            if (op == TokenKind::Minus) {
                return "(" + L + " - (ptrdiff_t)(" + R + "))";
            }
            const char* cop = op == TokenKind::Lt    ? "<"
                              : op == TokenKind::LtEq ? "<="
                              : op == TokenKind::Gt   ? ">"
                                                      : ">=";
            return "(" + L + " " + cop + " " + R + ")";
        }
        Type* ct = n->left != nullptr && n->left->ty != nullptr ? n->left->ty : t;
        if (op == TokenKind::EqEq) {
            return "(" + L + " == " + R + ")";
        }
        if (op == TokenKind::NotEq) {
            return "(" + L + " != " + R + ")";
        }
        if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
            op == TokenKind::GtEq) {
            string cty = ct != nullptr ? c_type(ct) : "int64_t";
            string a = "((" + cty + ")(" + L + "))";
            string b = "((" + cty + ")(" + R + "))";
            const char* cop = op == TokenKind::Lt    ? "<"
                              : op == TokenKind::LtEq ? "<="
                              : op == TokenKind::Gt   ? ">"
                                                      : ">=";
            return "(" + a + " " + cop + " " + b + ")";
        }
        if (is_float(t)) {
            const char* cop = op == TokenKind::Plus    ? "+"
                              : op == TokenKind::Minus ? "-"
                              : op == TokenKind::Star  ? "*"
                                                       : "/";
            return "(" + L + " " + cop + " " + R + ")";
        }
        if (op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret) {
            const char* cop = op == TokenKind::Amp ? "&" : op == TokenKind::Pipe ? "|" : "^";
            return down_cast(t, "(" + L + " " + cop + " " + R + ")");
        }
        const char* helper = nullptr;
        if (op == TokenKind::Plus) {
            helper = "add";
        } else if (op == TokenKind::Minus) {
            helper = "sub";
        } else if (op == TokenKind::Star) {
            helper = "mul";
        } else if (op == TokenKind::SlashSlash) {
            helper = "div";
        } else if (op == TokenKind::Percent) {
            helper = "mod";
        } else if (op == TokenKind::PlusPercent) {
            helper = "addw";
        } else if (op == TokenKind::MinusPercent) {
            helper = "subw";
        } else if (op == TokenKind::StarPercent) {
            helper = "mulw";
        } else if (op == TokenKind::PlusPipe) {
            helper = "adds";
        } else if (op == TokenKind::MinusPipe) {
            helper = "subs";
        } else if (op == TokenKind::StarPipe) {
            helper = "muls";
        } else if (op == TokenKind::LtLt) {
            helper = "shl";
        } else if (op == TokenKind::GtGt) {
            helper = "shr";
        } else if (op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
                   op == TokenKind::StarQuestion) {
            Type* et = result != nullptr && is_opt(result) ? result->elem : result;
            const char* q = op == TokenKind::PlusQuestion    ? "qadd"
                            : op == TokenKind::MinusQuestion ? "qsub"
                                                             : "qmul";
            string h = "lb_";
            h += q;
            h += is_signed_int(et) ? "_s" : "_u";
            int id = tmp();
            string o = "_lb_qo" + std::to_string(id);
            string r = "_lb_qr" + std::to_string(id);
            string oty = is_signed_int(et) ? "int64_t" : "uint64_t";
            string s = "({ ";
            s += oty + " " + o + "; ";
            s += opt_c_name(result) + " " + r + "; ";
            s += r + ".present = " + h + "(" + word_cast(et, L) + ", " + word_cast(et, R) + ", " +
                 bits_lit(et) + ", &" + o + "); ";
            s += r + ".value = (" + c_type(et) + ")(" + o + "); ";
            s += r + "; })";
            return s;
        }
        if (helper != nullptr) {
            return emit_helper(helper, t, L, R);
        }
        return "0";
    }

    string emit_enum_check(Type* dest, const string& e) {
        string s = "({ " + c_type(dest) + " _lb_e = (" + c_type(dest) + ")(" + e + "); ";
        s += "if (!(";
        bool first = true;
        uint64_t next = 0;
        Node* en = dest != nullptr ? dest->decl : nullptr;
        for (Node* c = en != nullptr ? en->body : nullptr; c != nullptr; c = c->next) {
            if (c->kind != NodeKind::EnumCase) {
                continue;
            }
            uint64_t v = next;
            if (c->left != nullptr && c->left->kind == NodeKind::Literal) {
                ParsedInt p = parse_int_literal(c->left->text);
                if (p.ok) {
                    v = p.value;
                }
            }
            if (!first) {
                s += " || ";
            }
            first = false;
            s += "_lb_e == " + std::to_string(v) + "u";
            next = v + 1;
        }
        if (first) {
            s += "0";
        }
        s += ")) lb_trap(\"invalid enum value\"); _lb_e; })";
        return s;
    }

    string emit_conv(Node* src, Type* dest, bool checked) {
        string e = emit_expr(src);
        Type* st = src != nullptr ? src->ty : nullptr;
        if (dest == nullptr) {
            return e;
        }
        if (is_int_enum(dest)) {
            if (checked) {
                return emit_enum_check(dest, e);
            }
            return "(" + c_type(dest) + ")(" + e + ")";
        }
        if (is_int_enum(st) && is_int(dest)) {
            return down_cast(dest, e);
        }
        int mode = checked ? 0 : 1;
        if (is_float(st) && is_int(dest)) {
            const char* h = is_signed_int(dest) ? "lb_f_to_s" : "lb_f_to_u";
            string call = string(h) + "((double)(" + e + "), " + bits_lit(dest) + ", " +
                          std::to_string(mode) + ")";
            return down_cast(dest, call);
        }
        if (is_int(st) && is_float(dest)) {
            string w = is_signed_int(st) ? "(int64_t)(" + e + ")" : "(uint64_t)(" + e + ")";
            string call = "lb_to_f(" + w + ", " + (is_signed_int(st) ? "1" : "0") + ")";
            if (dest->kind == TypeKind::F32) {
                return "(float)" + call;
            }
            return call;
        }
        if (is_float(st) && is_float(dest)) {
            return "(" + string(c_type_name(dest)) + ")(" + e + ")";
        }
        if ((is_int(st) || (st != nullptr && st->kind == TypeKind::Char)) &&
            (is_int(dest) || dest->kind == TypeKind::Char)) {
            Type* from = st;
            int fb = from->kind == TypeKind::Char ? 32 : int_bits(from);
            int tb = dest->kind == TypeKind::Char ? 32 : int_bits(dest);
            int fs = from->kind == TypeKind::Char ? 0 : (is_signed_int(from) ? 1 : 0);
            int ts = dest->kind == TypeKind::Char ? 0 : (is_signed_int(dest) ? 1 : 0);
            const char* h = fs ? "lb_conv_s" : "lb_conv_u";
            string call = string(h) + "(" + word_cast(from, e) + ", " + std::to_string(fb) + ", " +
                          std::to_string(fs) + ", " + std::to_string(tb) + ", " +
                          std::to_string(ts) + ", " + std::to_string(mode) + ")";
            if (dest->kind == TypeKind::Char) {
                return "(uint32_t)(" + call + ")";
            }
            return down_cast(dest, call);
        }
        return "(" + c_type(dest) + ")(" + e + ")";
    }

    string emit_member(Node* n) {
        Type* ot = n->left != nullptr ? n->left->ty : nullptr;
        if (ot != nullptr && ot->kind == TypeKind::Module) {
            if (n->text == "allocator") {
                return "lb_get_alloc()";
            }
            if (n->text == "heap") {
                return "lb_heap_alloc()";
            }
            if (n->text == "exhausted") {
                return "((int32_t)LB_MEMORY_EXHAUSTED)";
            }
        }
        bool ptr = is_ptr(ot);
        Type* raw = ptr && ot != nullptr ? ot->elem : ot;
        string base = emit_expr(n->left);
        string acc = ptr ? "->" : ".";
        if (n->text == "length") {
            if (raw != nullptr && (raw->kind == TypeKind::Str || is_span(raw))) {
                return "(" + base + (ptr ? "->" : ".") + "length)";
            }
            if (is_array(raw) && raw != nullptr) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%lluULL", static_cast<unsigned long long>(raw->length));
                return buf;
            }
        }
        if (n->text == "data") {
            if (is_span(raw) || (raw != nullptr && raw->kind == TypeKind::Str)) {
                string d = "(" + base + acc + "data)";
                if (n->ty != nullptr) {
                    return "((" + c_type(n->ty) + ")(" + d + "))";
                }
                return d;
            }
        }
        if (n->text == "bytes") {
            return "((lb_cspan){" + base + acc + "data, " + base + acc + "length})";
        }
        if (n->left != nullptr && n->left->kind == NodeKind::Self) {
            return "self->" + string(n->text);
        }
        if (ptr) {
            return "(" + base + ")->" + string(n->text);
        }
        return base + "." + string(n->text);
    }

    string emit_index(Node* n) {
        Type* bt = n->left != nullptr ? n->left->ty : nullptr;
        string b = emit_expr(n->left);
        string i = emit_expr(n->body);
        if (is_array(bt)) {
            char nbuf[32];
            snprintf(nbuf, sizeof(nbuf), "%lluULL",
                     static_cast<unsigned long long>(bt->length));
            return "((" + b + ").d[(lb_check_index((uint64_t)(" + i + "), " + nbuf + "), " + i +
                   ")])";
        }
        if (is_span(bt) || (bt != nullptr && bt->kind == TypeKind::Str)) {
            Type* et = is_span(bt) ? bt->elem : n->ty;
            if (bt != nullptr && bt->kind == TypeKind::Str) {
                et = nullptr;
            }
            string elem = et != nullptr ? c_type(et) : "uint8_t";
            return "(((" + elem + "*)" + b + ".data)[(lb_check_index((uint64_t)(" + i + "), " + b +
                   ".length), " + i + ")])";
        }
        if (is_ptr(bt)) {
            return "((" + b + ")[" + i + "])";
        }
        return "0";
    }

    string emit_slice(Node* n) {
        Type* bt = n->left != nullptr ? n->left->ty : nullptr;
        string b = emit_expr(n->left);
        string start = n->body != nullptr ? emit_expr(n->body) : "0";
        string end;
        string len;
        string data;
        if (is_array(bt)) {
            char nbuf[32];
            snprintf(nbuf, sizeof(nbuf), "%lluULL",
                     static_cast<unsigned long long>(bt->length));
            len = nbuf;
            data = b + ".d";
        } else {
            len = b + ".length";
            data = b + ".data";
        }
        end = n->right != nullptr ? emit_expr(n->right) : len;
        string span_ty = n->ty != nullptr && n->ty->is_const ? "lb_cspan" : "lb_span";
        return "((void)lb_check_index((uint64_t)(" + start + "), (uint64_t)(" + len +
               ") + 1), (void)lb_check_index((uint64_t)(" + end + "), (uint64_t)(" + len +
               ") + 1), (" + span_ty + "){(void*)((" + data + ") + (" + start + ")), (size_t)((" +
               end + ") - (" + start + "))})";
    }

    string emit_array_lit(Node* n) {
        string s = "(" + c_type(n->ty) + "){{";
        bool first = true;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            if (!first) {
                s += ", ";
            }
            first = false;
            s += emit_expr(e);
        }
        s += "}}";
        return s;
    }

    string emit_span_make(Node* n) {
        string p = n->body != nullptr ? emit_expr(n->body->left) : "0";
        string len =
            n->body != nullptr && n->body->next != nullptr ? emit_expr(n->body->next->left) : "0";
        return "((lb_span){" + p + ", (size_t)(" + len + ")})";
    }

    string emit_addr(Node* n) {
        if (n == nullptr) {
            return "NULL";
        }
        if (n->kind == NodeKind::Self) {
            return "self";
        }
        if (n->kind == NodeKind::Name) {
            if (n->ty != nullptr && is_array(n->ty)) {
                return ident("lb_", n->text) + ".d";
            }
            return "&" + ident("lb_", n->text);
        }
        if (n->kind == NodeKind::Member) {
            return "&(" + emit_member(n) + ")";
        }
        if (n->kind == NodeKind::Index) {
            return "&(" + emit_index(n) + ")";
        }
        if (n->kind == NodeKind::Unary && n->op == TokenKind::Star) {
            return emit_expr(n->left);
        }
        return "&(" + emit_expr(n) + ")";
    }

    string emit_args(Node* args) {
        string s;
        bool first = true;
        for (Node* a = args; a != nullptr; a = a->next) {
            if (!first) {
                s += ", ";
            }
            first = false;
            s += emit_expr(a->left);
        }
        return s;
    }

    string emit_call(Node* n) {
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "CAllocator") {
            return "lb_heap_alloc()";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "assert") {
            Node* cond = n->body != nullptr ? n->body->left : nullptr;
            string msg = "\"assert failed\"";
            if (n->body != nullptr && n->body->next != nullptr) {
                msg = "(" + emit_expr(n->body->next->left) + ").data";
            }
            return "((void)((" + emit_expr(cond) + ") ? 0 : (lb_trap(" + msg + "), 0)))";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            Type* t = arg != nullptr ? arg->ty : nullptr;
            string e = emit_expr(arg);
            if (t != nullptr && t->kind == TypeKind::Bool) {
                return "lb_print_bool(" + e + ")";
            }
            if (t != nullptr && t->kind == TypeKind::Str) {
                return "lb_print_str(" + e + ")";
            }
            if (is_float(t)) {
                return "lb_print_f64((double)(" + e + "))";
            }
            if (t != nullptr && is_unsigned_int(t)) {
                return "lb_print_u64((uint64_t)(" + e + "))";
            }
            return "lb_print_i64((int64_t)(" + e + "))";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            return "lb_trap((" + emit_expr(arg) + ").data)";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "error") {
            Node* code = n->body != nullptr ? n->body->left : nullptr;
            Node* msg = n->body != nullptr && n->body->next != nullptr ? n->body->next->left
                                                                      : nullptr;
            return wrap_err(emit_expr(code), emit_expr(msg));
        }
        if (callee != nullptr && callee->kind == NodeKind::Name &&
            (callee->text == "sizeof" || callee->text == "alignof")) {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            Type* t = arg != nullptr ? arg->ty : nullptr;
            string ty = c_type(t);
            if (callee->text == "sizeof") {
                return "((size_t)sizeof(" + ty + "))";
            }
            return "((size_t)_Alignof(" + ty + "))";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "offsetof") {
            Node* tyarg = n->body != nullptr ? n->body->left : nullptr;
            Node* field = n->body != nullptr && n->body->next != nullptr ? n->body->next->left
                                                                        : nullptr;
            Type* t = tyarg != nullptr ? tyarg->ty : nullptr;
            string ty = c_type(t);
            string f = field != nullptr ? string(field->text) : "x";
            return "((size_t)offsetof(" + ty + ", " + f + "))";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && n->body != nullptr &&
            n->ty != nullptr &&
            (n->resolved == nullptr ||
             (n->resolved->kind == NodeKind::Enum && is_int_enum(n->ty))) &&
            (is_int(n->ty) || is_float(n->ty) || n->ty->kind == TypeKind::Char ||
             is_int_enum(n->ty))) {
            return emit_conv(n->body->left, n->ty, true);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
            return emit_ctor(n, n->resolved);
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            if (callee->resolved != nullptr && callee->resolved->kind == NodeKind::EnumCase) {
                return emit_enum_value(n);
            }
            Type* lt = callee->left != nullptr ? callee->left->ty : nullptr;
            if (lt != nullptr && lt->kind == TypeKind::Module && n->resolved != nullptr &&
                n->resolved->kind == NodeKind::Func) {
                return func_ident(n->resolved, nullptr) + "(" + emit_args(n->body) + ")";
            }
            if (lt != nullptr && lt->kind == TypeKind::Module && n->resolved != nullptr &&
                n->resolved->kind == NodeKind::Struct) {
                return emit_ctor(n, n->resolved);
            }
            Node* method = callee->resolved;
            Node* obj = callee->left;
            Type* ot = obj != nullptr ? obj->ty : nullptr;
            if (callee->text == "compare" && method == nullptr) {
                string L = emit_expr(obj);
                string R = emit_expr(n->body != nullptr ? n->body->left : nullptr);
                return "((" + L + " < " + R + ") ? -1LL : ((" + L + " > " + R + ") ? 1LL : 0LL))";
            }
            Node* owner = ot != nullptr ? ot->decl : nullptr;
            string name = func_ident(method, owner);
            string args = emit_args(n->body);
            if (method != nullptr && (method->flags & FlagStatic) != 0) {
                bool fixed_over = method->text == "over" &&
                                  ((owner != nullptr && owner->text == "FixedBuffer") ||
                                   (n->ty != nullptr && n->ty->name == "FixedBuffer"));
                if (fixed_over) {
                    Node* arg = n->body != nullptr ? n->body->left : nullptr;
                    Type* at = arg != nullptr ? arg->ty : nullptr;
                    string e = emit_expr(arg);
                    if (is_array(at)) {
                        return "((lb_fixed){ .data = (uint8_t*)(" + e + ".d), .cap = " +
                               std::to_string(at->length) + "ULL, .used = 0 })";
                    }
                    int sid = tmp();
                    string sn = "_lb_s" + std::to_string(sid);
                    return "({ lb_span " + sn + " = " + e + "; (lb_fixed){ .data = (uint8_t*)" +
                           sn + ".data, .cap = " + sn + ".length, .used = 0 }; })";
                }
                return name + "(" + args + ")";
            }
            string recv = emit_addr(obj);
            if (args.empty()) {
                return name + "(" + recv + ")";
            }
            return name + "(" + recv + ", " + args + ")";
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
            return emit_enum_value(n);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
            string name = func_ident(n->resolved, nullptr);
            string args = emit_args(n->body);
            return name + "(" + args + ")";
        }
        return "0";
    }

    string emit_ctor(Node* n, Node* st) {
        string s = "(" + struct_ident(st) + "){";
        bool first = true;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            if (a->text.empty()) {
                continue;
            }
            if (!first) {
                s += ", ";
            }
            first = false;
            s += "." + string(a->text) + " = " + emit_expr(a->left);
        }
        s += "}";
        return s;
    }

    string emit_exhausted_lit(Type* payload) {
        return "((" + fail_c_name(payload) +
               "){ .error = { .code = LB_MEMORY_EXHAUSTED, .message = "
               "(lb_str){\"memory.exhausted\", 16} }, .failed = true })";
    }

    string emit_allocator(Node* n) {
        if (n == nullptr) {
            return "lb_get_alloc()";
        }
        Type* t = n->ty;
        if (t != nullptr && t->kind == TypeKind::Struct && t->name == "FixedBuffer") {
            return "lb_fixed_alloc(&(" + emit_expr(n) + "))";
        }
        return emit_expr(n);
    }

    string emit_new(Node* n) {
        int id = tmp();
        string an = "_lb_a" + std::to_string(id);
        string bn = "_lb_b" + std::to_string(id);
        string rn = "_lb_r" + std::to_string(id);
        Type* payload = is_fail(n->ty) ? n->ty->elem : n->ty;
        string rty = fail_c_name(payload);
        string s = "({ ";
        s += "lb_alloc " + an + " = " + emit_allocator(n->right) + "; ";
        if (is_span(payload)) {
            Type* elem = payload->elem;
            Node* count = n->type != nullptr ? n->type->right : nullptr;
            string cn = "_lb_n" + std::to_string(id);
            string et = c_type(elem);
            s += "size_t " + cn + " = (size_t)(" + emit_expr(count) + "); ";
            s += rty + " " + rn + "; ";
            s += "if (" + cn + " != 0 && sizeof(" + et + ") > ((size_t)-1) / " + cn + ") { ";
            s += rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
            s += "size_t _lb_bytes" + std::to_string(id) + " = sizeof(" + et + ") * " + cn + "; ";
            s += "lb_span " + bn + " = lb_alloc_bytes(" + an + ", _lb_bytes" + std::to_string(id) +
                 ", _Alignof(" + et + ")); ";
            s += "if (_lb_bytes" + std::to_string(id) + " != 0 && " + bn +
                 ".data == NULL) { " + rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
            s += "if (" + bn + ".data != NULL) memset(" + bn + ".data, 0, _lb_bytes" +
                 std::to_string(id) + "); ";
            s += rn + ".value.data = " + bn + ".data; ";
            s += rn + ".value.length = " + cn + "; ";
            s += rn + ".failed = false; } } ";
            s += rn + "; })";
            return s;
        }
        Type* elem = is_ptr(payload) ? payload->elem : payload;
        string et = c_type(elem);
        string pn = "_lb_p" + std::to_string(id);
        s += "lb_span " + bn + " = lb_alloc_bytes(" + an + ", sizeof(" + et + "), _Alignof(" + et +
             ")); ";
        s += rty + " " + rn + "; ";
        s += "if (sizeof(" + et + ") != 0 && " + bn + ".data == NULL) { " + rn + " = " +
             emit_exhausted_lit(payload) + "; } else { ";
        s += et + "* " + pn + " = (" + et + "*)" + bn + ".data; ";
        if (n->body != nullptr && n->body->kind == NodeKind::CaseValue) {
            s += "if (" + pn + ") *" + pn + " = " + emit_enum_value(n->body) + "; ";
        } else if (n->body != nullptr && n->resolved != nullptr &&
                   n->resolved->kind == NodeKind::Struct) {
            s += "if (" + pn + ") *" + pn + " = " + emit_ctor(n, n->resolved) + "; ";
        } else {
            s += "if (" + pn + ") memset(" + pn + ", 0, sizeof(" + et + ")); ";
        }
        s += rn + ".value = " + pn + "; ";
        s += rn + ".failed = false; } ";
        s += rn + "; })";
        return s;
    }

    string emit_alloc(Node* n) {
        int id = tmp();
        string an = "_lb_a" + std::to_string(id);
        string bn = "_lb_b" + std::to_string(id);
        string rn = "_lb_r" + std::to_string(id);
        Type* payload = is_fail(n->ty) ? n->ty->elem : n->ty;
        string rty = fail_c_name(payload);
        string s = "({ ";
        s += "lb_alloc " + an + " = " + emit_allocator(n->right) + "; ";
        s += rty + " " + rn + "; ";
        if (n->type == nullptr) {
            string sz = emit_expr(n->body != nullptr ? n->body->left : nullptr);
            string al = emit_expr(n->body != nullptr && n->body->next != nullptr
                                      ? n->body->next->left
                                      : nullptr);
            s += "size_t _lb_sz" + std::to_string(id) + " = (size_t)(" + sz + "); ";
            s += "size_t _lb_al" + std::to_string(id) + " = (size_t)(" + al + "); ";
            s += "lb_span " + bn + " = lb_alloc_bytes(" + an + ", _lb_sz" + std::to_string(id) +
                 ", _lb_al" + std::to_string(id) + "); ";
            s += "if (_lb_sz" + std::to_string(id) + " != 0 && " + bn +
                 ".data == NULL) { " + rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
            s += rn + ".value = " + bn + "; " + rn + ".failed = false; } ";
            s += rn + "; })";
            return s;
        }
        Type* elem = payload != nullptr ? payload->elem : nullptr;
        string et = c_type(elem);
        Node* count = n->type->right;
        string cn = "_lb_n" + std::to_string(id);
        s += "size_t " + cn + " = (size_t)(" + emit_expr(count) + "); ";
        s += "if (" + cn + " != 0 && sizeof(" + et + ") > ((size_t)-1) / " + cn + ") { ";
        s += rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
        s += "size_t _lb_bytes" + std::to_string(id) + " = sizeof(" + et + ") * " + cn + "; ";
        s += "lb_span " + bn + " = lb_alloc_bytes(" + an + ", _lb_bytes" + std::to_string(id) +
             ", _Alignof(" + et + ")); ";
        s += "if (_lb_bytes" + std::to_string(id) + " != 0 && " + bn +
             ".data == NULL) { " + rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
        s += rn + ".value.data = " + bn + ".data; ";
        s += rn + ".value.length = " + cn + "; ";
        s += rn + ".failed = false; } } ";
        s += rn + "; })";
        return s;
    }

    void emit_free(Node* n) {
        Type* t = n->left != nullptr ? n->left->ty : nullptr;
        string a = emit_allocator(n->right);
        string e = emit_expr(n->left);
        if (is_ptr(t)) {
            string et = c_type(t->elem);
            line("lb_release_bytes(" + a + ", (lb_span){ (void*)(" + e + "), sizeof(" + et +
                 ") });");
            return;
        }
        if (is_span(t)) {
            string et = c_type(t->elem);
            int id = tmp();
            string sn = "_lb_s" + std::to_string(id);
            line("{ lb_span " + sn + " = " + e + "; lb_release_bytes(" + a + ", (lb_span){ " + sn +
                 ".data, " + sn + ".length * sizeof(" + et + ") }); }");
            return;
        }
        line("(void)(" + e + ");");
    }

    void emit_with(Node* n) {
        int id = tmp();
        string save = "_lb_as" + std::to_string(id);
        line("{");
        indent++;
        line("lb_alloc " + save + " = lb_get_alloc();");
        line("lb_set_alloc(" + emit_allocator(n->left) + ");");
        Scope sc;
        sc.restore_alloc = true;
        sc.alloc_save = save;
        scopes.push_back(sc);
        emit_stmt(n->body);
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        indent--;
        line("}");
    }

    void emit_block(Node* n) {
        line("{");
        indent++;
        scopes.push_back(Scope{});
        if (n != nullptr) {
            for (Node* s = n->kind == NodeKind::Block ? n->body : n; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        indent--;
        line("}");
    }

    void emit_stmt(Node* n) {
        if (n == nullptr) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            emit_block(n);
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            string ty = c_type(n->ty);
            string name = ident("lb_", n->text);
            if (n->flags & FlagUninit) {
                line(ty + " " + name + ";");
                break;
            }
            string init = "0";
            if (n->left != nullptr) {
                init = emit_expr(n->left);
                Type* st = n->left->ty;
                if (is_span(n->ty) && is_array(st)) {
                    char nbuf[32];
                    snprintf(nbuf, sizeof(nbuf), "%lluULL",
                             static_cast<unsigned long long>(st->length));
                    init = "((lb_span){" + init + ".d, " + nbuf + "})";
                }
            } else if (n->ty != nullptr &&
                       (n->ty->kind == TypeKind::Struct || n->ty->kind == TypeKind::Union ||
                        n->ty->kind == TypeKind::Enum || is_array(n->ty) || is_span(n->ty) ||
                        n->ty->kind == TypeKind::Str || is_opt(n->ty) ||
                        n->ty->kind == TypeKind::Allocator)) {
                init = "{0}";
            } else if (n->ty != nullptr && n->ty->kind == TypeKind::Bool) {
                init = "false";
            }
            line(ty + " " + name + " = " + init + ";");
            break;
        }
        case NodeKind::Assign: {
            if (n->left != nullptr && n->left->kind == NodeKind::Member &&
                n->left->text == "allocator") {
                Type* lt = n->left->left != nullptr ? n->left->left->ty : nullptr;
                if (lt != nullptr && lt->kind == TypeKind::Module) {
                    line("lb_set_alloc(" + emit_allocator(n->right) + ");");
                    break;
                }
            }
            string dst = emit_expr(n->left);
            // self.x emits self->x; Name emits lb_name.
            if (n->left != nullptr && n->left->kind == NodeKind::Self) {
                dst = "(*self)";
            }
            string src = emit_expr(n->right);
            if (n->op == TokenKind::Eq) {
                line(dst + " = " + src + ";");
                break;
            }
            TokenKind op = TokenKind::Plus;
            if (n->op == TokenKind::MinusEq) {
                op = TokenKind::Minus;
            } else if (n->op == TokenKind::StarEq) {
                op = TokenKind::Star;
            } else if (n->op == TokenKind::SlashSlashEq) {
                op = TokenKind::SlashSlash;
            } else if (n->op == TokenKind::PercentEq) {
                op = TokenKind::Percent;
            } else if (n->op == TokenKind::PlusPercentEq) {
                op = TokenKind::PlusPercent;
            } else if (n->op == TokenKind::MinusPercentEq) {
                op = TokenKind::MinusPercent;
            } else if (n->op == TokenKind::StarPercentEq) {
                op = TokenKind::StarPercent;
            } else if (n->op == TokenKind::PlusPipeEq) {
                op = TokenKind::PlusPipe;
            } else if (n->op == TokenKind::MinusPipeEq) {
                op = TokenKind::MinusPipe;
            } else if (n->op == TokenKind::StarPipeEq) {
                op = TokenKind::StarPipe;
            }
            Type* t = n->left != nullptr ? n->left->ty : nullptr;
            const char* helper = "add";
            if (op == TokenKind::Minus) {
                helper = "sub";
            } else if (op == TokenKind::Star) {
                helper = "mul";
            } else if (op == TokenKind::SlashSlash) {
                helper = "div";
            } else if (op == TokenKind::Percent) {
                helper = "mod";
            } else if (op == TokenKind::PlusPercent) {
                helper = "addw";
            } else if (op == TokenKind::MinusPercent) {
                helper = "subw";
            } else if (op == TokenKind::StarPercent) {
                helper = "mulw";
            } else if (op == TokenKind::PlusPipe) {
                helper = "adds";
            } else if (op == TokenKind::MinusPipe) {
                helper = "subs";
            } else if (op == TokenKind::StarPipe) {
                helper = "muls";
            }
            line(dst + " = " + emit_helper(helper, t, dst, src) + ";");
            break;
        }
        case NodeKind::If:
            emit_if(n);
            break;
        case NodeKind::While:
            emit_while(n);
            break;
        case NodeKind::Return:
            emit_return(n);
            break;
        case NodeKind::Break:
        case NodeKind::Continue:
            emit_jump(n);
            break;
        case NodeKind::Defer:
        case NodeKind::Errdefer:
            if (!scopes.empty()) {
                scopes.back().defers.push_back(n);
            } else {
                line(emit_expr(n->left) + ";");
            }
            break;
        case NodeKind::Recover:
            if (!catch_var.empty()) {
                line(catch_var + " = " + emit_expr(n->left) + ";");
            } else {
                string e = emit_expr(n->left);
                if (fn_fallible()) {
                    e = wrap_ok(e);
                }
                run_defers_from(0);
                line("return " + e + ";");
            }
            break;
        case NodeKind::Match:
            emit_match(n);
            break;
        case NodeKind::For: {
            if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
                (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
                emit_for_range(n);
                break;
            }
            Type* it = n->right != nullptr ? n->right->ty : nullptr;
            string seq = emit_expr(n->right);
            string idx = ident("lb_i_", n->text);
            string len;
            string elem_e;
            if (is_array(it)) {
                char nbuf[32];
                snprintf(nbuf, sizeof(nbuf), "%lluULL",
                         static_cast<unsigned long long>(it->length));
                len = nbuf;
                elem_e = seq + ".d[" + idx + "]";
            } else if (it != nullptr && it->kind == TypeKind::Str) {
                len = seq + ".length";
                elem_e = "((const unsigned char*)" + seq + ".data)[" + idx + "]";
            } else {
                len = seq + ".length";
                string et = n->ty != nullptr
                                ? c_type((n->flags & FlagByPtr) != 0 && is_ptr(n->ty) ? n->ty->elem
                                                                                     : n->ty)
                                : "uint8_t";
                if (n->ty != nullptr && is_ptr(n->ty)) {
                    et = c_type(n->ty->elem);
                }
                elem_e = "((" + et + "*)" + seq + ".data)[" + idx + "]";
            }
            Scope sc;
            sc.loop = true;
            scopes.push_back(sc);
            line("for (size_t " + idx + " = 0; " + idx + " < " + len + "; " + idx + "++) {");
            indent++;
            if (n->flags & FlagByPtr) {
                line(c_type(n->ty) + " " + ident("lb_", n->text) + " = &(" + elem_e + ");");
            } else {
                line(c_type(n->ty) + " " + ident("lb_", n->text) + " = " + elem_e + ";");
            }
            emit_stmt(n->body);
            indent--;
            line("}");
            if (!scopes.empty()) {
                unwind_scope(scopes.back());
                scopes.pop_back();
            }
            break;
        }
        case NodeKind::Free:
            emit_free(n);
            break;
        case NodeKind::With:
            emit_with(n);
            break;
        case NodeKind::ExprStmt:
            if (is_error_call(n->left)) {
                run_defers_from(0);
                line("return " + emit_expr(n->left) + ";");
            } else {
                line(emit_expr(n->left) + ";");
            }
            break;
        default:
            line("/* unsupported stmt */");
            break;
        }
    }

    bool any_defers() {
        for (size_t i = 0; i < scopes.size(); i++) {
            if (!scopes[i].defers.empty() || scopes[i].restore_alloc) {
                return true;
            }
        }
        return false;
    }

    int loop_scope(string_view label) {
        for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; i--) {
            if (scopes[static_cast<size_t>(i)].loop &&
                (label.empty() || scopes[static_cast<size_t>(i)].label == label)) {
                return i;
            }
        }
        return 0;
    }

    void emit_return(Node* n) {
        if (n->left != nullptr && n->left->kind == NodeKind::Call && n->left->left != nullptr &&
            n->left->left->kind == NodeKind::Name && n->left->left->text == "trap") {
            line(emit_expr(n->left) + ";");
            return;
        }
        if (is_error_call(n->left)) {
            string e = emit_expr(n->left);
            if (any_defers()) {
                int id = tmp();
                string t = "_lb_ret" + std::to_string(id);
                line(fn_c_ret(current_fn) + " " + t + " = " + e + ";");
                run_defers_from(0);
                line("return " + t + ";");
            } else {
                line("return " + e + ";");
            }
            return;
        }
        if (n->left == nullptr) {
            run_defers_from(0);
            if (fn_fallible()) {
                line("return " + wrap_ok("0") + ";");
            } else {
                line("return;");
            }
            return;
        }
        string e = emit_expr(n->left);
        if (fn_fallible()) {
            e = wrap_ok(e);
        }
        if (any_defers()) {
            int id = tmp();
            string t = "_lb_ret" + std::to_string(id);
            line(fn_c_ret(current_fn) + " " + t + " = " + e + ";");
            run_defers_from(0);
            line("return " + t + ";");
        } else {
            line("return " + e + ";");
        }
    }

    void emit_jump(Node* n) {
        int from = loop_scope(n->text);
        run_defers_from(from);
        if (n->text.empty()) {
            line(n->kind == NodeKind::Break ? "break;" : "continue;");
            return;
        }
        string lab = n->kind == NodeKind::Break ? "lb_brk_" : "lb_cont_";
        lab += string(n->text);
        line("goto " + lab + ";");
    }

    void emit_while(Node* n) {
        Scope sc;
        sc.loop = true;
        sc.label = n->text;
        scopes.push_back(sc);
        if (n->flags & FlagIfLet) {
            Node* let = n->left;
            line("while (1) {");
            indent++;
            int id = tmp();
            string on = "_lb_o" + std::to_string(id);
            Type* ot = let != nullptr && let->left != nullptr ? let->left->ty : nullptr;
            line(c_type(ot) + " " + on + " = " + emit_expr(let != nullptr ? let->left : nullptr) +
                 ";");
            string cond = is_opt(ot) ? on + ".present" : on + " != ((void*)0)";
            line("if (!(" + cond + ")) break;");
            if (let != nullptr && !let->text.empty()) {
                if (is_opt(ot) && ot->elem != nullptr) {
                    line(c_type(ot->elem) + " " + ident("lb_", let->text) + " = " + on + ".value;");
                } else {
                    line(c_type(ot) + " " + ident("lb_", let->text) + " = " + on + ";");
                }
            }
            emit_stmt(n->body);
            if (!n->text.empty()) {
                line("lb_cont_" + string(n->text) + ": ;");
            }
            indent--;
            line("}");
        } else {
            pad();
            out += "while (" + emit_expr(n->left) + ") {\n";
            indent++;
            emit_stmt(n->body);
            if (!n->text.empty()) {
                line("lb_cont_" + string(n->text) + ": ;");
            }
            indent--;
            line("}");
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        if (!n->text.empty()) {
            line("lb_brk_" + string(n->text) + ": ;");
        }
    }

    void emit_for_range(Node* n) {
        Scope sc;
        sc.loop = true;
        scopes.push_back(sc);
        string ty = c_type(n->ty);
        string name = ident("lb_", n->text);
        string a = emit_expr(n->right->left);
        string b = emit_expr(n->right->right);
        string cmp = n->right->op == TokenKind::DotDotEq ? " <= " : " < ";
        line("for (" + ty + " " + name + " = (" + ty + ")(" + a + "); " + name + cmp + "(" + ty +
             ")(" + b + "); " + name + "++) {");
        indent++;
        emit_stmt(n->body);
        indent--;
        line("}");
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
    }

    void emit_match(Node* n) {
        Type* st = n->left != nullptr ? n->left->ty : nullptr;
        int id = tmp();
        string sv = "_lb_m" + std::to_string(id);
        line(c_type(st) + " " + sv + " = " + emit_expr(n->left) + ";");
        if (is_int(st) || is_int_enum(st) || (st != nullptr && st->kind == TypeKind::Bool)) {
            line("switch ((int64_t)(" + sv + ")) {");
            indent++;
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    if (pat->text == "_") {
                        line("default:");
                    } else if (pat->resolved != nullptr &&
                               pat->resolved->kind == NodeKind::EnumCase) {
                        line("case " + std::to_string(emit_case_int(st->decl, pat->resolved)) +
                             "LL:");
                    } else if (pat->left != nullptr) {
                        line("case " + emit_expr(pat->left) + ":");
                    }
                }
                line("{");
                indent++;
                emit_stmt(arm->body);
                line("break;");
                indent--;
                line("}");
            }
            indent--;
            line("}");
            return;
        }
        if (is_enum(st)) {
            line("switch ((int)(" + sv + ".tag)) {");
            indent++;
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    if (pat->text == "_") {
                        line("default:");
                    } else if (pat->resolved != nullptr) {
                        int tag = emit_case_tag(st->decl, pat->resolved);
                        line("case " + std::to_string(tag) + ":");
                    }
                }
                line("{");
                indent++;
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    Node* cse = pat->resolved;
                    if (cse == nullptr || cse->body == nullptr) {
                        continue;
                    }
                    Node* p = cse->body;
                    Node* b = pat->body;
                    while (p != nullptr && b != nullptr) {
                        if (b->text != "_") {
                            line(c_type(p->ty) + " " + ident("lb_", b->text) + " = " + sv + ".u." +
                                 string(cse->text) + "." + string(p->text) + ";");
                        }
                        p = p->next;
                        b = b->next;
                    }
                }
                emit_stmt(arm->body);
                line("break;");
                indent--;
                line("}");
            }
            indent--;
            line("}");
            return;
        }
        if (is_opt(st)) {
            bool first = true;
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                string cond = "0";
                string bind;
                for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                    if (pat->text == "_") {
                        cond = "1";
                    } else if (pat->text == "none") {
                        cond = "(!" + sv + ".present)";
                    } else if (pat->text == "some") {
                        cond = sv + ".present";
                        if (pat->body != nullptr && !pat->body->text.empty() && st->elem != nullptr) {
                            bind = c_type(st->elem) + " " + ident("lb_", pat->body->text) + " = " +
                                   sv + ".value;";
                        }
                    }
                }
                pad();
                out += first ? "if (" : "else if (";
                out += cond + ") {\n";
                first = false;
                indent++;
                if (!bind.empty()) {
                    line(bind);
                }
                emit_stmt(arm->body);
                indent--;
                line("}");
            }
        }
    }

    void emit_if(Node* n) {
        if (n->flags & FlagIfLet) {
            Node* let = n->left;
            int id = tmp();
            string on = "_lb_o" + std::to_string(id);
            Type* ot = let != nullptr && let->left != nullptr ? let->left->ty : nullptr;
            line(c_type(ot) + " " + on + " = " + emit_expr(let != nullptr ? let->left : nullptr) +
                 ";");
            string cond = is_opt(ot) ? on + ".present" : on + " != ((void*)0)";
            pad();
            out += "if (" + cond + ") {\n";
            indent++;
            if (let != nullptr && !let->text.empty()) {
                if (is_opt(ot) && ot->elem != nullptr) {
                    line(c_type(ot->elem) + " " + ident("lb_", let->text) + " = " + on + ".value;");
                } else {
                    line(c_type(ot) + " " + ident("lb_", let->text) + " = " + on + ";");
                }
            }
            emit_stmt(n->body);
            indent--;
            line("}");
            if (n->right != nullptr) {
                pad();
                out += "else ";
                if (n->right->kind == NodeKind::If) {
                    out += '\n';
                    emit_if(n->right);
                } else if (n->right->kind == NodeKind::Block) {
                    out += '\n';
                    emit_block(n->right);
                } else {
                    out += "{\n";
                    indent++;
                    emit_stmt(n->right);
                    indent--;
                    line("}");
                }
            }
            return;
        }
        pad();
        out += "if (" + emit_expr(n->left) + ") ";
        if (n->body != nullptr && n->body->kind == NodeKind::Block) {
            out += '\n';
            emit_block(n->body);
        } else {
            out += "{\n";
            indent++;
            emit_stmt(n->body);
            indent--;
            line("}");
        }
        if (n->right == nullptr) {
            return;
        }
        if (n->right->kind == NodeKind::If) {
            pad();
            out += "else ";
            // continue on same conceptual chain; emit_if writes "if"
            // so we need "else if". Rewrite first line: call emit_if after else.
            // emit_if always starts with pad+if. So write "else " without newline
            // then emit_if which pads again — that puts else and if on two lines
            // which is valid C: `else\n if`.
            out += '\n';
            emit_if(n->right);
            return;
        }
        pad();
        out += "else ";
        if (n->right->kind == NodeKind::Block) {
            out += '\n';
            emit_block(n->right);
        } else {
            out += "{\n";
            indent++;
            emit_stmt(n->right);
            indent--;
            line("}");
        }
    }

    void emit_sig(Node* fn, Node* owner, bool define) {
        string ret = fn_c_ret(fn);
        string name = func_ident(fn, owner);
        string sig = ret + " " + name + "(";
        bool first = true;
        if (owner != nullptr && (fn->flags & FlagStatic) == 0) {
            if ((fn->flags & FlagMutating) != 0) {
                sig += struct_ident(owner) + "* self";
            } else {
                sig += "const " + struct_ident(owner) + "* self";
            }
            first = false;
        }
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (!first) {
                sig += ", ";
            }
            first = false;
            sig += c_type(p->ty) + " " + ident("lb_", p->text);
        }
        if (first) {
            sig += "void";
        }
        sig += ")";
        if (!define) {
            line(sig + ";");
            return;
        }
        line(sig + " {");
        indent++;
        Node* saved_fn = current_fn;
        current_fn = fn;
        scopes.push_back(Scope{});
        if (fn->body != nullptr && fn->body->kind == NodeKind::Block) {
            for (Node* s = fn->body->body; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        } else {
            emit_stmt(fn->body);
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        current_fn = saved_fn;
        indent--;
        line("}");
        out += '\n';
    }

    string type_attrs(Node* n) {
        string a;
        if (n != nullptr && (n->flags & FlagPacked) != 0) {
            a += " __attribute__((packed))";
        }
        uint64_t al = 0;
        if (n != nullptr && n->type != nullptr && n->type->kind == NodeKind::Literal &&
            n->type->op == TokenKind::IntLit) {
            ParsedInt p = parse_int_literal(n->type->text);
            if (p.ok) {
                al = p.value;
            }
        }
        if (al != 0) {
            a += " __attribute__((aligned(" + std::to_string(al) + ")))";
        }
        return a;
    }

    void emit_struct(Node* st) {
        line("typedef struct" + type_attrs(st) + " " + struct_ident(st) + " {");
        indent++;
        bool any = false;
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                string fa;
                uint64_t al = 0;
                if (m->right != nullptr && m->right->kind == NodeKind::Literal &&
                    m->right->op == TokenKind::IntLit) {
                    ParsedInt p = parse_int_literal(m->right->text);
                    if (p.ok) {
                        al = p.value;
                    }
                }
                if (al != 0) {
                    fa = " __attribute__((aligned(" + std::to_string(al) + ")))";
                }
                line(c_type(m->ty) + " " + string(m->text) + fa + ";");
                any = true;
            }
        }
        if (!any) {
            line("int unused;");
        }
        indent--;
        line("} " + struct_ident(st) + ";");
        out += '\n';
    }

    void emit_union(Node* un) {
        line("typedef union " + struct_ident(un) + " {");
        indent++;
        bool any = false;
        for (Node* m = un->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                line(c_type(m->ty) + " " + string(m->text) + ";");
                any = true;
            }
        }
        if (!any) {
            line("int unused;");
        }
        indent--;
        line("} " + struct_ident(un) + ";");
        out += '\n';
    }

    void emit_enum(Node* en) {
        if (is_int_enum(en->ty)) {
            return;
        }
        line("typedef struct " + struct_ident(en) + " {");
        indent++;
        line("int32_t tag;");
        bool any_payload = false;
        for (Node* c = en->body; c != nullptr; c = c->next) {
            if (c->kind == NodeKind::EnumCase && c->body != nullptr) {
                any_payload = true;
                break;
            }
        }
        if (any_payload) {
            line("union {");
            indent++;
            for (Node* c = en->body; c != nullptr; c = c->next) {
                if (c->kind != NodeKind::EnumCase || c->body == nullptr) {
                    continue;
                }
                line("struct {");
                indent++;
                for (Node* p = c->body; p != nullptr; p = p->next) {
                    line(c_type(p->ty) + " " + string(p->text) + ";");
                }
                indent--;
                line("} " + string(c->text) + ";");
            }
            indent--;
            line("} u;");
        }
        indent--;
        line("} " + struct_ident(en) + ";");
        out += '\n';
    }

    void emit_global(Node* g) {
        string tl = (g->flags & FlagThreadLocal) != 0 ? "_Thread_local " : "";
        string ty = c_type(g->ty);
        string name = ident("lb_", g->text);
        if (g->flags & FlagUninit) {
            line(tl + ty + " " + name + ";");
            return;
        }
        string init = "0";
        if (g->left != nullptr) {
            init = emit_expr(g->left);
        } else if (g->ty != nullptr &&
                   (g->ty->kind == TypeKind::Struct || g->ty->kind == TypeKind::Union ||
                    g->ty->kind == TypeKind::Enum || is_array(g->ty) || is_opt(g->ty) ||
                    is_span(g->ty) || (g->ty->kind == TypeKind::Str) ||
                    g->ty->kind == TypeKind::Allocator)) {
            init = "{0}";
        }
        line(tl + ty + " " + name + " = " + init + ";");
    }

    void note_opt(Type* t) {
        if (t == nullptr) {
            return;
        }
        for (size_t i = 0; i < opts.size(); i++) {
            if (opts[i] == t) {
                return;
            }
        }
        opts.push_back(t);
    }

    void note_fail(Type* payload) {
        if (payload == nullptr || payload->kind == TypeKind::Unit ||
            payload->kind == TypeKind::Never) {
            return;
        }
        for (size_t i = 0; i < fails.size(); i++) {
            if (fails[i] == payload) {
                return;
            }
        }
        fails.push_back(payload);
    }

    void note_type(Type* t) {
        if (t == nullptr || t->kind == TypeKind::Param) {
            return;
        }
        if (t->kind == TypeKind::Array) {
            note_type(t->elem);
            for (size_t i = 0; i < arrays.size(); i++) {
                if (arrays[i] == t) {
                    return;
                }
            }
            arrays.push_back(t);
            return;
        }
        if (is_opt(t)) {
            note_type(t->elem);
            if (t->elem != nullptr && t->elem->kind != TypeKind::Param) {
                note_opt(t);
            }
            return;
        }
        if (is_fail(t)) {
            note_type(t->elem);
            if (t->elem != nullptr && t->elem->kind != TypeKind::Param) {
                note_fail(t->elem);
            }
            return;
        }
        if (t->kind == TypeKind::Pointer || t->kind == TypeKind::Span) {
            note_type(t->elem);
        }
        if (t->kind == TypeKind::Struct && t->decl != nullptr) {
            for (Node* m = t->decl->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Field) {
                    note_type(m->ty);
                }
            }
        }
    }

    void walk_types(Node* n) {
        if (n == nullptr) {
            return;
        }
        note_type(n->ty);
        walk_types(n->left);
        walk_types(n->right);
        walk_types(n->body);
        walk_types(n->type);
        walk_types(n->next);
    }

    void emit_array_typedefs() {
        for (size_t i = 0; i < arrays.size(); i++) {
            Type* t = arrays[i];
            line("typedef struct " + array_c_name(t) + " { " + c_type(t->elem) + " d[" +
                 std::to_string(t->length) + "]; } " + array_c_name(t) + ";");
        }
        if (!arrays.empty()) {
            out += '\n';
        }
    }

    void emit_opt_typedefs() {
        for (size_t i = 0; i < opts.size(); i++) {
            Type* t = opts[i];
            line("LB_OPT(" + c_type(t->elem) + ", " + opt_c_name(t) + ");");
        }
        for (size_t i = 0; i < fails.size(); i++) {
            Type* t = fails[i];
            line("LB_RES(" + c_type(t) + ", " + fail_c_name(t) + ");");
        }
        if (!opts.empty() || !fails.empty()) {
            out += '\n';
        }
    }

    void note_fail_fn(Node* fn) {
        if (fn != nullptr && (fn->flags & FlagFallible) != 0) {
            note_fail(fn->ty);
        }
    }

    void collect_from(Node* mod) {
        if (mod == nullptr) {
            return;
        }
        walk_types(mod);
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                note_fail_fn(d);
            } else if (d->kind == NodeKind::Struct || d->kind == NodeKind::Enum ||
                       d->kind == NodeKind::Union) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        note_fail_fn(m);
                    }
                }
            }
        }
    }

    void emit_types(Node* mod) {
        if (mod == nullptr) {
            return;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::Struct) {
                emit_struct(d);
            } else if (d->kind == NodeKind::Union) {
                emit_union(d);
            } else if (d->kind == NodeKind::Enum) {
                emit_enum(d);
            }
        }
    }

    void emit_decls(Node* mod) {
        if (mod == nullptr) {
            return;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Global || d->kind == NodeKind::Const) {
                emit_global(d);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, false);
            } else if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, false);
                    }
                }
            } else if (d->kind == NodeKind::Test) {
                emit_test_sig(d, false);
            }
        }
    }

    void emit_defs(Node* mod) {
        if (mod == nullptr) {
            return;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, true);
                    }
                }
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, true);
            } else if (d->kind == NodeKind::Test) {
                emit_test_sig(d, true);
            }
        }
    }

    void emit_test_sig(Node* t, bool define) {
        string name = "lb_test_" + std::to_string(reinterpret_cast<uintptr_t>(t) & 0xffffu);
        if (!define) {
            line("lb_r_unit " + name + "(void);");
            return;
        }
        line("lb_r_unit " + name + "(void) {");
        indent++;
        Node* saved = current_fn;
        current_fn = t;
        t->flags |= FlagFallible;
        t->ty = nullptr;
        scopes.push_back(Scope{});
        if (t->body != nullptr && t->body->kind == NodeKind::Block) {
            for (Node* s = t->body->body; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        } else {
            emit_stmt(t->body);
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        line("return ((lb_r_unit){ .failed = false });");
        current_fn = saved;
        indent--;
        line("}");
        out += '\n';
    }

    Node* find_main(Node* mod) {
        if (mod == nullptr) {
            return nullptr;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && d->text == "main") {
                return d;
            }
        }
        return nullptr;
    }

    Node* find_answer(Node* mod) {
        if (mod == nullptr) {
            return nullptr;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && d->text == "answer") {
                return d;
            }
        }
        return nullptr;
    }

    void emit_answer_unwrap(Node* mod) {
        Node* fn = find_answer(mod);
        if (fn == nullptr || (fn->flags & FlagFallible) == 0) {
            return;
        }
        line("int64_t lb_answer(void) {");
        indent++;
        line(fail_c_name(fn->ty) + " r = lb_answer_impl();");
        line("if (r.failed) {");
        indent++;
        line("fprintf(stderr, \"error %d: %.*s\\n\", r.error.code, (int)r.error.message.length, "
             "r.error.message.data);");
        line("exit(1);");
        indent--;
        line("}");
        line("return r.value;");
        indent--;
        line("}");
        out += '\n';
    }

    void emit_c_main(Node* fn) {
        bool fail = fn != nullptr && (fn->flags & FlagFallible) != 0;
        Type* at = fn != nullptr && fn->right != nullptr ? fn->right->ty : nullptr;
        bool cstr = at != nullptr && is_span(at) && at->elem != nullptr &&
                    at->elem->kind == TypeKind::CStr;
        out += "int main(int argc, char** argv) {\n";
        out += "    lb_set_alloc(lb_heap_alloc());\n";
        if (cstr) {
            out += "    lb_cspan args = { (const void*)argv, (size_t)argc };\n";
            if (fail) {
                out += "    lb_r_i32 r = lb_main(args);\n";
                out += "    if (r.failed) { fprintf(stderr, \"error %d: %.*s\\n\", r.error.code, "
                       "(int)r.error.message.length, r.error.message.data); return 1; }\n";
                out += "    return (int)r.value;\n";
            } else {
                out += "    return (int)lb_main(args);\n";
            }
        } else {
            out += "    lb_str* items = (lb_str*)malloc((size_t)argc * sizeof(lb_str));\n";
            out += "    if (items == NULL) { return 1; }\n";
            out += "    for (int i = 0; i < argc; i++) {\n";
            out += "        size_t n = 0; while (argv[i][n]) n++;\n";
            out += "        lb_check_utf8(argv[i], n);\n";
            out += "        items[i].data = argv[i]; items[i].length = n;\n";
            out += "    }\n";
            out += "    lb_span args = { items, (size_t)argc };\n";
            if (fail) {
                out += "    lb_r_i32 r = lb_main(args);\n";
                out += "    int code = r.failed ? 1 : (int)r.value;\n";
                out += "    if (r.failed) { fprintf(stderr, \"error %d: %.*s\\n\", r.error.code, "
                       "(int)r.error.message.length, r.error.message.data); }\n";
                out += "    free(items);\n";
                out += "    return code;\n";
            } else {
                out += "    int code = (int)lb_main(args);\n";
                out += "    free(items);\n";
                out += "    return code;\n";
            }
        }
        out += "}\n";
    }

    void emit_module(Node* mod) {
        out += "/* generated by lucb */\n";
        out += "#include \"lucb_rt.h\"\n";
        out += "#include <stdio.h>\n";
        out += "#include <stdlib.h>\n";
        out += "#include <string.h>\n\n";
        arrays.clear();
        opts.clear();
        fails.clear();
        collect_from(mod);
        emit_array_typedefs();
        emit_types(mod);
        emit_opt_typedefs();
        emit_decls(mod);
        out += '\n';
        emit_defs(mod);
        emit_answer_unwrap(mod);
        Node* main_fn = find_main(mod);
        if (main_fn != nullptr) {
            emit_c_main(main_fn);
        }
    }

    void emit_many(const vector<Node*>& modules, Node* entry) {
        out += "/* generated by lucb */\n";
        out += "#include \"lucb_rt.h\"\n";
        out += "#include <stdio.h>\n";
        out += "#include <stdlib.h>\n";
        out += "#include <string.h>\n\n";
        arrays.clear();
        opts.clear();
        fails.clear();
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            collect_from(modules[static_cast<size_t>(i)]);
        }
        emit_array_typedefs();
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_types(modules[static_cast<size_t>(i)]);
        }
        emit_opt_typedefs();
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_decls(modules[static_cast<size_t>(i)]);
        }
        out += '\n';
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_defs(modules[static_cast<size_t>(i)]);
        }
        emit_answer_unwrap(entry);
        Node* main_fn = find_main(entry);
        if (main_fn != nullptr) {
            emit_c_main(main_fn);
        }
    }
};

} // namespace

string emit_c(Node* module) {
    Emitter e;
    if (module != nullptr) {
        e.emit_module(module);
    }
    return e.out;
}

string emit_program(const vector<Node*>& modules, Node* entry) {
    Emitter e;
    if (!modules.empty()) {
        e.emit_many(modules, entry != nullptr ? entry : modules[0]);
    } else if (entry != nullptr) {
        e.emit_module(entry);
    }
    return e.out;
}

} // namespace lucb
