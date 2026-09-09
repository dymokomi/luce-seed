//==============================================================================================
//
//   emit/expr - Expressions in C
//
//   DESCRIPTION:
//       Literals, names, operators through the runtime's checked helpers, conversions,
//       members, indexing with bounds checks, slices, array literals, addresses, `try`,
//       `else`, and `catch` as statement expressions, and enum values. Calls, allocation, and
//       text have their own units.
//
//==============================================================================================

#include "emit/emitter.h"

#include "support/decimal.h"
#include "support/literal.h"
#include <cinttypes>
#include <cstdio>

namespace lucb {

auto Emitter::emit_src_file() -> string {
    string file = src_file.empty() ? string("t.lucb") : src_file;
    return "((lb_str){" + c_escape(file) + ", " + std::to_string(file.size()) + "})";
}

// `path:line: assert failed: condition`: what a failed `assert` reports (§11.6).
auto Emitter::assert_message(Node* call) -> string {
    string file = src_file.empty() ? string("t.lucb") : src_file;
    string msg = file + ":" + std::to_string(call->span.line) + ": assert failed";
    if (!call->text.empty()) {
        msg += ": " + string(call->text);
    }
    return msg;
}

auto Emitter::emit_src_function() -> string {
    string fn = current_fn != nullptr ? string(current_fn->text) : string("answer");
    return "((lb_str){" + c_escape(fn) + ", " + std::to_string(fn.size()) + "})";
}

auto Emitter::emit_src_location(Node* n) -> string {
    uint32_t line = n != nullptr ? n->span.line : 1;
    char lbuf[16];
    snprintf(lbuf, sizeof(lbuf), "%u", line);
    return "((lb_Location){ .file = " + emit_src_file() + ", .line = " + lbuf +
           "u, .function = " + emit_src_function() + " })";
}

auto Emitter::produces_opt(Node* n) -> bool {
    if (n == nullptr || !is_opt(n->ty)) {
        return false;
    }
    if (n->kind == NodeKind::Group) {
        // the inner expression's emission is the optional whenever its type is one
        return n->left != nullptr && is_opt(n->left->ty);
    }
    if (n->kind == NodeKind::Conditional) {
        // each branch was emitted as the optional; the choice between them is one too
        return n->left != nullptr && is_opt(n->left->ty) && n->right != nullptr && is_opt(n->right->ty);
    }
    if (n->kind == NodeKind::Literal && n->op == TokenKind::KwNone) {
        return true;
    }
    if (n->kind == NodeKind::Unary && n->op == TokenKind::KwTry) {
        return true; // `try f()` of a `T?!` yields the `T?` itself
    }
    if (n->kind == NodeKind::Binary &&
        (n->op == TokenKind::PlusQuestion || n->op == TokenKind::MinusQuestion ||
         n->op == TokenKind::StarQuestion)) {
        return true;
    }
    if ((n->kind == NodeKind::Name || n->kind == NodeKind::Member || n->kind == NodeKind::Call) &&
        n->resolved != nullptr && is_opt(n->resolved->ty)) {
        return true;
    }
    if (n->kind == NodeKind::Call && n->resolved == nullptr && n->left != nullptr &&
        n->left->kind == NodeKind::Member && (n->left->text == "first" || n->left->text == "last")) {
        return true; // `span.first()` builds the optional itself
    }
    if (n->kind == NodeKind::Index) {
        // an element of an array or span of optionals is the optional itself
        Type* bt = n->left != nullptr ? n->left->ty : nullptr;
        if (is_ptr(bt)) {
            bt = bt->elem;
        }
        return bt != nullptr && (is_array(bt) || is_span(bt)) && is_opt(bt->elem);
    }
    if (n->kind == NodeKind::Call && n->left != nullptr && is_func(n->left->ty) &&
        n->left->ty->elem != nullptr && is_opt(n->left->ty->elem)) {
        return true; // a call through a function value returning `T?`
    }
    if (n->kind == NodeKind::Conditional) {
        // each branch typed as the optional is emitted as one, so the conditional yields it:
        // `x if c else none`, `none if c else x`, two optionals
        return n->left != nullptr && is_opt(n->left->ty) && n->right != nullptr &&
               is_opt(n->right->ty);
    }
    return false;
}

auto Emitter::wrap_opt(Type* t, const string& e) -> string {
    Type* elem = t != nullptr ? t->elem : nullptr;
    return "((" + c_type(t) + "){ .value = (" + c_type(elem) + ")(" + e + "), .present = true })";
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

auto Emitter::is_trap_call(Node* n) -> bool {
    return n != nullptr && n->kind == NodeKind::Call && n->left != nullptr &&
           n->left->kind == NodeKind::Name && n->left->text == "trap";
}

// A value that never arrives: a `never` expression, or `try` on a `never!` call.
// The checker coerces the outer type to the context, so the inner call is asked.
auto Emitter::never_valued(Node* n) -> bool {
    if (n == nullptr) {
        return false;
    }
    if (n->ty != nullptr && n->ty->kind == TypeKind::Never) {
        return true;
    }
    return n->kind == NodeKind::Unary && n->op == TokenKind::KwTry && n->left != nullptr &&
           is_fail(n->left->ty) && n->left->ty->elem != nullptr &&
           n->left->ty->elem->kind == TypeKind::Never;
}

auto Emitter::unit_valued(Node* n) -> bool {
    return n != nullptr && n->ty != nullptr && n->ty->kind == TypeKind::Unit;
}

auto Emitter::is_never_expr(Node* n) -> bool {
    if (n == nullptr) {
        return false;
    }
    if (n->ty != nullptr && n->ty->kind == TypeKind::Never) {
        return true;
    }
    if (n->kind == NodeKind::Return || n->kind == NodeKind::Break ||
        n->kind == NodeKind::Continue) {
        return true;
    }
    return is_error_call(n) || is_trap_call(n);
}

auto Emitter::emit_enum_value(Node* n) -> string {
    Node* cse = n->resolved;
    // under an optional type the value is the enum itself; the expression wraps it after
    Type* t = is_opt(n->ty) && !is_ptr(n->ty) ? n->ty->elem : n->ty;
    if (cse == nullptr) {
        return "0";
    }
    if (is_int_enum(t)) {
        uint64_t v = emit_case_int(t->decl, cse);
        return "((" + c_type(t) + ")" + std::to_string(v) + "u)";
    }
    int tag = emit_case_tag(t != nullptr ? t->decl : nullptr, cse);
    string tn = c_type(t);
    if (tn == "void") {
        tn = t != nullptr && t->decl != nullptr ? struct_ident(t->decl) : "int";
    }
    string s = "((" + tn + "){ .tag = " + std::to_string(tag);
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
    s += "if (" + rn + ".failed) { " + snapshot_defers(true);
    string fnr = fn_c_ret(current_fn);
    if (fn_fallible() && fnr != rty) {
        s += "return ((" + fnr + "){ .error = " + rn + ".error, .failed = true }); } ";
    } else {
        s += "return " + rn + "; } ";
    }
    if (payload == nullptr || payload->kind == TypeKind::Unit || payload->kind == TypeKind::Never) {
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
    bool else_never = is_never_expr(n->right);
    auto never_stmt = [&]() -> string {
        string rhs = emit_expr(n->right);
        if (is_trap_call(n->right)) {
            return rhs;
        }
        if (is_error_call(n->right)) {
            if (rhs.size() < 7 || rhs.compare(0, 7, "return ") != 0) {
                rhs = "return " + rhs;
            }
        }
        return rhs;
    };
    if (is_opt(lt)) {
        if (else_never) {
            s += "if (!" + on + ".present) { " + never_stmt() + "; } " + on + ".value; })";
        } else {
            s += on + ".present ? " + on + ".value : (" + emit_expr(n->right) + "); })";
        }
    } else if (else_never) {
        s += "if (!(" + on + ")) { " + never_stmt() + "; } " + on + "; })";
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
    string saved_done = catch_done;
    string saved_out;
    saved_out.swap(out);
    int saved_indent = indent;
    catch_var = vn;
    catch_done = "_lb_cd" + std::to_string(id);
    indent = 0;
    emit_stmt(n->body);
    string body;
    body.swap(out);
    out.swap(saved_out);
    indent = saved_indent;
    catch_var = saved_catch;
    catch_done = saved_done;
    Type* payload = is_fail(ft) ? ft->elem : nullptr;
    string s = "({ ";
    s += rty + " " + rn + " = " + emit_expr(n->left) + "; ";
    s += vty + " " + vn + " = {0}; ";
    s += "if (" + rn + ".failed) { ";
    if (!n->text.empty()) {
        s += "lb_error " + ident("lb_", n->text) + " __attribute__((unused)) = " + rn + ".error; ";
    }
    s += body;
    s += "_lb_cd" + std::to_string(id) + ": __attribute__((unused));";
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
    if (n->ty != nullptr &&
        (n->ty->kind == TypeKind::Interface || n->ty->kind == TypeKind::Allocator)) {
        Type* src = nullptr;
        if (n->kind == NodeKind::Unary && n->op == TokenKind::Amp && n->left != nullptr) {
            src = n->left->ty;
        } else if (n->kind == NodeKind::Name && n->resolved != nullptr) {
            src = n->resolved->ty;
        }
        if (is_ptr(src)) {
            src = src->elem;
        }
        if (src != nullptr && src->kind == TypeKind::Struct && src->name == "FixedBuffer" &&
            src->decl != nullptr && (src->decl->flags & FlagBuiltin) != 0) {
            string data = e;
            if (n->kind == NodeKind::Name || n->kind == NodeKind::Member) {
                data = "&(" + e + ")";
            }
            return "lb_fixed_alloc(" + data + ")";
        }
        if (src != nullptr && src->kind == TypeKind::Struct && src->decl != nullptr &&
            (n->ty->decl != nullptr || n->ty->kind == TypeKind::Allocator)) {
            string data = e;
            if (n->kind == NodeKind::Name || n->kind == NodeKind::Member) {
                data = "&(" + e + ")";
            }
            string iface = n->ty->kind == TypeKind::Allocator ? string("Allocator")
                                                              : string(n->ty->decl->text);
            return "((lb_iface){ (void*)(" + data + "), &lb_vt_" + string(src->decl->text) + "_" +
                   iface + " })";
        }
    }
    return e;
}

auto Emitter::emit_expr_inner(Node* n) -> string {
    switch (n->kind) {
    case NodeKind::Literal:
        return emit_literal(n);
    case NodeKind::Name:
        if (n->flags & FlagFormatSink) {
            return current_sink;
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::EnumCase) {
            return emit_enum_value(n);
        }
        if (n->resolved != nullptr &&
            (n->resolved->kind == NodeKind::Func || n->resolved->kind == NodeKind::ExternFunc)) {
            return func_ident(n->resolved, nullptr);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::ExternVar) {
            return string(n->text);
        }
        if (is_span(n->ty) && n->resolved != nullptr && is_array(n->resolved->ty)) {
            string nm = name_ident(n);
            char nbuf[32];
            snprintf(nbuf, sizeof(nbuf), "%lluULL",
                     static_cast<unsigned long long>(n->resolved->ty->length));
            string sty = c_type(n->ty);
            return "((" + sty + "){" + nm + ".d, " + nbuf + "})";
        }
        return name_ident(n);
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
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
            Node* owner = n->left != nullptr ? n->left->resolved : nullptr;
            if (owner != nullptr && owner->kind != NodeKind::Struct &&
                owner->kind != NodeKind::Enum && owner->kind != NodeKind::Union) {
                owner = nullptr;
            }
            return func_ident(n->resolved, owner);
        }
        return emit_member(n);
    case NodeKind::Lambda:
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
            return func_ident(n->resolved, nullptr);
        }
        return "((void*)0)";
    case NodeKind::Tuple: {
        string s = "((" + tup_c_name(n->ty) + "){";
        bool first = true;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            if (!first) {
                s += ", ";
            }
            first = false;
            s += emit_expr(e);
        }
        s += "})";
        return s;
    }
    case NodeKind::Conditional:
        return "(" + emit_expr(n->type) + " ? " + emit_expr(n->left) + " : " + emit_expr(n->right) +
               ")";
    case NodeKind::Cast:
        return emit_conv(n->left,
                         n->type != nullptr && n->type->ty != nullptr ? n->type->ty : n->ty, false);
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
    case NodeKind::Formatted: {
        int id = tmp();
        string buf = "_lb_fb" + std::to_string(id);
        string bn = "_lb_ff" + std::to_string(id);
        // the buffer is the function's (`insert_format_decls`), not this expression's
        format_decls.push_back("char " + buf + "[1024]; lb_fmtbuf " + bn + ";");
        string s = "({ " + bn + " = (lb_fmtbuf){ " + buf + ", 1024, 0 }; ";
        for (Node* p = n->body; p != nullptr; p = p->next) {
            if (p->kind == NodeKind::FormatText) {
                string d = unescape_format_braces(decode_lit(p->text));
                s += "(void)lb_fmtbuf_put(&" + bn + ", " + c_escape(d) + ", " +
                     std::to_string(d.size()) + "); ";
            } else if (p->kind == NodeKind::FormatField) {
                s += "(void)" + emit_display_buf(bn, p->left) + "; ";
            }
        }
        s += "lb_fmtbuf_finish(&" + bn + "); })";
        return s;
    }
    case NodeKind::Catch:
        return emit_catch(n);
    case NodeKind::New:
        return emit_new(n);
    case NodeKind::Alloc:
        return emit_alloc(n);
    case NodeKind::Match:
    case NodeKind::MatchExpr:
        return emit_match_expr(n);
    case NodeKind::Return: {
        if (n->left != nullptr && n->left->kind == NodeKind::Call && n->left->left != nullptr &&
            n->left->left->kind == NodeKind::Name && n->left->left->text == "trap") {
            return emit_expr(n->left);
        }
        if (is_error_call(n->left)) {
            return "return " + emit_expr(n->left);
        }
        if (never_valued(n->left)) {
            // `else return try never_call()`: the call leaves on its own.
            return "(void)(" + emit_expr(n->left) + "); __builtin_unreachable()";
        }
        if (n->left == nullptr) {
            if (fn_fallible()) {
                return "return " + wrap_ok("0");
            }
            return "return";
        }
        string e = emit_expr(n->left);
        if (unit_valued(n->left)) {
            // `return call()` of a unit call: the call runs, the result is empty.
            return "(void)(" + e + "); return " + (fn_fallible() ? wrap_ok("0") : string());
        }
        if (fn_fallible()) {
            e = wrap_ok(e);
        }
        return "return " + e;
    }
    case NodeKind::Break:
        return n->text.empty() ? string("break") : string("goto _lb_break_") + string(n->text);
    case NodeKind::Continue:
        return n->text.empty() ? string("continue") : string("goto _lb_cont_") + string(n->text);
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
        if (n->ty != nullptr && (n->ty->kind == TypeKind::CStr ||
                                 (is_opt(n->ty) && n->ty->elem != nullptr && n->ty->elem->kind == TypeKind::CStr))) {
            return c_escape(d);
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", d.size());
        return "((lb_str){" + c_escape(d) + ", " + buf + "})";
    }
    if (n->op == TokenKind::BytesLit) {
        // `b"..."`: an array of `u8`, spelled byte by byte
        string d = decode_lit(n->text);
        string s = "((" + c_type(n->ty) + "){{";
        for (size_t i = 0; i < d.size(); i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%s%u", i == 0 ? "" : ", ", static_cast<unsigned char>(d[i]));
            s += buf;
        }
        return s + "}})";
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
        // the bits the literal rounds to at its own width (base.md §4.3), as C's exact
        // hexadecimal float: the C compiler neither rounds again nor refuses a small value
        int width = n->ty != nullptr && n->ty->kind == TypeKind::F16 ? 16
                  : n->ty != nullptr && n->ty->kind == TypeKind::F32 ? 32 : 64;
        uint64_t bits = 0;
        decimal_to_bits(p.body, width, &bits);
        return hex_float(bits, width);
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
    // under an optional context the operator computes in the payload's type; emit_expr wraps
    Type* t = is_opt(n->ty) && !is_ptr(n->ty) && !is_func(n->ty) ? n->ty->elem : n->ty;
    string x = emit_expr(n->left);
    if (is_vector(t)) {
        return emit_vector_unary(n, t, x);
    }
    if (n->op == TokenKind::Minus && !is_float(t) && n->left != nullptr &&
        n->left->kind == NodeKind::Literal && n->left->op == TokenKind::IntLit) {
        ParsedInt p = parse_int_literal(n->left->text);
        if (p.ok && t != nullptr &&
            p.value == static_cast<uint64_t>(int_max_signed(int_bits(t))) + 1) {
            char buf[64];
            snprintf(buf, sizeof(buf), "(int64_t)(1ULL << %d)", int_bits(t) - 1);
            return down_cast(t, buf);
        }
    }
    return emit_unary_scalar(t, n->op, x);
}

// `~x`, `-x`, and `-%x` on one scalar of type `t`.
auto Emitter::emit_unary_scalar(Type* t, TokenKind op, const string& x) -> string {
    if (op == TokenKind::Tilde) {
        if (t != nullptr && is_unsigned_int(t)) {
            return down_cast(t, "lb_not_u(" + word_cast(t, x) + ", " + bits_lit(t) + ")");
        }
        return down_cast(t, "~(" + x + ")");
    }
    if (is_float(t)) {
        return "(-(" + x + "))";
    }
    if (op == TokenKind::MinusPercent) {
        const char* h = is_signed_int(t) ? "lb_negw_s" : "lb_negw_u";
        return down_cast(t, string(h) + "(" + word_cast(t, x) + ", " + bits_lit(t) + ")");
    }
    if (op == TokenKind::Minus) {
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

// `a == b` for values of type `t`, component by component (§7.4); `a` and `b`
// are simple C lvalues, so they may appear as often as the type needs.
auto Emitter::emit_equal(Type* t, const string& a, const string& b) -> string {
    if (t == nullptr) {
        return "(" + a + " == " + b + ")";
    }
    if (is_opt(t) && !is_ptr(t)) {
        return "((" + a + ".present == " + b + ".present) && (!" + a + ".present || " +
               emit_equal(t->elem, a + ".value", b + ".value") + "))";
    }
    if (t->kind == TypeKind::Str) {
        return "(" + a + ".length == " + b + ".length && (" + a + ".length == 0 || memcmp(" + a +
               ".data, " + b + ".data, " + a + ".length) == 0))";
    }
    if (t->kind == TypeKind::Struct && t->decl != nullptr) {
        string out = "(1";
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                out += " && " + emit_equal(m->ty, a + "." + string(m->text), b + "." + string(m->text));
            }
        }
        return out + ")";
    }
    if (t->kind == TypeKind::Tuple) {
        string out = "(1";
        for (int i = 0; i < t->ntargs; i++) {
            string f = ".a" + std::to_string(i);
            out += " && " + emit_equal(t->args[i], a + f, b + f);
        }
        return out + ")";
    }
    if (t->kind == TypeKind::Array) {
        int id = tmp();
        string i = "_lb_ei" + std::to_string(id);
        string e = "_lb_ee" + std::to_string(id);
        return "({ int " + e + " = 1; for (size_t " + i + " = 0; " + e + " && " + i + " < " +
               std::to_string(t->length) + "ULL; " + i + "++) { " + e + " = " +
               emit_equal(t->elem, a + ".d[" + i + "]", b + ".d[" + i + "]") + "; } " + e + "; })";
    }
    if (t->kind == TypeKind::Enum && !is_int_enum(t) && t->decl != nullptr) {
        string out = "(" + a + ".tag == " + b + ".tag";
        int index = 0;
        for (Node* c = t->decl->body; c != nullptr; c = c->next) {
            if (c->kind != NodeKind::EnumCase) {
                continue;
            }
            if (c->body != nullptr) {
                string fields = "(1";
                for (Node* p = c->body; p != nullptr; p = p->next) {
                    string f = ".u." + string(c->text) + "." + string(p->text);
                    fields += " && " + emit_equal(p->ty, a + f, b + f);
                }
                out += " && (" + a + ".tag != " + std::to_string(index) + " || " + fields + "))";
            }
            index++;
        }
        return out + ")";
    }
    return "(" + a + " == " + b + ")";
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
    // under an optional context the operator computes in the payload's type; emit_expr wraps
    Type* t = is_opt(n->ty) && !is_ptr(n->ty) && !is_func(n->ty) ? n->ty->elem : n->ty;
    Type* result = t;
    if (is_opt(t) && op != TokenKind::PlusQuestion && op != TokenKind::MinusQuestion &&
        op != TokenKind::StarQuestion) {
        t = t->elem;
    }
    Type* lt = n->left != nullptr ? n->left->ty : nullptr;
    Type* rt = n->right != nullptr ? n->right->ty : nullptr;
    // Tagged optionals: `x == none` is a presence test; `x == y` compares
    // presence, then the payloads when both are present.
    bool tagged = (is_opt(lt) && !is_ptr(lt)) || (is_opt(rt) && !is_ptr(rt));
    if (tagged && (op == TokenKind::EqEq || op == TokenKind::NotEq)) {
        bool none_left = n->left->kind == NodeKind::Literal && n->left->op == TokenKind::KwNone;
        bool none_right = n->right->kind == NodeKind::Literal && n->right->op == TokenKind::KwNone;
        string test;
        if (none_left || none_right) {
            int id = tmp();
            string on = "_lb_on" + std::to_string(id);
            string side = none_left ? R : L;
            test = "({ " + c_type(none_left ? rt : lt) + " " + on + " = " + side + "; !" + on +
                   ".present; })";
        } else {
            int id = tmp();
            string a = "_lb_oa" + std::to_string(id);
            string b = "_lb_ob" + std::to_string(id);
            test = "({ " + c_type(lt) + " " + a + " = " + L + "; " + c_type(rt) + " " + b + " = " +
                   R + "; (" + a + ".present == " + b + ".present) && (!" + a + ".present || " +
                   emit_equal(lt->elem, a + ".value", b + ".value") + "); })";
        }
        return op == TokenKind::EqEq ? "(" + test + ")" : "(!" + test + ")";
    }
    if ((op == TokenKind::EqEq || op == TokenKind::NotEq) && lt != nullptr &&
        (lt->kind == TypeKind::Struct || lt->kind == TypeKind::Tuple ||
         lt->kind == TypeKind::Array || (lt->kind == TypeKind::Enum && !is_int_enum(lt)))) {
        int id = tmp();
        string a = "_lb_ea" + std::to_string(id);
        string b = "_lb_eb" + std::to_string(id);
        string test = "({ " + c_type(lt) + " " + a + " = " + L + "; " + c_type(lt) + " " + b +
                      " = " + R + "; " + emit_equal(lt, a, b) + "; })";
        return op == TokenKind::EqEq ? "(" + test + ")" : "(!" + test + ")";
    }
    if (is_vector(t)) {
        return emit_vector_binary(n, t, L, R);
    }
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
        const char* cop = op == TokenKind::Lt     ? "<"
                          : op == TokenKind::LtEq ? "<="
                          : op == TokenKind::Gt   ? ">"
                                                  : ">=";
        return "(" + L + " " + cop + " " + R + ")";
    }
    Type* ct = n->left != nullptr && n->left->ty != nullptr ? n->left->ty : t;
    if ((lt != nullptr && lt->kind == TypeKind::Str) ||
        (rt != nullptr && rt->kind == TypeKind::Str)) {
        if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
            int id = tmp();
            string a = "_lb_sa" + std::to_string(id);
            string b = "_lb_sb" + std::to_string(id);
            string eq = "({ lb_str " + a + " = " + L + "; lb_str " + b + " = " + R + "; (" + a +
                        ".length == " + b + ".length && (" + a + ".length == 0 || memcmp(" + a +
                        ".data, " + b + ".data, " + a + ".length) == 0)); })";
            if (op == TokenKind::NotEq) {
                return "(!" + eq + ")";
            }
            return eq;
        }
        if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
            op == TokenKind::GtEq) {
            const char* cop = op == TokenKind::Lt     ? "<"
                              : op == TokenKind::LtEq ? "<="
                              : op == TokenKind::Gt   ? ">"
                                                      : ">=";
            return "(lb_str_compare(" + L + ", " + R + ") " + cop + " 0)";
        }
    }
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
        const char* cop = op == TokenKind::Lt     ? "<"
                          : op == TokenKind::LtEq ? "<="
                          : op == TokenKind::Gt   ? ">"
                                                  : ">=";
        return "(" + a + " " + cop + " " + b + ")";
    }
    if (op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
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
    return emit_arith(t, op, L, R);
}

