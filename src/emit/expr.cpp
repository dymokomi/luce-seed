#include "emit/emitter.h"

#include "support/literal.h"
#include <cinttypes>
#include <cstdio>

namespace lucb {

string memorder_of(Node* n) {
    if (n != nullptr && (n->kind == NodeKind::CaseValue || n->kind == NodeKind::Member)) {
        if (n->text == "relaxed") {
            return "memory_order_relaxed";
        }
        if (n->text == "acquire") {
            return "memory_order_acquire";
        }
        if (n->text == "release") {
            return "memory_order_release";
        }
        if (n->text == "acq_rel") {
            return "memory_order_acq_rel";
        }
        if (n->text == "signal") {
            return "signal";
        }
    }
    return "memory_order_seq_cst";
}

auto Emitter::produces_opt(Node* n) -> bool {
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

auto Emitter::wrap_opt(Type* t, const string& e) -> string {
        Type* elem = t != nullptr ? t->elem : nullptr;
        return "((" + opt_c_name(t) + "){ .value = (" + c_type(elem) + ")(" + e +
               "), .present = true })";
    }

auto Emitter::wrap_ok(const string& e) -> string {
        Type* t = current_fn != nullptr ? current_fn->ty : nullptr;
        string rty = fail_c_name(t);
        if (t == nullptr || t->kind == TypeKind::Unit) {
            return "((" + rty + "){ .failed = false })";
        }
        return "((" + rty + "){ .value = (" + e + "), .failed = false })";
    }

auto Emitter::wrap_err(const string& code, const string& msg) -> string {
        string rty = fail_c_name(current_fn != nullptr ? current_fn->ty : nullptr);
        return "((" + rty + "){ .error = { .code = (int32_t)(" + code + "), .message = " + msg +
               " }, .failed = true })";
    }

auto Emitter::is_error_call(Node* n) -> bool {
        return n != nullptr && n->kind == NodeKind::Call && n->left != nullptr &&
               n->left->kind == NodeKind::Name && n->left->text == "error";
    }

auto Emitter::emit_enum_value(Node* n) -> string {
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

auto Emitter::emit_try(Node* n) -> string {
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

auto Emitter::emit_else(Node* n) -> string {
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

auto Emitter::emit_catch(Node* n) -> string {
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

auto Emitter::emit_expr(Node* n) -> string {
        if (n == nullptr) {
            return "0";
        }
        string e = emit_expr_inner(n);
        if (is_opt(n->ty) && !produces_opt(n)) {
            return wrap_opt(n->ty, e);
        }
        if (n->ty != nullptr && n->ty->kind == TypeKind::Interface) {
            Type* src = nullptr;
            if (n->kind == NodeKind::Unary && n->op == TokenKind::Amp && n->left != nullptr) {
                src = n->left->ty;
            } else if (n->kind == NodeKind::Name && n->resolved != nullptr) {
                src = n->resolved->ty;
            }
            if (is_ptr(src)) {
                src = src->elem;
            }
            if (src != nullptr && src->kind == TypeKind::Struct && src->decl != nullptr &&
                n->ty->decl != nullptr) {
                return "((lb_iface){ (void*)(" + e + "), &lb_vt_" + string(src->decl->text) + "_" +
                       string(n->ty->decl->text) + " })";
            }
        }
        return e;
    }

auto Emitter::emit_expr_inner(Node* n) -> string {
        switch (n->kind) {
        case NodeKind::Literal:
            return emit_literal(n);
        case NodeKind::Name:
            if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
                return emit_enum_value(n);
            }
            if (n->resolved != nullptr && n->resolved->kind == NodeKind::ExternVar) {
                return string(n->text);
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

auto Emitter::emit_literal(Node* n) -> string {
        if (n->op == TokenKind::KwTrue) {
            return "true";
        }
        if (n->op == TokenKind::KwFalse) {
            return "false";
        }
        if (n->op == TokenKind::StringLit) {
            string d = decode_lit(n->text);
            if (n->ty != nullptr && n->ty->kind == TypeKind::CStr) {
                return c_escape(d);
            }
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

auto Emitter::emit_unary(Node* n) -> string {
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

auto Emitter::emit_helper(const char* name, Type* t, const string& L, const string& R) -> string {
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

auto Emitter::emit_binary(Node* n) -> string {
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

auto Emitter::emit_enum_check(Type* dest, const string& e) -> string {
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

auto Emitter::emit_conv(Node* src, Type* dest, bool checked) -> string {
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

auto Emitter::emit_member(Node* n) -> string {
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

auto Emitter::emit_index(Node* n) -> string {
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

auto Emitter::emit_slice(Node* n) -> string {
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

auto Emitter::emit_array_lit(Node* n) -> string {
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

auto Emitter::emit_span_make(Node* n) -> string {
        string p = n->body != nullptr ? emit_expr(n->body->left) : "0";
        string len =
            n->body != nullptr && n->body->next != nullptr ? emit_expr(n->body->next->left) : "0";
        return "((lb_span){" + p + ", (size_t)(" + len + ")})";
    }

auto Emitter::emit_addr(Node* n) -> string {
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

auto Emitter::vt_type_name(Type* iface) -> string {
        string n = iface != nullptr && iface->decl != nullptr ? string(iface->decl->text)
                                                              : string("I");
        return "lb_vt_" + n;
    }

auto Emitter::vt_instance_name(Node* st, Node* iface) -> string {
        return "lb_vt_" + string(st->text) + "_" + string(iface->text);
    }

auto Emitter::emit_display_buf(const string& b, Node* v) -> string {
        Type* t = v != nullptr ? v->ty : nullptr;
        string e = emit_expr(v);
        if (t != nullptr && t->kind == TypeKind::Bool) {
            return "lb_fmtbuf_bool(&" + b + ", " + e + ")";
        }
        if (t != nullptr && t->kind == TypeKind::Str) {
            return "lb_fmtbuf_put(&" + b + ", " + e + ".data, " + e + ".length)";
        }
        if (is_float(t)) {
            return "lb_fmtbuf_f64(&" + b + ", (double)(" + e + "))";
        }
        if (t != nullptr && is_unsigned_int(t)) {
            return "lb_fmtbuf_u64(&" + b + ", (uint64_t)(" + e + "))";
        }
        if (is_ptr(t)) {
            return "lb_fmtbuf_u64(&" + b + ", (uint64_t)(uintptr_t)(" + e + "))";
        }
        return "lb_fmtbuf_i64(&" + b + ", (int64_t)(" + e + "))";
    }

auto Emitter::emit_print_formatted(Node* n) -> string {
        string s = "({ ";
        for (Node* p = n != nullptr ? n->body : nullptr; p != nullptr; p = p->next) {
            if (p->kind == NodeKind::FormatText) {
                string d = decode_lit(p->text);
                s += "fputs(" + c_escape(d) + ", stdout); ";
            } else if (p->kind == NodeKind::FormatField) {
                Type* t = p->left != nullptr ? p->left->ty : nullptr;
                string e = emit_expr(p->left);
                if (t != nullptr && t->kind == TypeKind::Bool) {
                    s += "fputs((" + e + ") ? \"true\" : \"false\", stdout); ";
                } else if (t != nullptr && t->kind == TypeKind::Str) {
                    s += "fwrite(" + e + ".data, 1, " + e + ".length, stdout); ";
                } else if (is_float(t)) {
                    s += "fprintf(stdout, \"%g\", (double)(" + e + ")); ";
                } else if (t != nullptr && is_unsigned_int(t)) {
                    s += "fprintf(stdout, \"%llu\", (unsigned long long)(" + e + ")); ";
                } else {
                    s += "fprintf(stdout, \"%lld\", (long long)(" + e + ")); ";
                }
            }
        }
        s += "fputc('\\n', stdout); (void)0; })";
        return s;
    }

auto Emitter::emit_format_call(Node* n) -> string {
        Node* buf = n->body != nullptr ? n->body->left : nullptr;
        Node* msg = n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
        int id = tmp();
        string bn = "_lb_fb" + std::to_string(id);
        string rn = "_lb_fr" + std::to_string(id);
        string s = "({ lb_span _lb_ds" + std::to_string(id) + " = " + emit_expr(buf) + "; ";
        s += "lb_fmtbuf " + bn + " = { (char*)_lb_ds" + std::to_string(id) + ".data, _lb_ds" +
             std::to_string(id) + ".length, 0 }; ";
        s += "int " + rn + " = 0; ";
        if (msg != nullptr && msg->kind == NodeKind::Formatted) {
            for (Node* p = msg->body; p != nullptr; p = p->next) {
                if (p->kind == NodeKind::FormatText) {
                    string d = decode_lit(p->text);
                    s += rn + " = " + rn + " || lb_fmtbuf_put(&" + bn + ", " + c_escape(d) + ", " +
                         std::to_string(d.size()) + "); ";
                } else if (p->kind == NodeKind::FormatField) {
                    s += rn + " = " + rn + " || " + emit_display_buf(bn, p->left) + "; ";
                }
            }
        } else {
            string e = emit_expr(msg);
            s += rn + " = " + rn + " || lb_fmtbuf_put(&" + bn + ", " + e + ".data, " + e +
                 ".length); ";
        }
        s += "lb_r_str _lb_out" + std::to_string(id) + "; ";
        s += "if (" + rn + ") { _lb_out" + std::to_string(id) +
             " = ((lb_r_str){ .error = { .code = LB_MEMORY_EXHAUSTED, .message = "
             "(lb_str){\"memory.exhausted\", 16} }, .failed = true }); } else { _lb_out" +
             std::to_string(id) + " = ((lb_r_str){ .value = lb_fmtbuf_finish(&" + bn +
             "), .failed = false }); } _lb_out" + std::to_string(id) + "; })";
        return s;
    }

auto Emitter::emit_args(Node* args) -> string {
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

auto Emitter::emit_extern_args(Node* n) -> string {
        string s;
        bool first = true;
        Node* p = n->resolved != nullptr ? n->resolved->right : nullptr;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            if (!first) {
                s += ", ";
            }
            first = false;
            Node* v = a->left;
            Type* pt = p != nullptr && (p->flags & FlagVariadic) == 0 ? p->ty : nullptr;
            if (v != nullptr && v->kind == NodeKind::Literal && v->op == TokenKind::StringLit &&
                (pt == nullptr || (pt != nullptr && pt->kind == TypeKind::CStr))) {
                s += c_escape(decode_lit(v->text));
            } else if (pt != nullptr && pt->kind == TypeKind::CStr) {
                s += "(" + emit_expr(v) + ").data";
            } else {
                s += emit_expr(v);
            }
            if (p != nullptr && (p->flags & FlagVariadic) == 0) {
                p = p->next;
            }
        }
        return s;
    }

auto Emitter::emit_call(Node* n) -> string {
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "location") {
            string file = "t.lucb";
            uint32_t line = n->span.line;
            string fn = current_fn != nullptr ? string(current_fn->text) : string("answer");
            char lbuf[16];
            snprintf(lbuf, sizeof(lbuf), "%u", line);
            return "((lb_Location){ .file = (lb_str){" + c_escape(file) + ", " +
                   std::to_string(file.size()) + "}, .line = " + lbuf +
                   "u, .function = (lb_str){" + c_escape(fn) + ", " + std::to_string(fn.size()) +
                   "} })";
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "format") {
            return emit_format_call(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "CAllocator") {
            return "lb_heap_alloc()";
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            Type* ot = callee->left != nullptr ? callee->left->ty : nullptr;
            Type* recv = ot;
            if (is_ptr(ot) && ot->elem != nullptr) {
                recv = ot->elem;
            }
            if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "fence") {
                string ord = memorder_of(n->body != nullptr ? n->body->left : nullptr);
                if (ord == "signal") {
                    return "({ __asm__ volatile(\"\" ::: \"memory\"); (void)0; })";
                }
                return "(atomic_thread_fence(" + ord + "))";
            }
            if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "current") {
                return "((lb_Handle){ (size_t)pthread_self() })";
            }
            if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "pause") {
                return "(lb_pause())";
            }
            if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "yield") {
                return "(sched_yield())";
            }
            if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "sleep") {
                string ms = n->body != nullptr ? emit_expr(n->body->left) : "0";
                int id = tmp();
                string tn = "_lb_ts" + std::to_string(id);
                return "({ struct timespec " + tn + "; " + tn + ".tv_sec = (time_t)((" + ms +
                       ") / 1000); " + tn + ".tv_nsec = (long)(((" + ms +
                       ") % 1000) * 1000000L); nanosleep(&" + tn + ", NULL); (void)0; })";
            }
            if (ot != nullptr && ot->kind == TypeKind::Module && callee->text == "spawn") {
                Node* entry = n->body != nullptr ? n->body->left : nullptr;
                Node* ctx = n->body != nullptr && n->body->next != nullptr ? n->body->next->left
                                                                          : nullptr;
                string fn = entry != nullptr && entry->resolved != nullptr
                                ? func_ident(entry->resolved, nullptr)
                                : "lb_unknown";
                string c = ctx != nullptr ? emit_expr(ctx) : "NULL";
                int id = tmp();
                string tn = "_lb_th" + std::to_string(id);
                string rn = "_lb_tr" + std::to_string(id);
                return "({ pthread_t " + tn + "; " + fail_c_name(n->ty) + " " + rn +
                       "; if (pthread_create(&" + tn + ", NULL, (void*(*)(void*))(void*)" + fn +
                       ", (void*)(" + c + ")) != 0) { " + rn +
                       " = (" + fail_c_name(n->ty) +
                       "){ .failed = true, .error = { .code = 1, .message = (lb_str){\"spawn\", 5} } }; } else { " +
                       rn + ".failed = false; " + rn + ".value = (lb_Handle){ (size_t)" + tn +
                       " }; } " + rn + "; })";
            }
            if (is_atomic(ot) || is_atomic(recv)) {
                string loc = is_ptr(ot) ? emit_expr(callee->left) : ("&(" + emit_expr(callee->left) + ")");
                Type* elem = is_atomic(ot) ? ot->elem : recv->elem;
                string et = c_type(elem);
                if (callee->text == "wait") {
                    string exp = n->body != nullptr ? emit_expr(n->body->left) : "0";
                    return "({ while (atomic_load_explicit(" + loc +
                           ", memory_order_seq_cst) == (" + et + ")(" + exp +
                           ")) { lb_pause(); } (void)0; })";
                }
                if (callee->text == "wake") {
                    return "((void)(" +
                           (n->body != nullptr ? emit_expr(n->body->left) : string("0")) + "))";
                }
                if (callee->text == "cas") {
                    Node* a = n->body;
                    string exp = a != nullptr ? emit_expr(a->left) : "0";
                    a = a != nullptr ? a->next : nullptr;
                    string des = a != nullptr ? emit_expr(a->left) : "0";
                    a = a != nullptr ? a->next : nullptr;
                    string succ = "memory_order_seq_cst";
                    string failo = "memory_order_seq_cst";
                    string weak = "0";
                    if (a != nullptr && a->text != "weak") {
                        succ = memorder_of(a->left);
                        a = a->next;
                        if (a != nullptr && a->text != "weak") {
                            failo = memorder_of(a->left);
                            a = a->next;
                        }
                    }
                    if (a != nullptr) {
                        weak = emit_expr(a->left);
                    }
                    int id = tmp();
                    string en = "_lb_ce" + std::to_string(id);
                    string on = "_lb_co" + std::to_string(id);
                    string tn = tup_c_name(n->ty);
                    return "({ " + et + " " + en + " = (" + et + ")(" + exp + "); bool " + on +
                           "; if (" + weak + ") { " + on +
                           " = atomic_compare_exchange_weak_explicit(" + loc + ", &" + en + ", (" +
                           et + ")(" + des + "), " + succ + ", " + failo + "); } else { " + on +
                           " = atomic_compare_exchange_strong_explicit(" + loc + ", &" + en + ", (" +
                           et + ")(" + des + "), " + succ + ", " + failo + "); } (" + tn + "){ " +
                           on + ", " + en + " }; })";
                }
                Node* extra = nullptr;
                if (n->body != nullptr && n->body->next != nullptr) {
                    extra = n->body->next->left;
                } else if (n->body != nullptr && callee->text == "load") {
                    extra = n->body->left;
                }
                string ord = memorder_of(extra);
                if (callee->text == "load") {
                    return "(" + et + ")atomic_load_explicit(" + loc + ", " + ord + ")";
                }
                string val = n->body != nullptr ? emit_expr(n->body->left) : "0";
                if (callee->text == "store") {
                    return "(atomic_store_explicit(" + loc + ", (" + et + ")(" + val + "), " + ord +
                           "), (void)0)";
                }
                if (callee->text == "max" || callee->text == "min") {
                    string cmp = callee->text == "max" ? ">=" : "<=";
                    int id = tmp();
                    string on = "_lb_mo" + std::to_string(id);
                    string nn = "_lb_mn" + std::to_string(id);
                    return "({ " + et + " " + on + " = atomic_load_explicit(" + loc + ", " + ord +
                           "); " + et + " " + nn + " = (" + et + ")(" + val + "); for (;;) { " + et +
                           " _w = (" + on + " " + cmp + " " + nn + ") ? " + on + " : " + nn +
                           "; if (atomic_compare_exchange_weak_explicit(" + loc + ", &" + on +
                           ", _w, " + ord + ", " + ord + ")) break; } " + on + "; })";
                }
                const char* op = "atomic_fetch_add_explicit";
                if (callee->text == "sub") {
                    op = "atomic_fetch_sub_explicit";
                } else if (callee->text == "set") {
                    op = "atomic_fetch_or_explicit";
                } else if (callee->text == "clear") {
                    op = "atomic_fetch_and_explicit";
                } else if (callee->text == "flip") {
                    op = "atomic_fetch_xor_explicit";
                } else if (callee->text == "swap") {
                    op = "atomic_exchange_explicit";
                }
                string arg = val;
                if (callee->text == "clear") {
                    arg = "~(" + val + ")";
                }
                return "(" + et + ")" + string(op) + "(" + loc + ", (" + et + ")(" + arg + "), " +
                       ord + ")";
            }
            if (recv != nullptr && recv->kind == TypeKind::Struct && recv->name == "Handle" &&
                callee->text == "join") {
                string h = emit_expr(callee->left);
                return "({ pthread_join((pthread_t)(" + h + ".id), NULL); (" + fail_c_name(n->ty) +
                       "){ .failed = false }; })";
            }
            if (recv != nullptr && recv->kind == TypeKind::Struct && recv->name == "Handle" &&
                callee->text == "detach") {
                string h = emit_expr(callee->left);
                return "(pthread_detach((pthread_t)(" + h + ".id)))";
            }
            if (recv != nullptr && recv->kind == TypeKind::Struct &&
                (recv->name == "Mutex" || recv->name == "Condition" || recv->name == "Once" ||
                 recv->name == "Semaphore")) {
                string loc = is_ptr(ot) ? emit_expr(callee->left) : emit_addr(callee->left);
                if (recv->name == "Mutex" && callee->text == "lock") {
                    return "(lb_mutex_lock(" + loc + "))";
                }
                if (recv->name == "Mutex" && callee->text == "unlock") {
                    return "(lb_mutex_unlock(" + loc + "))";
                }
                if (recv->name == "Mutex" && callee->text == "try") {
                    return "(lb_mutex_try(" + loc + "))";
                }
                if (recv->name == "Condition" && callee->text == "wait") {
                    string mu = n->body != nullptr ? emit_expr(n->body->left) : "NULL";
                    return "(lb_cond_wait(" + loc + ", " + mu + "))";
                }
                if (recv->name == "Condition" && callee->text == "signal") {
                    return "(lb_cond_signal(" + loc + "))";
                }
                if (recv->name == "Condition" && callee->text == "broadcast") {
                    return "(lb_cond_broadcast(" + loc + "))";
                }
                if (recv->name == "Once" && callee->text == "run") {
                    Node* entry = n->body != nullptr ? n->body->left : nullptr;
                    string fn = entry != nullptr && entry->resolved != nullptr
                                    ? func_ident(entry->resolved, nullptr)
                                    : "lb_unknown";
                    return "({ if (lb_once_begin(" + loc + ")) { " + fn + "(); lb_once_end(" + loc +
                           "); } else { lb_once_wait(" + loc + "); } (void)0; })";
                }
                if (recv->name == "Semaphore" && callee->text == "acquire") {
                    return "(lb_sem_acquire(" + loc + "))";
                }
                if (recv->name == "Semaphore" && callee->text == "release") {
                    return "(lb_sem_release(" + loc + "))";
                }
            }
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
            if (t != nullptr && t->kind == TypeKind::Fmt) {
                return emit_print_formatted(arg);
            }
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
            if (ot != nullptr && is_ptr(ot) && ot->elem != nullptr) {
                ot = ot->elem;
            }
            if (ot != nullptr && ot->kind == TypeKind::Interface && method != nullptr) {
                int id = tmp();
                string vn = "_lb_if" + std::to_string(id);
                string args = emit_args(n->body);
                string call = "((" + vt_type_name(ot) + "*)" + vn + ".vtable)->" +
                              string(method->text) + "(" + vn + ".data";
                if (!args.empty()) {
                    call += ", " + args;
                }
                call += ")";
                return "({ lb_iface " + vn + " = " + emit_expr(obj) + "; " + call + "; })";
            }
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
        if (n->resolved != nullptr &&
            (n->resolved->kind == NodeKind::Func || n->resolved->kind == NodeKind::ExternFunc)) {
            string name = func_ident(n->resolved, nullptr);
            string args = emit_args(n->body);
            if (n->resolved->kind == NodeKind::ExternFunc && n->body != nullptr) {
                args = emit_extern_args(n);
            }
            string call = name + "(" + args + ")";
            Type* rt = n->ty != nullptr ? n->ty : n->resolved->ty;
            if (n->resolved->kind == NodeKind::ExternFunc && needs_null_foreign(rt)) {
                int id = tmp();
                string pn = "_lb_fp" + std::to_string(id);
                return "({ " + c_type(rt) + " " + pn + " = " + call + "; if (" + pn +
                       " == NULL) lb_trap(\"null_foreign\"); " + pn + "; })";
            }
            return call;
        }
        return "0";
    }

auto Emitter::emit_ctor(Node* n, Node* st) -> string {
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

auto Emitter::emit_exhausted_lit(Type* payload) -> string {
        return "((" + fail_c_name(payload) +
               "){ .error = { .code = LB_MEMORY_EXHAUSTED, .message = "
               "(lb_str){\"memory.exhausted\", 16} }, .failed = true })";
    }

auto Emitter::emit_allocator(Node* n) -> string {
        if (n == nullptr) {
            return "lb_get_alloc()";
        }
        Type* t = n->ty;
        if (t != nullptr && t->kind == TypeKind::Struct && t->name == "FixedBuffer") {
            return "lb_fixed_alloc(&(" + emit_expr(n) + "))";
        }
        return emit_expr(n);
    }

auto Emitter::emit_new(Node* n) -> string {
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

auto Emitter::emit_alloc(Node* n) -> string {
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

} // namespace lucb
