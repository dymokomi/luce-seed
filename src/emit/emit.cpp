#include "emit/emit.h"

#include "check/type.h"
#include "support/literal.h"

#include <cinttypes>
#include <cstdio>

namespace lucb {
namespace {

string ident(string_view prefix, string_view name) {
    string s;
    s.append(prefix.data(), prefix.size());
    s.append(name.data(), name.size());
    return s;
}

string struct_ident(Node* st) {
    return ident("lb_", st->text);
}

string func_ident(Node* fn, Node* owner) {
    if (owner != nullptr) {
        return ident("lb_", owner->text) + "_" + string(fn->text);
    }
    return ident("lb_", fn->text);
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

string c_type(Type* t) {
    if (t == nullptr) {
        return "void";
    }
    if (t->kind == TypeKind::Struct) {
        if (t->decl != nullptr) {
            return struct_ident(t->decl);
        }
        return ident("lb_", t->name);
    }
    if (t->kind == TypeKind::Array) {
        return array_c_name(t);
    }
    if (t->kind == TypeKind::Pointer) {
        return c_type_spelling(t);
    }
    return c_type_name(t);
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
    return "(" + string(c_type_name(t)) + ")(" + e + ")";
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

    string emit_expr(Node* n) {
        if (n == nullptr) {
            return "0";
        }
        switch (n->kind) {
        case NodeKind::Literal:
            return emit_literal(n);
        case NodeKind::Name:
            if (is_span(n->ty) && n->resolved != nullptr && is_array(n->resolved->ty)) {
                string nm = ident("lb_", n->text);
                char nbuf[32];
                snprintf(nbuf, sizeof(nbuf), "%lluULL",
                         static_cast<unsigned long long>(n->resolved->ty->length));
                return "((lb_span){" + nm + ".d, " + nbuf + "})";
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
        }
        if (helper != nullptr) {
            return emit_helper(helper, t, L, R);
        }
        return "0";
    }

    string emit_conv(Node* src, Type* dest, bool checked) {
        string e = emit_expr(src);
        Type* st = src != nullptr ? src->ty : nullptr;
        if (dest == nullptr) {
            return e;
        }
        int mode = checked ? 0 : 1;
        if (is_float(st) && is_int(dest)) {
            const char* h = is_signed_int(dest) ? "lb_f_to_s" : "lb_f_to_u";
            char buf[256];
            snprintf(buf, sizeof(buf), "%s((double)(%s), %d, %d)", h, e.c_str(), int_bits(dest),
                     mode);
            return down_cast(dest, buf);
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
            char buf[256];
            snprintf(buf, sizeof(buf), "%s(%s, %d, %d, %d, %d, %d)", h, word_cast(from, e).c_str(),
                     fb, fs, tb, ts, mode);
            if (dest->kind == TypeKind::Char) {
                return "(uint32_t)(" + string(buf) + ")";
            }
            return down_cast(dest, buf);
        }
        return "(" + c_type(dest) + ")(" + e + ")";
    }

    string emit_member(Node* n) {
        Type* ot = n->left != nullptr ? n->left->ty : nullptr;
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
            return "(lb_check_index((uint64_t)(" + i + "), " + nbuf + "), (" + b + ").d[" + i +
                   "])";
        }
        if (is_span(bt) || (bt != nullptr && bt->kind == TypeKind::Str)) {
            string elem = n->ty != nullptr ? c_type(n->ty) : "uint8_t";
            return "(lb_check_index((uint64_t)(" + i + "), " + b + ".length), ((" + elem + "*)" +
                   b + ".data)[" + i + "])";
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
        if (callee != nullptr && callee->kind == NodeKind::Name && n->resolved == nullptr &&
            n->body != nullptr && n->ty != nullptr &&
            (is_int(n->ty) || is_float(n->ty) || n->ty->kind == TypeKind::Char)) {
            return emit_conv(n->body->left, n->ty, true);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
            return emit_ctor(n, n->resolved);
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            Node* method = callee->resolved;
            Node* obj = callee->left;
            Type* ot = obj != nullptr ? obj->ty : nullptr;
            Node* owner = ot != nullptr ? ot->decl : nullptr;
            string name = func_ident(method, owner);
            string args = emit_args(n->body);
            if (method != nullptr && (method->flags & FlagStatic) != 0) {
                return name + "(" + args + ")";
            }
            string recv = emit_addr(obj);
            if (args.empty()) {
                return name + "(" + recv + ")";
            }
            return name + "(" + recv + ", " + args + ")";
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

    void emit_block(Node* n) {
        line("{");
        indent++;
        if (n != nullptr) {
            for (Node* s = n->kind == NodeKind::Block ? n->body : n; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
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
                       (n->ty->kind == TypeKind::Struct || is_array(n->ty) || is_span(n->ty) ||
                        n->ty->kind == TypeKind::Str)) {
                init = "{0}";
            } else if (n->ty != nullptr && n->ty->kind == TypeKind::Bool) {
                init = "false";
            }
            line(ty + " " + name + " = " + init + ";");
            break;
        }
        case NodeKind::Assign: {
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
            pad();
            out += "while (" + emit_expr(n->left) + ") ";
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
            break;
        case NodeKind::Return:
            if (n->left == nullptr) {
                line("return;");
            } else if (n->left->kind == NodeKind::Call && n->left->left != nullptr &&
                       n->left->left->kind == NodeKind::Name && n->left->left->text == "trap") {
                line(emit_expr(n->left) + ";");
            } else {
                line("return " + emit_expr(n->left) + ";");
            }
            break;
        case NodeKind::For: {
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
            break;
        }
        case NodeKind::ExprStmt:
            line(emit_expr(n->left) + ";");
            break;
        default:
            line("/* unsupported stmt */");
            break;
        }
    }

    void emit_if(Node* n) {
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
        string ret = c_type(fn->ty);
        if (fn->ty != nullptr && fn->ty->kind == TypeKind::Unit) {
            ret = "void";
        }
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
        if (fn->body != nullptr && fn->body->kind == NodeKind::Block) {
            for (Node* s = fn->body->body; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        } else {
            emit_stmt(fn->body);
        }
        indent--;
        line("}");
        out += '\n';
    }

    void emit_struct(Node* st) {
        line("typedef struct " + struct_ident(st) + " {");
        indent++;
        bool any = false;
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                line(c_type(m->ty) + " " + string(m->text) + ";");
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

    void note_type(Type* t) {
        if (t == nullptr) {
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

    void emit_module(Node* mod) {
        out += "/* generated by lucb */\n";
        out += "#include \"lucb_rt.h\"\n\n";
        arrays.clear();
        walk_types(mod);
        emit_array_typedefs();
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                emit_struct(d);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, false);
            } else if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, false);
                    }
                }
            }
        }
        out += '\n';
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, true);
                    }
                }
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, true);
            }
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

} // namespace lucb
