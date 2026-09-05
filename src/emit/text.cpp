//==============================================================================================
//
//   emit/text - Formatted output, text conversion, and hashing
//
//   DESCRIPTION:
//       `print`, `Writer.write`, and `format` consume a formatted string piece by piece into
//       a caller buffer or a `Writer` view without materialising a string (base.md §4.4,
//       §14.4); `str(bytes)` validates UTF-8 and yields `str!` (§5.5); `hash`, `hex`, `bin`,
//       and `pad` are the compiler-supplied Display forms (§7.4).
//
//==============================================================================================

#include "emit/emitter.h"

#include "support/literal.h"
#include <cinttypes>

namespace lucb {

auto Emitter::emit_str_conv(Node* src, bool checked) -> string {
    Type* st = src != nullptr ? src->ty : nullptr;
    string e = emit_expr(src);
    int id = tmp();
    string sn = "_lb_sc" + std::to_string(id);
    string bn = "_lb_sb" + std::to_string(id);
    string ln = "_lb_sl" + std::to_string(id);
    string bind = "({ ";
    if (st != nullptr && st->kind == TypeKind::CStr) {
        bind += "const char* " + bn + " = " + e + "; size_t " + ln + " = " + bn +
                " == NULL ? 0 : strlen(" + bn + "); ";
    } else if (is_array(st)) {
        bind += c_type(st) + " " + sn + " = " + e + "; const char* " + bn + " = (const char*)" +
                sn + ".d; size_t " + ln + " = " + std::to_string(st->length) + "ULL; ";
    } else {
        string sty = st != nullptr && st->is_const ? "lb_cspan" : "lb_span";
        bind += sty + " " + sn + " = " + e + "; const char* " + bn + " = (const char*)" + sn +
                ".data; size_t " + ln + " = " + sn + ".length; ";
    }
    if (!checked) {
        return bind + "(lb_str){ " + bn + ", " + ln + " }; })";
    }
    string rn = "_lb_sr" + std::to_string(id);
    bind += "lb_r_str " + rn + "; ";
    bind += "if (!lb_utf8_ok(" + bn + ", " + ln + ")) { " + rn + ".failed = true; " + rn +
            ".error = (lb_error){ .code = LB_INVALID_UTF8, .message = (lb_str){\"invalid_utf8\", "
            "12} }; } else { " +
            rn + ".failed = false; " + rn + ".value = (lb_str){ " + bn + ", " + ln + " }; } " + rn +
            "; })";
    return bind;
}

auto Emitter::emit_display_buf(const string& b, Node* v) -> string {
    Type* t = v != nullptr ? v->ty : nullptr;
    string e = emit_expr(v);
    if (t != nullptr && t->kind == TypeKind::Bool) {
        return "lb_fmtbuf_bool(&" + b + ", " + e + ")";
    }
    if (t != nullptr && (t->kind == TypeKind::Str || t->kind == TypeKind::Fmt)) {
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

auto Emitter::emit_hash_of(Type* t, const string& e) -> string {
    if (t == nullptr) {
        return "0";
    }
    if (t->kind == TypeKind::Str) {
        return "lb_hash_bytes(lb_hash_seed(), " + e + ".data, " + e + ".length)";
    }
    if (t->kind == TypeKind::Bool) {
        return "lb_hash_mix(lb_hash_seed(), (uint64_t)(" + e + " ? 1 : 0))";
    }
    if (is_float(t)) {
        int id = tmp();
        string vn = "_lb_hf" + std::to_string(id);
        string bits = t->kind == TypeKind::F32 ? "uint32_t" : "uint64_t";
        return "({ " + c_type(t) + " " + vn + " = " + e + "; " + bits + " _lb_hb" +
               std::to_string(id) + "; memcpy(&_lb_hb" + std::to_string(id) + ", &" + vn +
               ", sizeof(" + vn + ")); lb_hash_mix(lb_hash_seed(), (uint64_t)_lb_hb" +
               std::to_string(id) + "); })";
    }
    if (is_ptr(t) || t->kind == TypeKind::CStr) {
        return "lb_hash_mix(lb_hash_seed(), (uint64_t)(uintptr_t)(" + e + "))";
    }
    if (is_int(t) || t->kind == TypeKind::Char || is_int_enum(t)) {
        return "lb_hash_mix(lb_hash_seed(), (uint64_t)(" + e + "))";
    }
    if (is_array(t)) {
        int id = tmp();
        string vn = "_lb_ha" + std::to_string(id);
        string h = "_lb_hh" + std::to_string(id);
        string i = "_lb_hi" + std::to_string(id);
        string s = "({ " + c_type(t) + " " + vn + " = " + e + "; uint64_t " + h +
                   " = lb_hash_seed(); size_t " + i + " = 0; for (; " + i + " < " +
                   std::to_string(t->length) + "ULL; " + i + "++) { " + h + " = lb_hash_mix(" + h +
                   ", " + emit_hash_of(t->elem, vn + ".d[" + i + "]") + "); } " + h + "; })";
        return s;
    }
    if (is_opt(t)) {
        int id = tmp();
        string vn = "_lb_ho" + std::to_string(id);
        return "({ " + c_type(t) + " " + vn + " = " + e + "; " + vn + ".present ? lb_hash_mix(" +
               emit_hash_of(t->elem, vn + ".value") + ", 1) : lb_hash_mix(lb_hash_seed(), 0); })";
    }
    if (is_tup(t)) {
        int id = tmp();
        string vn = "_lb_ht" + std::to_string(id);
        string s = "({ " + c_type(t) + " " + vn + " = " + e + "; uint64_t _lb_hh" +
                   std::to_string(id) + " = lb_hash_seed(); ";
        for (int i = 0; i < t->ntargs; i++) {
            s += "_lb_hh" + std::to_string(id) + " = lb_hash_mix(_lb_hh" + std::to_string(id) +
                 ", " + emit_hash_of(t->args[i], vn + ".a" + std::to_string(i)) + "); ";
        }
        s += "_lb_hh" + std::to_string(id) + "; })";
        return s;
    }
    if (t->kind == TypeKind::Struct && t->decl != nullptr) {
        int id = tmp();
        string vn = "_lb_hs" + std::to_string(id);
        string s = "({ " + c_type(t) + " " + vn + " = " + e + "; uint64_t _lb_hh" +
                   std::to_string(id) + " = lb_hash_seed(); ";
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::Field) {
                continue;
            }
            s += "_lb_hh" + std::to_string(id) + " = lb_hash_mix(_lb_hh" + std::to_string(id) +
                 ", " + emit_hash_of(m->ty, vn + "." + string(m->text)) + "); ";
        }
        s += "_lb_hh" + std::to_string(id) + "; })";
        return s;
    }
    if (t->kind == TypeKind::Enum && t->decl != nullptr && !is_int_enum(t)) {
        int id = tmp();
        string vn = "_lb_he" + std::to_string(id);
        return "({ " + c_type(t) + " " + vn + " = " + e +
               "; lb_hash_mix(lb_hash_seed(), "
               "(uint64_t)" +
               vn + ".tag); })";
    }
    return "lb_hash_mix(lb_hash_seed(), (uint64_t)(" + e + "))";
}

auto Emitter::emit_hash(Node* n) -> string {
    Node* arg = n->body != nullptr ? n->body->left : nullptr;
    Type* t = arg != nullptr ? arg->ty : nullptr;
    return emit_hash_of(t, emit_expr(arg));
}

auto Emitter::emit_print_formatted(Node* n) -> string {
    string s = "({ ";
    for (Node* p = n != nullptr ? n->body : nullptr; p != nullptr; p = p->next) {
        if (p->kind == NodeKind::FormatText) {
            string d = unescape_format_braces(decode_lit(p->text));
            s += "fputs(" + c_escape(d) + ", stdout); ";
        } else if (p->kind == NodeKind::FormatField) {
            Type* t = p->left != nullptr ? p->left->ty : nullptr;
            string e = emit_expr(p->left);
            if (t != nullptr && t->kind == TypeKind::Bool) {
                s += "fputs((" + e + ") ? \"true\" : \"false\", stdout); ";
            } else if (t != nullptr && (t->kind == TypeKind::Str || t->kind == TypeKind::Fmt)) {
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
                string d = unescape_format_braces(decode_lit(p->text));
                s += rn + " = " + rn + " || lb_fmtbuf_put(&" + bn + ", " + c_escape(d) + ", " +
                     std::to_string(d.size()) + "); ";
            } else if (p->kind == NodeKind::FormatField) {
                s += rn + " = " + rn + " || " + emit_display_buf(bn, p->left) + "; ";
            }
        }
    } else {
        string e = emit_expr(msg);
        s += rn + " = " + rn + " || lb_fmtbuf_put(&" + bn + ", " + e + ".data, " + e + ".length); ";
    }
    s += "lb_r_str _lb_out" + std::to_string(id) + "; ";
    s += "if (" + rn + ") { _lb_out" + std::to_string(id) +
         " = ((lb_r_str){ .error = { .code = LB_MEMORY_EXHAUSTED, .message = "
         "(lb_str){\"memory.exhausted\", 16} }, .failed = true }); } else { _lb_out" +
         std::to_string(id) + " = ((lb_r_str){ .value = lb_fmtbuf_finish(&" + bn +
         "), .failed = false }); } _lb_out" + std::to_string(id) + "; })";
    return s;
}

auto Emitter::emit_as_cspan(Node* n) -> string {
    if (n == nullptr) {
        return "((lb_cspan){NULL, 0})";
    }
    if (n->kind == NodeKind::Formatted) {
        int id = tmp();
        string buf = "_lb_wb" + std::to_string(id);
        string bn = "_lb_wf" + std::to_string(id);
        string s = "({ char " + buf + "[1024]; lb_fmtbuf " + bn + " = { " + buf + ", 1024, 0 }; ";
        for (Node* p = n->body; p != nullptr; p = p->next) {
            if (p->kind == NodeKind::FormatText) {
                string d = unescape_format_braces(decode_lit(p->text));
                s += "(void)lb_fmtbuf_put(&" + bn + ", " + c_escape(d) + ", " +
                     std::to_string(d.size()) + "); ";
            } else if (p->kind == NodeKind::FormatField) {
                s += "(void)" + emit_display_buf(bn, p->left) + "; ";
            }
        }
        s += "(lb_cspan){ (void*)(" + bn + ".data), " + bn + ".used }; })";
        return s;
    }
    Type* t = n->ty;
    if (t != nullptr && (t->kind == TypeKind::Str || t->kind == TypeKind::Fmt)) {
        int id = tmp();
        string vn = "_lb_sp" + std::to_string(id);
        return "({ lb_str " + vn + " = " + emit_expr(n) + "; (lb_cspan){ (void*)(" + vn +
               ".data), " + vn + ".length }; })";
    }
    if (is_span(t)) {
        int id = tmp();
        string vn = "_lb_cs" + std::to_string(id);
        string sty = t->is_const ? "lb_cspan" : "lb_span";
        return "({ " + sty + " " + vn + " = " + emit_expr(n) + "; (lb_cspan){ (void*)(" + vn +
               ".data), " + vn + ".length }; })";
    }
    if (is_array(t)) {
        int id = tmp();
        string vn = "_lb_ca" + std::to_string(id);
        return "({ " + c_type(t) + " " + vn + " = " + emit_expr(n) + "; (lb_cspan){ " + vn +
               ".d, " + std::to_string(t->length) + "ULL }; })";
    }
    return emit_expr(n);
}

} // namespace lucb
