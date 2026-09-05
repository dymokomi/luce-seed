//==============================================================================================
//
//   interp/memory - Allocation in the oracle
//
//   DESCRIPTION:
//       `new`, `alloc`, and `free` over the heap and `FixedBuffer` allocators, and a user
//       `Allocator` reached through its interface view. Storage is a deque of typed values so
//       pointers into it stay valid; exhaustion is the recoverable error of base.md §11.7.
//
//==============================================================================================

#include "interp/interp_impl.h"

#include "support/literal.h"
#include <cstring>

namespace lucb {

auto Interp::heap_alloc_value() -> Value {
    Value a;
    a.kind = TypeKind::Allocator;
    a.ptr = nullptr;
    a.u = 0;
    return a;
}

auto Interp::fail_exhausted(Type* fail_ty) -> Value {
    Value v;
    v.kind = TypeKind::Fallible;
    v.type = fail_ty;
    v.failed = true;
    v.err_code = 1;
    v.err_msg = "memory.exhausted";
    return v;
}

auto Interp::as_u8_span(const Value& v) -> Value {
    if (v.kind == TypeKind::Span || v.kind == TypeKind::Array) {
        return v;
    }
    string d = decode_string(v.str);
    vector<Value> elems;
    elems.resize(d.size());
    for (size_t i = 0; i < d.size(); i++) {
        elems[i] = v_int(nullptr, static_cast<unsigned char>(d[i]));
        elems[i].kind = TypeKind::U8;
    }
    storage.push_back(std::move(elems));
    Value sp;
    sp.kind = TypeKind::Span;
    sp.ptr = storage.back().empty() ? nullptr : storage.back().data();
    sp.length = storage.back().size();
    return sp;
}

auto Interp::ok_payload(Value payload, Type* fail_ty) -> Value {
    payload.failed = false;
    payload.kind = TypeKind::Fallible;
    payload.type = fail_ty;
    return payload;
}

auto Interp::as_alloc(Node* n) -> Value {
    if (n == nullptr) {
        return current_alloc;
    }
    Type* t = n->ty;
    if (t != nullptr && t->kind == TypeKind::Struct && t->name == "FixedBuffer") {
        Value* p = lvalue(n);
        Value a;
        a.kind = TypeKind::Allocator;
        a.ptr = p;
        a.u = 1;
        return a;
    }
    if (t != nullptr && t->kind == TypeKind::Struct && t->decl != nullptr) {
        Value* p = lvalue(n);
        Value a;
        a.kind = TypeKind::Interface;
        a.ptr = p;
        a.type = t;
        a.u = 2;
        return a;
    }
    Value v = eval(n);
    if ((v.kind == TypeKind::Pointer || v.kind == TypeKind::Interface) && v.ptr != nullptr &&
        v.ptr->type != nullptr && v.ptr->type->kind == TypeKind::Struct) {
        // `&arena` reached an Allocator position: form the view from the pointee.
        Value a;
        a.ptr = v.ptr;
        if (v.ptr->type->name == "FixedBuffer") {
            a.kind = TypeKind::Allocator;
            a.u = 1;
        } else {
            a.kind = TypeKind::Interface;
            a.type = v.ptr->type;
            a.u = 2;
        }
        return a;
    }
    return v;
}

auto Interp::bump_fixed(Value* fb, size_t size, size_t align) -> bool {
    if (fb == nullptr || fb->fields.size() < 3) {
        return false;
    }
    size_t used = static_cast<size_t>(fb->fields[2].u);
    size_t cap = static_cast<size_t>(fb->fields[1].u);
    if (align < 1) {
        align = 1;
    }
    size_t start = used;
    size_t rem = start % align;
    if (rem != 0) {
        start += align - rem;
    }
    if (start + size < start || start + size > cap) {
        return false;
    }
    fb->fields[2].u = start + size;
    return true;
}

auto Interp::take_bytes(const Value& a, size_t size, size_t align) -> bool {
    if (size == 0) {
        return true;
    }
    if (a.kind == TypeKind::Allocator && a.ptr != nullptr && a.u == 1) {
        return bump_fixed(a.ptr, size, align);
    }
    if ((a.kind == TypeKind::Interface || a.u == 2) && a.ptr != nullptr) {
        Node* st = a.ptr->type != nullptr ? a.ptr->type->decl : nullptr;
        if (st == nullptr && a.type != nullptr) {
            st = a.type->decl;
        }
        Value sz;
        sz.kind = TypeKind::Usize;
        sz.u = size;
        Value al;
        al.kind = TypeKind::Usize;
        al.u = align < 1 ? 1 : align;
        vector<Value> args;
        args.push_back(sz);
        args.push_back(al);
        Value r = invoke_method(a.ptr, st, "allocate", args);
        if (trapped) {
            return false;
        }
        if (r.kind == TypeKind::Optional || is_opt(r.type)) {
            return r.present;
        }
        return !r.failed;
    }
    return true;
}

auto Interp::eval_new(Node* n) -> Value {
    Value a = as_alloc(n->right);
    if (trapped) {
        return v_unit();
    }
    Type* payload = is_fail(n->ty) ? n->ty->elem : n->ty;
    if (is_span(payload)) {
        Node* count_n = n->type != nullptr ? n->type->right : nullptr;
        Value cv = eval(count_n);
        if (trapped) {
            return v_unit();
        }
        size_t count = static_cast<size_t>(as_u(cv, count_n != nullptr ? count_n->ty : nullptr));
        Type* elem = payload->elem;
        int esz = type_size(elem);
        int eal = type_align(elem);
        if (count != 0 && esz > 0 && count > static_cast<size_t>(-1) / static_cast<size_t>(esz)) {
            return fail_exhausted(n->ty);
        }
        size_t bytes = static_cast<size_t>(esz) * count;
        if (!take_bytes(a, bytes, static_cast<size_t>(eal < 1 ? 1 : eal))) {
            return fail_exhausted(n->ty);
        }
        vector<Value> elems;
        elems.resize(count);
        for (size_t i = 0; i < count; i++) {
            elems[i] = zero_of(elem);
        }
        Value sp = make_array(payload, std::move(elems));
        sp.kind = TypeKind::Span;
        sp.type = payload;
        if (a.u == 1) {
            bump_fb = a.ptr;
            bump_ptr = sp.ptr;
            bump_len = sp.length;
        }
        return ok_payload(sp, n->ty);
    }
    Type* elem = is_ptr(payload) ? payload->elem : payload;
    int esz = type_size(elem);
    int eal = type_align(elem);
    if (!take_bytes(a, static_cast<size_t>(esz < 0 ? 0 : esz),
                    static_cast<size_t>(eal < 1 ? 1 : eal))) {
        return fail_exhausted(n->ty);
    }
    Value init;
    if (n->body != nullptr && n->body->kind == NodeKind::CaseValue) {
        init = eval(n->body);
    } else if (n->body != nullptr && n->resolved != nullptr &&
               n->resolved->kind == NodeKind::Struct) {
        init = eval_ctor(n, n->resolved);
    } else {
        init = zero_of(elem);
    }
    if (trapped) {
        return v_unit();
    }
    if (elem != nullptr && elem->kind == TypeKind::Array) {
        Value arr = init.kind == TypeKind::Array ? init : zero_of(elem);
        storage.push_back(vector<Value>{arr});
        Value p;
        p.kind = TypeKind::Pointer;
        p.type = payload;
        p.ptr = storage.back().data();
        return ok_payload(p, n->ty);
    }
    storage.push_back(vector<Value>{init});
    Value p;
    p.kind = TypeKind::Pointer;
    p.type = payload;
    p.ptr = storage.back().data();
    return ok_payload(p, n->ty);
}

auto Interp::eval_alloc(Node* n) -> Value {
    Value a = as_alloc(n->right);
    if (trapped) {
        return v_unit();
    }
    Type* payload = is_fail(n->ty) ? n->ty->elem : n->ty;
    size_t count = 0;
    Type* elem = payload != nullptr ? payload->elem : nullptr;
    size_t align = static_cast<size_t>(type_align(elem) < 1 ? 1 : type_align(elem));
    if (n->type == nullptr) {
        Value sv = eval(n->body != nullptr ? n->body->left : nullptr);
        Value av =
            eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr);
        if (trapped) {
            return v_unit();
        }
        count = static_cast<size_t>(
            as_u(sv, n->body != nullptr && n->body->left != nullptr ? n->body->left->ty : nullptr));
        align = static_cast<size_t>(as_u(av, nullptr));
        if (align < 1) {
            align = 1;
        }
        elem = payload != nullptr ? payload->elem : nullptr;
    } else {
        Node* count_n = n->type->right;
        Value cv = eval(count_n);
        if (trapped) {
            return v_unit();
        }
        count = static_cast<size_t>(as_u(cv, count_n != nullptr ? count_n->ty : nullptr));
    }
    int esz = type_size(elem);
    if (n->type == nullptr) {
        esz = 1;
    }
    if (count != 0 && esz > 0 && count > static_cast<size_t>(-1) / static_cast<size_t>(esz)) {
        return fail_exhausted(n->ty);
    }
    size_t bytes = n->type == nullptr ? count : static_cast<size_t>(esz) * count;
    size_t nlen = n->type == nullptr ? count : count;
    if (!take_bytes(a, bytes, align)) {
        return fail_exhausted(n->ty);
    }
    vector<Value> elems;
    elems.resize(nlen);
    for (size_t i = 0; i < nlen; i++) {
        elems[i] = zero_of(elem);
    }
    Value sp = make_array(payload, std::move(elems));
    sp.kind = TypeKind::Span;
    sp.type = payload;
    if (a.u == 1) {
        bump_fb = a.ptr;
        bump_ptr = sp.ptr;
        bump_len = sp.length;
    }
    return ok_payload(sp, n->ty);
}

} // namespace lucb