// One scalar arithmetic, wrapping, saturating, bit, or shift operation of §7.2 and §7.3 on
// operands `L` and `R` of type `t`: a C operator for floats and bits, a runtime helper for
// the checked and modular integer forms.
auto Emitter::emit_arith(Type* t, TokenKind op, const string& L, const string& R) -> string {
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

// Lane-wise arithmetic (§5.12): the operands are bound once, each lane of the result is the
// scalar operation on the operands' lanes, a scalar operand standing for every lane.
auto Emitter::emit_vector_binary(Node* n, Type* vt, const string& L, const string& R) -> string {
    Type* et = vt->elem;
    Type* lt = n->left->ty;
    Type* rt = n->right->ty;
    const int id = tmp();
    const string a = "_lb_va" + std::to_string(id);
    const string b = "_lb_vb" + std::to_string(id);
    const string r = "_lb_vr" + std::to_string(id);
    const bool lv = is_array(lt);
    const bool rv = is_array(rt);
    string s = "({ " + c_type(lt) + " " + a + " = " + L + "; " + c_type(rt) + " " + b + " = " + R +
               "; " + c_type(vt) + " " + r + "; ";
    for (uint64_t i = 0; i < vt->length; i++) {
        const string k = std::to_string(i);
        const string ai = lv ? a + ".d[" + k + "]" : a;
        const string bi = rv ? b + ".d[" + k + "]" : b;
        s += r + ".d[" + k + "] = " + emit_arith(et, n->op, ai, bi) + "; ";
    }
    return s + r + "; })";
}

auto Emitter::emit_vector_unary(Node* n, Type* vt, const string& x) -> string {
    const int id = tmp();
    const string a = "_lb_va" + std::to_string(id);
    const string r = "_lb_vr" + std::to_string(id);
    string s = "({ " + c_type(vt) + " " + a + " = " + x + "; " + c_type(vt) + " " + r + "; ";
    for (uint64_t i = 0; i < vt->length; i++) {
        const string k = std::to_string(i);
        s += r + ".d[" + k + "] = " + emit_unary_scalar(vt->elem, n->op, a + ".d[" + k + "]") + "; ";
    }
    return s + r + "; })";
}

// `T[N](x)`: every lane is `x`, computed once (§5.12); in a global's initialiser `x` is a
// constant and the lanes are a compound literal.
auto Emitter::emit_splat(Node* n) -> string {
    Type* vt = n->ty;
    if (at_file_scope) {
        const string x = emit_expr(n->body != nullptr ? n->body->left : nullptr);
        string s = "((" + c_type(vt) + "){{";
        for (uint64_t i = 0; i < vt->length; i++) {
            s += (i == 0 ? "" : ", ") + x;
        }
        return s + "}})";
    }
    const int id = tmp();
    const string a = "_lb_va" + std::to_string(id);
    const string r = "_lb_vr" + std::to_string(id);
    string s = "({ " + c_type(vt->elem) + " " + a + " = " +
               emit_expr(n->body != nullptr ? n->body->left : nullptr) + "; " + c_type(vt) + " " + r + "; ";
    for (uint64_t i = 0; i < vt->length; i++) {
        s += r + ".d[" + std::to_string(i) + "] = " + a + "; ";
    }
    return s + r + "; })";
}

// `v.sum()`, `v.min()`, `v.max()`: the lanes folded in order (§5.12); a float `min` or `max`
// skips a NaN lane when another lane compares.
auto Emitter::emit_vector_fold(Node* obj, Node* n) -> string {
    Type* vt = obj->ty;
    Type* et = vt->elem;
    const int id = tmp();
    const string v = "_lb_va" + std::to_string(id);
    const string acc = "_lb_vf" + std::to_string(id);
    const string which = string(n->left->text);
    string s = "({ " + c_type(vt) + " " + v + " = " + emit_expr(obj) + "; " + c_type(et) + " " + acc +
               " = " + v + ".d[0]; ";
    for (uint64_t i = 1; i < vt->length; i++) {
        const string lane = v + ".d[" + std::to_string(i) + "]";
        if (which == "sum") {
            s += acc + " = " + emit_arith(et, TokenKind::Plus, acc, lane) + "; ";
        } else {
            const string nan = is_float(et) ? acc + " != " + acc + " || " : string();
            s += "if (" + nan + lane + (which == "min" ? " < " : " > ") + acc + ") " + acc + " = " + lane + "; ";
        }
    }
    return s + acc + "; })";
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
        uint64_t v = (c->flags & FlagLiteralCached) != 0 ? c->cached : next;
        if (!first) {
            s += " || ";
        }
        first = false;
        s += "_lb_e == (" + c_type(dest) + ")" + std::to_string(v) + "ull";
        next = v + 1;
    }
    if (first) {
        s += "0";
    }
    s += ")) lb_trap(\"invalid enum value\"); _lb_e; })";
    return s;
}

