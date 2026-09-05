//==============================================================================================
//
//   emit/memory - Allocation: new, alloc, free, with, and the current allocator
//
//   DESCRIPTION:
//       `new` and `alloc` become `lb_allocate_call` on the chosen allocator view with the
//       element size and alignment the checker fixed; `free` becomes `lb_release_call`;
//       `with` swaps the thread-local current allocator and restores it on every exit through
//       the scope machinery in stmt.cpp. Exhaustion is the recoverable `memory.exhausted`
//       error (base.md §12).
//
//==============================================================================================

#include "emit/emitter.h"

#include "support/literal.h"

namespace lucb {

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
    if (t != nullptr && t->kind == TypeKind::Struct && t->decl != nullptr && n->ty != nullptr &&
        n->ty->kind != TypeKind::Interface) {
        string vt = "lb_vt_" + string(t->decl->text) + "_Allocator";
        return "((lb_iface){ (void*)(" + emit_addr(n) + "), &" + vt + " })";
    }
    // `in &arena`: the pointer is the view's payload.
    if (is_ptr(t) && t->elem != nullptr && t->elem->kind == TypeKind::Struct) {
        if (t->elem->name == "FixedBuffer") {
            return "lb_fixed_alloc(" + emit_expr(n) + ")";
        }
        if (t->elem->decl != nullptr) {
            string vt = "lb_vt_" + string(t->elem->decl->text) + "_Allocator";
            return "((lb_iface){ (void*)(" + emit_expr(n) + "), &" + vt + " })";
        }
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
    s += "lb_iface " + an + " = " + emit_allocator(n->right) + "; ";
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
        s += "lb_span_opt _lb_ao" + std::to_string(id) + " = lb_alloc_call(" + an + ", _lb_bytes" +
             std::to_string(id) + ", _Alignof(" + et + ")); ";
        s += "lb_span " + bn + " = _lb_ao" + std::to_string(id) + ".value; ";
        s += "if (_lb_bytes" + std::to_string(id) + " != 0 && !_lb_ao" + std::to_string(id) +
             ".present) { " + rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
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
    s += "lb_span_opt _lb_ao" + std::to_string(id) + " = lb_alloc_call(" + an + ", sizeof(" + et +
         "), _Alignof(" + et + ")); ";
    s += "lb_span " + bn + " = _lb_ao" + std::to_string(id) + ".value; ";
    s += rty + " " + rn + "; ";
    s += "if (sizeof(" + et + ") != 0 && !_lb_ao" + std::to_string(id) + ".present) { " + rn +
         " = " + emit_exhausted_lit(payload) + "; } else { ";
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
    s += "lb_iface " + an + " = " + emit_allocator(n->right) + "; ";
    s += rty + " " + rn + "; ";
    if (n->type == nullptr) {
        string sz = emit_expr(n->body != nullptr ? n->body->left : nullptr);
        string al = emit_expr(n->body != nullptr && n->body->next != nullptr ? n->body->next->left
                                                                             : nullptr);
        s += "size_t _lb_sz" + std::to_string(id) + " = (size_t)(" + sz + "); ";
        s += "size_t _lb_al" + std::to_string(id) + " = (size_t)(" + al + "); ";
        s += "lb_span_opt _lb_ao" + std::to_string(id) + " = lb_alloc_call(" + an + ", _lb_sz" +
             std::to_string(id) + ", _lb_al" + std::to_string(id) + "); ";
        s += "lb_span " + bn + " = _lb_ao" + std::to_string(id) + ".value; ";
        s += "if (_lb_sz" + std::to_string(id) + " != 0 && !_lb_ao" + std::to_string(id) +
             ".present) { " + rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
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
    s += "lb_span_opt _lb_ao" + std::to_string(id) + " = lb_alloc_call(" + an + ", _lb_bytes" +
         std::to_string(id) + ", _Alignof(" + et + ")); ";
    s += "lb_span " + bn + " = _lb_ao" + std::to_string(id) + ".value; ";
    s += "if (_lb_bytes" + std::to_string(id) + " != 0 && !_lb_ao" + std::to_string(id) +
         ".present) { " + rn + " = " + emit_exhausted_lit(payload) + "; } else { ";
    s += rn + ".value.data = " + bn + ".data; ";
    s += rn + ".value.length = " + cn + "; ";
    s += rn + ".failed = false; } } ";
    s += rn + "; })";
    return s;
}

} // namespace lucb