auto Emitter::emit_conv(Node* src, Type* dest, bool checked) -> string {
    Type* st = src != nullptr ? src->ty : nullptr;
    if (dest != nullptr && dest->kind == TypeKind::Str &&
        (st != nullptr &&
         (st->kind == TypeKind::CStr || ((is_span(st) || is_array(st)) && st->elem != nullptr &&
                                         st->elem->kind == TypeKind::U8)))) {
        return emit_str_conv(src, checked);
    }
    string e = emit_expr(src);
    if (dest == nullptr) {
        return e;
    }
    if (dest->kind == TypeKind::CStr && st != nullptr && st->kind == TypeKind::Str) {
        return "lb_cstr_of(" + e + ")"; // the byte after the text is NUL, or the cast traps (§5.2)
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
    if (st != nullptr && st->kind == TypeKind::ErrorCode && is_int(dest)) {
        return down_cast(dest, e);
    }
    if (is_int(st) && dest != nullptr && dest->kind == TypeKind::ErrorCode) {
        return "(uint32_t)(" + e + ")";
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
        if (dest->kind == TypeKind::F16) {
            return "(_Float16)" + call;
        }
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
                      std::to_string(fs) + ", " + std::to_string(tb) + ", " + std::to_string(ts) +
                      ", " + std::to_string(mode) + ")";
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
        if (ot->name == "luce") {
            if (n->text == "location") {
                return emit_src_location(n);
            }
            if (n->text == "file") {
                return emit_src_file();
            }
            if (n->text == "line") {
                char lbuf[16];
                snprintf(lbuf, sizeof(lbuf), "%uu", n->span.line);
                return lbuf;
            }
            if (n->text == "function") {
                return emit_src_function();
            }
        }
        if (ot->name == "files" && n->text == "missing") {
            return "2";
        }
        // `module.constant`: a public top-level binding of another module.
        if (n->resolved != nullptr &&
            (n->resolved->kind == NodeKind::Const || n->resolved->kind == NodeKind::Global)) {
            return global_ident(n->resolved);
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
    if (n->text == "bytes" && raw != nullptr && raw->kind == TypeKind::Str) {
        return "((lb_cspan){(void*)(" + base + acc + "data), " + base + acc + "length})";
    }
    if (n->left != nullptr && n->left->kind == NodeKind::Self) {
        return "self->" + string(n->text);
    }
    if (ptr) {
        return "(" + base + ")->" + string(n->text);
    }
    return base + "." + string(n->text);
}

auto Emitter::emit_index(Node* n, bool one_past) -> string {
    Type* bt = n->left != nullptr ? n->left->ty : nullptr;
    string b = emit_expr(n->left);
    string i = emit_expr(n->body);
    // under `&` the index may equal the length: C's one past the end (§7.7)
    const uint64_t slack = one_past ? 1 : 0;
    if (is_array(bt)) {
        char nbuf[32];
        snprintf(nbuf, sizeof(nbuf), "%lluULL", static_cast<unsigned long long>(bt->length + slack));
        return "((" + b + ").d[(lb_check_index((uint64_t)(" + i + "), " + nbuf + "), " + i + ")])";
    }
    if (is_span(bt) || (bt != nullptr && bt->kind == TypeKind::Str)) {
        Type* et = is_span(bt) ? bt->elem : n->ty;
        if (bt != nullptr && bt->kind == TypeKind::Str) {
            et = nullptr;
        }
        string elem = et != nullptr ? c_type(et) : "uint8_t";
        return "(((" + elem + "*)" + b + ".data)[(lb_check_index((uint64_t)(" + i + "), " + b +
               (one_past ? ".length + 1ULL), " : ".length), ") + i + ")])";
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
        snprintf(nbuf, sizeof(nbuf), "%lluULL", static_cast<unsigned long long>(bt->length));
        len = nbuf;
        data = b + ".d";
    } else {
        len = b + ".length";
        // A span's data is `void*`; index in elements, not bytes.
        if (bt != nullptr && bt->elem != nullptr && bt->kind != TypeKind::Str) {
            data = "((" + c_type(bt->elem) + "*)(" + b + ".data))";
        } else {
            data = b + ".data";
        }
    }
    end = n->right != nullptr ? emit_expr(n->right) : len;
    string span_ty = n->ty != nullptr && n->ty->is_const ? "lb_cspan" : "lb_span";
    return "((void)lb_check_index((uint64_t)(" + start + "), (uint64_t)(" + len +
           ") + 1), (void)lb_check_index((uint64_t)(" + end + "), (uint64_t)(" + len + ") + 1), (" +
           span_ty + "){(void*)((" + data + ") + (" + start + ")), (size_t)((" + end + ") - (" +
           start + "))})";
}

auto Emitter::emit_array_lit(Node* n) -> string {
    if (n->ty != nullptr && n->ty->kind == TypeKind::Span) {
        // a literal where a span is wanted (§5.4): an array of the function's, declared at
        // its top like a format buffer, so the span outlives the expression that filled it
        int count = 0;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            count++;
        }
        const string span_ty = n->ty->is_const ? "lb_cspan" : "lb_span";
        if (count == 0) {
            return "((" + span_ty + "){(void*)8, 0})";
        }
        int id = tmp();
        string name = "_lb_ad" + std::to_string(id);
        format_decls.push_back(c_type(n->ty->elem) + " " + name + "[" + std::to_string(count) + "];");
        string s = "(";
        int i = 0;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            s += name + "[" + std::to_string(i) + "] = " + emit_expr(e) + ", ";
            i++;
        }
        s += "(" + span_ty + "){(void*)" + name + ", " + std::to_string(count) + "})";
        return s;
    }
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
    return "((lb_span){(void*)(" + p + "), (size_t)(" + len + ")})";
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
            return name_ident(n) + ".d";
        }
        return "&" + name_ident(n);
    }
    if (n->kind == NodeKind::Member) {
        return "&(" + emit_member(n) + ")";
    }
    if (n->kind == NodeKind::Index) {
        return "&(" + emit_index(n, true) + ")";
    }
    if (n->kind == NodeKind::Unary && n->op == TokenKind::Star) {
        return emit_expr(n->left);
    }
    return "&(" + emit_expr(n) + ")";
}

auto Emitter::vt_type_name(Type* iface) -> string {
    string n = iface != nullptr && iface->decl != nullptr ? string(iface->decl->text) : string("I");
    return "lb_vt_" + n;
}

auto Emitter::vt_instance_name(Node* st, Node* iface) -> string {
    return "lb_vt_" + string(st->text) + "_" + string(iface->text);
}

} // namespace lucb
