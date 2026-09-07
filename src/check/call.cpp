//==============================================================================================
//
//   check/call - Calls, methods, members, lambdas, and the extern boundary
//
//   DESCRIPTION:
//       Argument binding for every call form: positional and named arguments, defaults
//       including the call-site `luce.location`, generic inference through `generic.cpp`,
//       method receivers and `mutating`, static constructors, function values, capture-free
//       lambdas, and extern calls with the C-representable and variadic rules of base.md §9,
//       §13, §17. Member access resolves fields, methods, enum cases, and the standard
//       modules.
//
//==============================================================================================

#include "check/checker.h"

#include "support/literal.h"

namespace lucb {

auto Checker::count_args(Node* args) -> int {
    int n = 0;
    for (Node* a = args; a != nullptr; a = a->next) {
        n++;
    }
    return n;
}

auto Checker::check_call(Node* n, Type* expected) -> Type* {
    (void)expected;
    Node* callee = n->left;
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
        return check_print(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "format") {
        return check_format(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "hash") {
        return check_hash(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "hex") {
        return check_hex(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "bin") {
        return check_bin(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "pad") {
        return check_pad(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "location") {
        fail_n(n, "lucb.check.name", "write `luce.location`");
        return t_error();
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
        return check_trap(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "error") {
        return check_error(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "sizeof") {
        return check_sizeof(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "alignof") {
        return check_alignof(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "offsetof") {
        return check_offsetof(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "assert") {
        return check_assert(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "discard") {
        return check_discard(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "CAllocator") {
        if (count_args(n->body) != 0) {
            fail_n(n, "lucb.check.call", "`CAllocator()` takes no arguments");
        }
        n->resolved = nullptr;
        return ty_alloc;
    }
    if (callee != nullptr && callee->kind == NodeKind::Member && callee->left != nullptr &&
        callee->left->kind == NodeKind::Name && callee->left->text == "ErrorCode" &&
        callee->text == "package") {
        return check_error_code_package(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Member && callee->left != nullptr &&
        callee->left->kind == NodeKind::Name && callee->left->text == "c") {
        Type* ct = imported_c_type(callee, keep("c." + string(callee->text)));
        if (ct != nullptr) { // `c.long(x)`: the checked conversion (§5.2, §7.5)
            callee->ty = ct;
            callee->left->ty = ct;
            return check_checked_conv(n, ct);
        }
    }
    if (callee != nullptr && callee->kind == NodeKind::Member) {
        return check_method_call(n);
    }
    if (callee != nullptr && callee->kind == NodeKind::Name) {
        Type* conv = named_scalar(callee->text);
        if (conv != nullptr && lookup(callee->text) == nullptr) {
            return check_checked_conv(n, conv);
        }
        Binding* b = lookup(callee->text);
        if (b != nullptr) {
            mark_referenced(b->decl);
        }
        if (b == nullptr) {
            fail_n(n, "lucb.check.name", "unknown name `" + string(callee->text) + "`");
            return t_error();
        }
        mark_import(b);
        callee->resolved = b->decl;
        n->resolved = b->decl;
        if (b->decl != nullptr && b->decl->kind == NodeKind::Struct) {
            if (is_generic_decl(b->decl) || n->type != nullptr) {
                return check_generic_ctor(n, b->decl);
            }
            return check_ctor(n, b->decl);
        }
        if (b->decl != nullptr && b->decl->kind == NodeKind::Enum && is_int_enum(b->type)) {
            return check_checked_conv(n, b->type);
        }
        if (b->decl != nullptr && b->decl->kind == NodeKind::ExternFunc) {
            return check_extern_call(n, b->decl);
        }
        if (b->decl != nullptr && b->decl->kind == NodeKind::Func) {
            if (is_generic_decl(b->decl) || n->type != nullptr) {
                return check_generic_call(n, b->decl, nullptr);
            }
            return check_func_call(n, b->decl, nullptr);
        }
        if (b->type != nullptr && is_func(b->type)) {
            callee->ty = b->type;
            return check_fnptr_call(n, b->type);
        }
        fail_n(n, "lucb.check.type", "`" + string(callee->text) + "` is not callable");
        return t_error();
    }
    Type* ct = check_expr(callee, nullptr);
    if (is_func(ct)) {
        return check_fnptr_call(n, ct);
    }
    fail_n(n, "lucb.check.unsupported", "this call is not in the scalar core yet");
    return t_error();
}

auto Checker::check_ctor(Node* n, Node* st) -> Type* {
    Type* ty = st->ty;
    Node* init = struct_member(st, "init", NodeKind::Func);
    if (init != nullptr) {
        n->resolved = init;
        check_func_call(n, init, nullptr);
        if ((init->flags & FlagFallible) != 0) {
            return intern_fail(ty);
        }
        return ty;
    }
    for (Node* a = n->body; a != nullptr; a = a->next) {
        if (a->left == nullptr) {
            continue;
        }
        Node* field = nullptr;
        if (!a->text.empty()) {
            field = struct_member(st, a->text, NodeKind::Field);
            if (field == nullptr) {
                fail_n(a, "lucb.check.name", "no field `" + string(a->text) + "`");
                continue;
            }
            if (field != nullptr && imported_owner(st) && (field->flags & FlagPub) == 0) {
                fail_n(a, "lucb.check.name", "field `" + string(a->text) + "` is not public");
                return t_error();
            }
        } else {
            fail_n(a, "lucb.check.unsupported",
                   "positional struct construction is not in the scalar core; use names");
            continue;
        }
        a->resolved = field;
        Type* at = check_expr(a->left, field->ty);
        if (!type_eq(at, field->ty) && !can_widen(at, field->ty)) {
            fail_n(a, "lucb.check.type",
                   "field `" + string(field->text) + "` has type " + type_name(field->ty));
        }
        for (Node* o = n->body; o != a; o = o->next) {
            if (o->text == a->text && !a->text.empty()) {
                fail_n(a, "lucb.check.shadow", "duplicate field");
                break;
            }
        }
    }
    if (st != nullptr) {
        for (Node* f = st->body; f != nullptr; f = f->next) {
            if (f->kind != NodeKind::Field) {
                continue;
            }
            bool provided = false;
            for (Node* a = n->body; a != nullptr; a = a->next) {
                if (a->text == f->text) {
                    provided = true;
                    break;
                }
            }
            if (provided || f->left != nullptr || is_zeroable(f->ty)) {
                continue;
            }
            fail_n(n, "lucb.check.type", "missing field `" + string(f->text) + "`");
        }
    }
    return ty;
}

auto Checker::check_func_call(Node* n, Node* fn, Node* recv) -> Type* {
    n->resolved = fn;
    Node* params = fn->right;
    fill_call_args(n, params);
    Node* args = n->body;
    // Skip implicit self: methods don't list self.
    int nparams = count_args(params);
    int nargs = count_args(args);
    if (recv != nullptr) {
        // method: args must match params
    }
    (void)recv;
    if (nparams != nargs) {
        fail_n(n, "lucb.check.call", "wrong number of arguments");
    }
    Node* p = params;
    Node* a = args;
    while (p != nullptr && a != nullptr) {
        if (!a->text.empty() && a->text != p->text) {
            fail_n(a, "lucb.check.call",
                   "argument name does not match parameter `" + string(p->text) + "`");
        }
        Type* hint = p->ty;
        if (is_u8_cspan(p->ty)) {
            hint = nullptr;
        }
        Type* at = check_expr(a->left, hint);
        bool text_to_bytes = is_u8_cspan(p->ty) && at != nullptr &&
                             (at->kind == TypeKind::Fmt || at->kind == TypeKind::Str);
        if (!text_to_bytes) {
            if (is_u8_cspan(p->ty) && is_array(at) && can_ptr_convert(at, p->ty, a->left)) {
                at = coerce(a->left, at, p->ty);
                if (a->left != nullptr) {
                    a->left->ty = at;
                }
            } else if (!type_eq(at, p->ty) && !can_widen(at, p->ty) &&
                       !can_ptr_convert(at, p->ty, a->left)) {
                fail_n(a, "lucb.check.type",
                       "parameter `" + string(p->text) + "` has type " + type_name(p->ty));
            }
        }
        a->resolved = p;
        p = p->next;
        a = a->next;
    }
    Type* result = fn->ty;
    if (result == nullptr) {
        result = t_unit();
    }
    if ((fn->flags & FlagFallible) != 0) {
        return intern_fail(result);
    }
    return result;
}

auto Checker::fill_call_args(Node* n, Node* params) -> void {
    if (n == nullptr) {
        return;
    }
    vector<Node*> plist;
    for (Node* p = params; p != nullptr; p = p->next) {
        if (p->flags & FlagVariadic) {
            continue;
        }
        plist.push_back(p);
    }
    int nparams = static_cast<int>(plist.size());
    vector<Node*> chosen(static_cast<size_t>(nparams), nullptr);
    int pos = 0;
    bool seen_named = false;
    for (Node* a = n->body; a != nullptr; a = a->next) {
        if (!a->text.empty()) {
            seen_named = true;
            int idx = -1;
            for (int i = 0; i < nparams; i++) {
                if (plist[static_cast<size_t>(i)]->text == a->text) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                fail_n(a, "lucb.check.call", "unknown argument `" + string(a->text) + "`");
                continue;
            }
            if (chosen[static_cast<size_t>(idx)] != nullptr) {
                fail_n(a, "lucb.check.call", "duplicate argument `" + string(a->text) + "`");
                continue;
            }
            chosen[static_cast<size_t>(idx)] = a;
        } else {
            if (seen_named) {
                fail_n(a, "lucb.check.call", "positional arguments must come first");
                continue;
            }
            if (pos >= nparams) {
                fail_n(n, "lucb.check.call", "wrong number of arguments");
                break;
            }
            chosen[static_cast<size_t>(pos)] = a;
            pos++;
        }
    }
    Node* filled = nullptr;
    Node** tail = &filled;
    for (int i = 0; i < nparams; i++) {
        Node* a = chosen[static_cast<size_t>(i)];
        Node* p = plist[static_cast<size_t>(i)];
        if (a == nullptr) {
            if (p->left == nullptr) {
                fail_n(n, "lucb.check.call", "missing argument `" + string(p->text) + "`");
                continue;
            }
            a = arena->make<Node>();
            a->kind = NodeKind::Param;
            a->text = p->text;
            a->span = n->span;
            a->left = clone_node(p->left);
            if (a->left != nullptr && a->left->kind == NodeKind::Member) {
                a->left->span = n->span;
            }
        }
        a->next = nullptr;
        *tail = a;
        tail = &a->next;
    }
    n->body = filled;
}

auto Checker::check_fnptr_call(Node* n, Type* ft) -> Type* {
    if (ft != nullptr && ft->is_nullable) {
        fail_n(n, "lucb.check.type", "unwrap a nullable function with `if let` or `else`");
        return t_error();
    }
    int nparams = ft != nullptr ? ft->ntargs : 0;
    int nargs = count_args(n->body);
    if (nparams != nargs) {
        fail_n(n, "lucb.check.call", "wrong number of arguments");
    }
    Node* a = n->body;
    int i = 0;
    while (a != nullptr && i < nparams) {
        Type* want = ft->args[i];
        Type* at = check_expr(a->left, want);
        if (!type_eq(at, want) && !can_widen(at, want) && !can_ptr_convert(at, want, a->left) &&
            !(is_func(at) && is_func(want) && func_converts(at, want))) {
            fail_n(a, "lucb.check.type",
                   "argument has type " + type_name(at) + ", expected " + type_name(want));
        }
        a = a->next;
        i++;
    }
    Type* result = ft != nullptr && ft->elem != nullptr ? ft->elem : t_unit();
    return result;
}

auto Checker::check_lambda(Node* n, Type* expected) -> Type* {
    Type* ft = expected;
    if (is_opt(ft) && ft->elem != nullptr && is_func(ft->elem)) {
        ft = ft->elem;
    }
    if (ft != nullptr && is_func(ft) && ft->is_nullable) {
        ft = intern_func(ft->args, ft->ntargs, ft->elem, false);
    }
    if (ft != nullptr && !is_func(ft)) {
        fail_n(n, "lucb.check.type", "a lambda needs a function type");
        return t_error();
    }
    int nparams = 0;
    for (Node* p = n->right; p != nullptr; p = p->next) {
        nparams++;
    }
    if (is_func(ft) && ft->ntargs != nparams) {
        fail_n(n, "lucb.check.call", "wrong number of arguments");
    }
    int saved_lambda = lambda_depth;
    push_scope();
    lambda_depth = depth;
    int i = 0;
    for (Node* p = n->right; p != nullptr; p = p->next) {
        Type* pt = nullptr;
        if (p->type != nullptr) {
            pt = resolve_type(p->type);
        } else if (is_func(ft) && i < ft->ntargs) {
            pt = ft->args[i];
        } else {
            fail_n(p, "lucb.check.type", "a lambda parameter needs a type");
            pt = t_error();
        }
        if (is_func(ft) && i < ft->ntargs && pt != nullptr && !type_eq(pt, ft->args[i]) &&
            !can_widen(pt, ft->args[i])) {
            fail_n(p, "lucb.check.type", "parameter has type " + type_name(pt));
        }
        p->ty = pt;
        bind(p->text, pt, false, p);
        i++;
    }
    Type* want_ret = is_func(ft) ? ft->elem : nullptr;
    Type* body = n->body != nullptr ? check_expr(n->body, want_ret) : t_unit();
    if (body != nullptr && body->kind == TypeKind::UntypedInt && want_ret == nullptr) {
        body = coerce(n->body, body, t_i64());
        if (n->body != nullptr) {
            n->body->ty = body;
        }
    }
    pop_scope();
    lambda_depth = saved_lambda;
    if (is_func(ft)) {
        n->ty = intern_func(ft->args, ft->ntargs, ft->elem, false);
    } else {
        vector<Type*> ps;
        for (Node* p = n->right; p != nullptr; p = p->next) {
            ps.push_back(p->ty != nullptr ? p->ty : t_error());
        }
        n->ty =
            intern_func(ps.empty() ? nullptr : ps.data(), static_cast<int>(ps.size()), body, false);
    }
    if (!checking_generic_template) {
        Node* syn = arena->make<Node>();
        syn->kind = NodeKind::Func;
        syn->text = keep("_lam" + std::to_string(nlambda++));
        syn->span = n->span;
        syn->right = n->right;
        syn->ty = n->ty != nullptr && n->ty->elem != nullptr ? n->ty->elem : t_unit();
        if (n->ty != nullptr && is_fail(n->ty->elem)) {
            syn->flags |= FlagFallible;
            syn->ty = n->ty->elem->elem != nullptr ? n->ty->elem->elem : t_unit();
        }
        Node* block = arena->make<Node>();
        block->kind = NodeKind::Block;
        block->span = n->span;
        Node* ret = arena->make<Node>();
        ret->kind = NodeKind::Return;
        ret->span = n->span;
        if (syn->ty != nullptr && syn->ty->kind != TypeKind::Unit) {
            ret->left = n->body;
            block->body = ret;
        } else {
            Node* es = arena->make<Node>();
            es->kind = NodeKind::ExprStmt;
            es->left = n->body;
            es->span = n->span;
            es->next = ret;
            block->body = es;
        }
        syn->body = block;
        pending_clones.push_back(syn);
        n->resolved = syn;
    }
    return n->ty;
}

auto Checker::is_cstr(Type* t) -> bool {
    return t != nullptr && t->kind == TypeKind::CStr;
}

auto Checker::extern_arg_ok(Type* at, Type* pt, Node* arg) -> bool {
    if (type_eq(at, pt) || can_widen(at, pt) || can_ptr_convert(at, pt, arg)) {
        return true;
    }
    if (is_cstr(pt) && at != nullptr && at->kind == TypeKind::Str && arg != nullptr &&
        arg->kind == NodeKind::Literal && arg->op == TokenKind::StringLit) {
        return true;
    }
    return false;
}

auto Checker::check_variadic_arg(Node* a) -> Type* {
    Type* t = check_expr(a->left, nullptr);
    if (t != nullptr && t->kind == TypeKind::UntypedInt) {
        t = coerce(a->left, t, ty_i32);
        a->left->ty = t;
        return t;
    }
    if (is_float(t) && t->kind != TypeKind::F64) {
        a->left->ty = ty_f64;
        return ty_f64;
    }
    if (t != nullptr &&
        (t->kind == TypeKind::Bool || t->kind == TypeKind::Char || t->kind == TypeKind::I8 ||
         t->kind == TypeKind::U8 || t->kind == TypeKind::I16 || t->kind == TypeKind::U16)) {
        return ty_i32;
    }
    if (t != nullptr && t->kind == TypeKind::Str) {
        if (a->left != nullptr && a->left->kind == NodeKind::Literal &&
            a->left->op == TokenKind::StringLit) {
            a->left->ty = ty_cstr;
            return ty_cstr;
        }
        fail_n(a, "lucb.check.type", "a variadic argument cannot be `str`; use `c.str`");
        return t_error();
    }
    if (is_span(t) || (t != nullptr && (t->kind == TypeKind::Struct || is_opt(t) ||
                                        t->kind == TypeKind::Interface))) {
        fail_n(a, "lucb.check.type", "this type cannot be a variadic argument");
        return t_error();
    }
    return t;
}

auto Checker::check_extern_call(Node* n, Node* fn) -> Type* {
    n->resolved = fn;
    // an `out` parameter takes no argument: the callee writes it, and the call answers it
    // beside the declared result (§17.1)
    int nparams = 0;
    int nfixed = 0;
    bool variadic = false;
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        if (p->flags & FlagOut) {
            continue;
        }
        nparams++;
        if (p->flags & FlagVariadic) {
            variadic = true;
        } else {
            nfixed++;
        }
    }
    int nargs = count_args(n->body);
    if (variadic) {
        if (nargs < nfixed) {
            fail_n(n, "lucb.check.call", "wrong number of arguments");
        }
    } else if (nparams != nargs) {
        fail_n(n, "lucb.check.call", "wrong number of arguments");
    }
    Node* p = fn->right;
    Node* a = n->body;
    int i = 0;
    while (p != nullptr && a != nullptr && i < nfixed) {
        if (p->flags & FlagOut) {
            p = p->next;
            continue;
        }
        Type* at = check_expr(a->left, p->ty);
        if (!extern_arg_ok(at, p->ty, a->left)) {
            fail_n(a, "lucb.check.type",
                   "parameter `" + string(p->text) + "` has type " + type_name(p->ty));
        }
        a->resolved = p;
        p = p->next;
        a = a->next;
        i++;
    }
    while (a != nullptr) {
        check_variadic_arg(a);
        a = a->next;
    }
    return extern_result(fn);
}

// What a call of `fn` answers: its declared result, and after it every `out` parameter, as
// a tuple when there is more than one value (§17.1).
auto Checker::extern_result(Node* fn) -> Type* {
    Type* declared = fn->ty != nullptr ? fn->ty : t_unit();
    vector<Type*> parts;
    if (declared->kind != TypeKind::Unit) {
        parts.push_back(declared);
    }
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        if (p->flags & FlagOut) {
            parts.push_back(p->ty);
        }
    }
    if (parts.empty()) {
        return declared;
    }
    if (parts.size() == 1) {
        return parts[0];
    }
    return intern_tup(parts.data(), static_cast<int>(parts.size()));
}

auto Checker::check_memory_copy(Node* n, string_view name) -> Type* {
    n->resolved = n->left != nullptr ? n->left->resolved : n->resolved;
    if (count_args(n->body) != 3) {
        fail_n(n, "lucb.check.call", "`memory." + string(name) + "` takes to, from, and count");
        return t_unit();
    }
    Node* to = n->body != nullptr ? n->body->left : nullptr;
    Node* from = n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
    Node* count = n->body != nullptr && n->body->next != nullptr && n->body->next->next != nullptr
                      ? n->body->next->next->left
                      : nullptr;
    Type* tt = check_expr(to, nullptr);
    Type* ft = check_expr(from, nullptr);
    Type* ct = check_expr(count, ty_usize);
    (void)ct;
    if (is_array(tt) && to != nullptr) {
        tt = intern_sp(tt->elem, false);
        to->ty = tt;
    }
    if (is_array(ft) && from != nullptr) {
        bool cnst = ft->is_const;
        ft = intern_sp(ft->elem, true);
        (void)cnst;
        from->ty = ft;
    }
    Type* telem = (is_span(tt) || is_array(tt)) && tt != nullptr ? tt->elem : nullptr;
    Type* felem = (is_span(ft) || is_array(ft)) && ft != nullptr ? ft->elem : nullptr;
    if (telem == nullptr || felem == nullptr) {
        fail_n(n, "lucb.check.type", "`memory." + string(name) + "` needs spans or arrays");
        return t_unit();
    }
    if (!type_eq(telem, felem)) {
        fail_n(n, "lucb.check.type", "`memory." + string(name) + "` copies one element type");
    }
    if (is_span(tt) && tt->is_const) {
        fail_n(n, "lucb.check.mut", "`memory." + string(name) + "` destination must be mutable");
    }
    return t_unit();
}

auto Checker::check_memory_rw(Node* n, string_view name) -> Type* {
    Node* mem = n->left;
    Node* obj = mem != nullptr ? mem->left : nullptr;
    if (obj != nullptr) {
        Binding* b = lookup(obj->text);
        if (b != nullptr) {
            obj->resolved = b->decl;
            obj->ty = b->type;
        }
    }
    if (n->type == nullptr) {
        fail_n(n, "lucb.check.type", "`memory." + string(name) + "` needs a type argument");
        return t_error();
    }
    if (n->type->next != nullptr) {
        fail_n(n, "lucb.check.type", "`memory." + string(name) + "` takes one type argument");
    }
    Type* t = resolve_type(n->type);
    n->type->ty = t;
    Type* voidp = intern_ptr(ty_void, false, false, false);
    if (name == "read") {
        if (count_args(n->body) != 1) {
            fail_n(n, "lucb.check.call", "`memory.read` takes an address");
        }
        if (n->body != nullptr) {
            Type* at = check_expr(n->body->left, voidp);
            if (!is_ptr(at) && (at == nullptr || at->kind != TypeKind::Void)) {
                fail_n(n, "lucb.check.type", "`memory.read` needs a pointer");
            }
        }
        if (mem != nullptr) {
            mem->resolved = n->resolved;
        }
        return t;
    }
    if (count_args(n->body) != 2) {
        fail_n(n, "lucb.check.call", "`memory.write` takes an address and a value");
    }
    if (n->body != nullptr) {
        Type* at = check_expr(n->body->left, voidp);
        if (!is_ptr(at) && (at == nullptr || at->kind != TypeKind::Void)) {
            fail_n(n, "lucb.check.type", "`memory.write` needs a pointer");
        }
        if (n->body->next != nullptr) {
            Type* vt = check_expr(n->body->next->left, t);
            if (!type_eq(vt, t) && !can_widen(vt, t) &&
                !can_ptr_convert(vt, t, n->body->next->left)) {
                fail_n(n->body->next, "lucb.check.type",
                       "`memory.write` value has type `" + type_name(t) + "`");
            }
        }
    }
    if (mem != nullptr) {
        mem->resolved = n->resolved;
    }
    return t_unit();
}

auto Checker::check_method_call(Node* n) -> Type* {
    Node* mem = n->left;
    Node* obj = mem->left;
    // Static: Point.origin() — obj is a type name.
    if (obj != nullptr && obj->kind == NodeKind::Name && obj->text == "c" && ty_c_mod != nullptr &&
        (mem->text == "stdin" || mem->text == "stdout" || mem->text == "stderr")) {
        Node* d = pub_member(ty_c_mod->decl, mem->text);
        mem->resolved = d;
        obj->ty = ty_c_mod;
        n->resolved = d;
        return intern_ptr(ty_void, false, false, false);
    }
    if (obj != nullptr && obj->kind == NodeKind::Name && mem->text == "bits" &&
        named_float(obj->text) != nullptr) {
        return check_float_from_bits(n, obj);
    }
    if (obj != nullptr && obj->kind == NodeKind::Name) {
        Binding* b = lookup(obj->text);
        if (b != nullptr && b->type != nullptr && b->type->kind == TypeKind::Module) {
            mark_import(b);
            Node* d = pub_member(b->type->decl, mem->text);
            if (d == nullptr) {
                fail_n(n, "lucb.check.name", "no public `" + string(mem->text) + "`");
                return t_error();
            }
            mem->resolved = d;
            obj->resolved = b->decl;
            obj->ty = b->type;
            n->resolved = d;
            if (d->kind == NodeKind::Struct) {
                if (is_generic_decl(d) || n->type != nullptr) {
                    return check_generic_ctor(n, d); // `module.List[i64](...)`
                }
                return check_ctor(n, d);
            }
            if (d->kind == NodeKind::Enum && is_int_enum(d->ty)) {
                return check_checked_conv(n, d->ty);
            }
            if (d->kind == NodeKind::Func) {
                if (b->type->name == "memory" && (mem->text == "copy" || mem->text == "move")) {
                    return check_memory_copy(n, mem->text);
                }
                if (b->type->name == "memory" && (mem->text == "read" || mem->text == "write")) {
                    return check_memory_rw(n, mem->text);
                }
                if (b->type->name == "thread" && mem->text == "spawn") {
                    n->resolved = d;
                    mem->resolved = d;
                    int nargs = count_args(n->body);
                    if (nargs < 2) {
                        fail_n(n, "lucb.check.call", "`thread.spawn` needs an entry and a context");
                        return intern_fail(d->ty);
                    }
                    Node* entry = n->body != nullptr ? n->body->left : nullptr;
                    if (entry == nullptr || entry->kind != NodeKind::Name) {
                        fail_n(n, "lucb.check.type", "`thread.spawn` needs a function name");
                    } else {
                        Binding* fb = lookup(entry->text);
                        if (fb != nullptr) {
                            mark_referenced(fb->decl);
                        }
                        if (fb == nullptr || fb->decl == nullptr ||
                            fb->decl->kind != NodeKind::Func) {
                            fail_n(n, "lucb.check.type", "`thread.spawn` needs a function name");
                        } else {
                            entry->resolved = fb->decl;
                        }
                    }
                    if (n->body != nullptr && n->body->next != nullptr) {
                        Type* want = intern_ptr(ty_void, false, false, true);
                        Type* ct = check_expr(n->body->next->left, want);
                        if (!is_ptr(ct) && (ct == nullptr || ct->kind != TypeKind::Void)) {
                            fail_n(n, "lucb.check.type", "`thread.spawn` context needs a pointer");
                        }
                    }
                    return intern_fail(d->ty);
                }
                if (is_generic_decl(d) || n->type != nullptr) {
                    return check_generic_call(n, d, nullptr);
                }
                return check_func_call(n, d, nullptr);
            }
            fail_n(n, "lucb.check.type", "`" + string(mem->text) + "` is not callable");
            return t_error();
        }
        if (b != nullptr && b->decl != nullptr && b->decl->kind == NodeKind::Enum) {
            Node* cse = enum_case(b->decl, mem->text);
            if (cse == nullptr) {
                fail_n(n, "lucb.check.name", "no case `" + string(mem->text) + "`");
                return t_error();
            }
            mem->resolved = cse;
            n->resolved = cse;
            obj->resolved = b->decl;
            obj->ty = b->type;
            if (cse->body == nullptr) {
                fail_n(n, "lucb.check.type", "this case has no payload");
                return t_error();
            }
            return check_case_payload(n, cse, b->type);
        }
        if (b != nullptr && b->decl != nullptr && b->decl->kind == NodeKind::Struct) {
            Node* method = struct_member(b->decl, mem->text, NodeKind::Func);
            if (method == nullptr) {
                fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
                return t_error();
            }
            if ((method->flags & FlagStatic) == 0) {
                fail_n(n, "lucb.check.call", "this method needs a receiver");
            }
            mem->resolved = method;
            obj->resolved = b->decl;
            obj->ty = b->type;
            if (is_generic_decl(b->decl) || n->type != nullptr) {
                fail_n(n, "lucb.check.type", "write type arguments on the constructor");
                return t_error();
            }
            return check_func_call(n, method, nullptr);
        }
    }
    // Qualified: module.Enum.case(...) and module.Type.method(...).
    if (obj != nullptr && obj->kind == NodeKind::Member && obj->left != nullptr &&
        obj->left->kind == NodeKind::Name) {
        Binding* mb = lookup(obj->left->text);
        if (mb != nullptr && mb->type != nullptr && mb->type->kind == TypeKind::Module) {
            Node* d = pub_member(mb->type->decl, obj->text);
            if (d == nullptr && (mb->type->decl->flags & FlagBuiltin) != 0) {
                // `memory.FixedBuffer.over(...)`: a standard module's type this compiler binds
                // as a builtin, spelled through its module
                Binding* bb = lookup(obj->text);
                if (bb != nullptr && bb->decl != nullptr && bb->decl->kind == NodeKind::Struct) {
                    d = bb->decl;
                }
            }
            if (d != nullptr && (d->kind == NodeKind::Enum || d->kind == NodeKind::Struct)) {
                mark_import(mb);
                obj->left->resolved = mb->decl;
                obj->left->ty = mb->type;
                obj->resolved = d;
                obj->ty = d->ty;
                if (d->kind == NodeKind::Enum) {
                    Node* cse = enum_case(d, mem->text);
                    if (cse == nullptr) {
                        fail_n(n, "lucb.check.name", "no case `" + string(mem->text) + "`");
                        return t_error();
                    }
                    mem->resolved = cse;
                    n->resolved = cse;
                    if (cse->body == nullptr) {
                        fail_n(n, "lucb.check.type", "this case has no payload");
                        return t_error();
                    }
                    return check_case_payload(n, cse, d->ty);
                }
                Node* method = struct_member(d, mem->text, NodeKind::Func);
                if (method == nullptr) {
                    fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
                    return t_error();
                }
                if ((method->flags & FlagStatic) == 0) {
                    fail_n(n, "lucb.check.call", "this method needs a receiver");
                }
                mem->resolved = method;
                n->resolved = method;
                if (is_generic_decl(d) || n->type != nullptr) {
                    return check_generic_call(n, method, nullptr);
                }
                return check_func_call(n, method, nullptr);
            }
        }
    }
    Type* ot = check_expr(obj);
    Type* recv = ot;
    if (is_ptr(ot) && ot->elem != nullptr) {
        recv = ot->elem;
    }
    if (is_atomic(ot) || is_atomic(recv)) {
        Type* at = is_atomic(ot) ? ot : recv;
        Type* elem = at->elem;
        n->resolved = nullptr;
        mem->resolved = nullptr;
        if (mem->text != "load" && mem->text != "wait" && mem->text != "wake") {
            bool mut_ok = is_mut_place(obj) || (is_ptr(ot) && !ot->is_const);
            if (!mut_ok) {
                fail_n(n, "lucb.check.mut", "a mutating method needs a `var` receiver");
            }
        }
        int nargs = count_args(n->body);
        Binding* ob = lookup("Ordering");
        Type* ord = ob != nullptr ? ob->type : ty_i32;
        if (mem->text == "load") {
            if (nargs > 1) {
                fail_n(n, "lucb.check.call", "`load` takes an optional ordering");
            }
            if (nargs == 1) {
                check_expr(n->body->left, ord);
            }
            return elem;
        }
        if (mem->text == "store") {
            if (nargs < 1 || nargs > 2) {
                fail_n(n, "lucb.check.call", "`store` takes a value");
            }
            if (n->body != nullptr) {
                check_expr(n->body->left, elem);
                if (n->body->next != nullptr) {
                    check_expr(n->body->next->left, ord);
                }
            }
            return t_unit();
        }
        if (mem->text == "add" || mem->text == "sub" || mem->text == "swap" || mem->text == "set" ||
            mem->text == "clear" || mem->text == "flip" || mem->text == "max" ||
            mem->text == "min") {
            if (nargs < 1 || nargs > 2) {
                fail_n(n, "lucb.check.call", "this atomic method takes a value");
            }
            if (n->body != nullptr) {
                check_expr(n->body->left, elem);
                if (n->body->next != nullptr) {
                    check_expr(n->body->next->left, ord);
                }
            }
            return elem;
        }
        if (mem->text == "wait") {
            if (nargs != 1) {
                fail_n(n, "lucb.check.call", "`wait` takes the expected value");
            }
            if (n->body != nullptr) {
                check_expr(n->body->left, elem);
            }
            return t_unit();
        }
        if (mem->text == "wake") {
            if (nargs != 1) {
                fail_n(n, "lucb.check.call", "`wake` takes a count");
            }
            if (n->body != nullptr) {
                check_expr(n->body->left, t_usize());
            }
            return t_unit();
        }
        if (mem->text == "cas") {
            if (nargs < 2) {
                fail_n(n, "lucb.check.call", "`cas` needs expected and desired values");
                Type* pair[2] = {ty_bool, elem};
                return intern_tup(pair, 2);
            }
            Node* a = n->body;
            check_expr(a->left, elem);
            a = a->next;
            check_expr(a->left, elem);
            a = a->next;
            if (a != nullptr && a->text != "weak") {
                check_expr(a->left, ord);
                if (a->left != nullptr &&
                    (a->left->kind == NodeKind::CaseValue || a->left->kind == NodeKind::Member) &&
                    (a->left->text == "release" || a->left->text == "acq_rel")) {
                    // success may be those; check failure next
                }
                a = a->next;
                if (a != nullptr && a->text != "weak") {
                    Type* ft = check_expr(a->left, ord);
                    (void)ft;
                    if (a->left != nullptr &&
                        (a->left->kind == NodeKind::CaseValue ||
                         a->left->kind == NodeKind::Member) &&
                        (a->left->text == "release" || a->left->text == "acq_rel")) {
                        fail_n(a, "lucb.check.type",
                               "`cas` failure ordering cannot be `release` or `acq_rel`");
                    }
                    a = a->next;
                }
            }
            if (a != nullptr) {
                if (a->text != "weak" && a->text != "") {
                    fail_n(a, "lucb.check.call", "unexpected `cas` argument");
                }
                check_expr(a->left, ty_bool);
            }
            Type* pair[2] = {ty_bool, elem};
            return intern_tup(pair, 2);
        }
        fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "` on an atomic");
        return t_error();
    }
    if (recv != nullptr && recv->kind == TypeKind::Interface && recv->decl != nullptr) {
        Node* method = struct_member(recv->decl, mem->text, NodeKind::Func);
        if (method == nullptr) {
            fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
            return t_error();
        }
        mem->resolved = method;
        n->resolved = method;
        return check_func_call(n, method, obj);
    }
    if (recv != nullptr && recv->kind == TypeKind::Param && (recv->bounds & BoundIface) != 0 &&
        recv->elem != nullptr && recv->elem->decl != nullptr) {
        Node* method = struct_member(recv->elem->decl, mem->text, NodeKind::Func);
        if (method == nullptr) {
            fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
            return t_error();
        }
        mem->resolved = method;
        n->resolved = method;
        return check_func_call(n, method, obj);
    }
    if (mem->text == "bits" && is_float(recv)) {
        return check_float_bits(n, obj);
    }
    if ((mem->text == "first" || mem->text == "last") && (is_span(recv) || is_array(recv))) {
        // `span.first()` and `span.last()` are `T?` (§5.4)
        if (count_args(n->body) != 0) {
            fail_n(n, "lucb.check.call", "`" + string(mem->text) + "` takes no argument");
        }
        n->resolved = nullptr;
        mem->resolved = nullptr;
        return intern_opt(recv->elem);
    }
    if (mem->text == "compare" && comparable_type(recv)) {
        if (count_args(n->body) != 1) {
            fail_n(n, "lucb.check.call", "`compare` takes one argument");
            return t_i64();
        }
        Type* at = check_expr(n->body->left, recv);
        if (!type_eq(at, recv) && !can_widen(at, recv)) {
            fail_n(n, "lucb.check.type", "`compare` needs the same type");
        }
        n->resolved = nullptr;
        mem->resolved = nullptr;
        return t_i64();
    }
    if (recv == nullptr ||
        (recv->kind != TypeKind::Struct && recv->kind != TypeKind::Union &&
         recv->kind != TypeKind::Enum) ||
        recv->decl == nullptr) {
        fail_n(n, "lucb.check.type", "methods are called on structs");
        return t_error();
    }
    Node* method = struct_member(recv->decl, mem->text, NodeKind::Func);
    if (method == nullptr) {
        // `holder.callback(args)`: a field of function type is called through its value
        Node* field = struct_member(recv->decl, mem->text, NodeKind::Field);
        if (field != nullptr && is_func(field->ty)) {
            Type* ft = check_expr(mem, nullptr);
            mem->resolved = field;
            n->resolved = nullptr;
            return check_fnptr_call(n, ft);
        }
        fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
        return t_error();
    }
    if (imported_owner(recv->decl) && (method->flags & FlagPub) == 0) {
        fail_n(n, "lucb.check.name", "no public `" + string(mem->text) + "`");
        return t_error();
    }
    if ((method->flags & FlagStatic) != 0) {
        bool on_type = false;
        if (obj != nullptr && obj->kind == NodeKind::Name) {
            Binding* b = lookup(obj->text);
            on_type = b != nullptr && b->decl != nullptr &&
                      (b->decl->kind == NodeKind::Struct || b->decl->kind == NodeKind::Enum);
        } else if (obj != nullptr && obj->kind == NodeKind::Member && obj->resolved != nullptr &&
                   (obj->resolved->kind == NodeKind::Struct ||
                    obj->resolved->kind == NodeKind::Enum)) {
            on_type = true;
        } else if (obj != nullptr && obj->kind == NodeKind::Call && obj->type != nullptr) {
            on_type = true;
        }
        if (on_type) {
            mem->resolved = method;
            n->resolved = method;
            if (is_generic_decl(method) || n->type != nullptr) {
                return check_generic_call(n, method, nullptr);
            }
            return check_func_call(n, method, nullptr);
        }
        fail_n(n, "lucb.check.call", "a static method is called on the type");
    }
    bool mut_ok = is_mut_place(obj) || (is_ptr(ot) && !ot->is_const) ||
                  (recv != nullptr && recv->kind == TypeKind::Interface);
    if ((method->flags & FlagMutating) != 0 && !mut_ok) {
        fail_n(n, "lucb.check.mut", "a mutating method needs a `var` receiver");
    }
    mem->resolved = method;
    if (recv->name == "Once" && mem->text == "run") {
        n->resolved = method;
        int nargs = count_args(n->body);
        if (nargs != 1) {
            fail_n(n, "lucb.check.call", "`Once.run` needs a function");
            return t_unit();
        }
        Node* entry = n->body != nullptr ? n->body->left : nullptr;
        if (entry == nullptr || entry->kind != NodeKind::Name) {
            fail_n(n, "lucb.check.type", "`Once.run` needs a function name");
        } else {
            Binding* fb = lookup(entry->text);
            if (fb != nullptr) {
                mark_referenced(fb->decl);
            }
            if (fb == nullptr || fb->decl == nullptr || fb->decl->kind != NodeKind::Func) {
                fail_n(n, "lucb.check.type", "`Once.run` needs a function name");
            } else if (fb->decl->right != nullptr) {
                fail_n(n, "lucb.check.type", "`Once.run` needs a function with no parameters");
            } else {
                entry->resolved = fb->decl;
            }
        }
        return t_unit();
    }
    return check_func_call(n, method, obj);
}

auto Checker::check_member(Node* n, bool as_call) -> Type* {
    (void)as_call;
    if (n->left != nullptr && n->left->kind == NodeKind::Name && n->left->text == "c") {
        Type* t = imported_c_type(n, keep("c." + string(n->text)));
        if (t != nullptr) {
            n->left->ty = t;
            return t;
        }
    }
    Type* ot = check_expr(n->left);
    if (is_ptr(ot) && ot->elem != nullptr) {
        ot = ot->elem;
    }
    if (n->text == "length") {
        if (is_span(ot) || is_array(ot) || (ot != nullptr && ot->kind == TypeKind::Str) ||
            is_span(n->left != nullptr ? n->left->ty : nullptr) ||
            is_array(n->left != nullptr ? n->left->ty : nullptr) ||
            (n->left != nullptr && n->left->ty != nullptr && n->left->ty->kind == TypeKind::Str)) {
            Type* raw = n->left != nullptr ? n->left->ty : ot;
            if (is_ptr(raw)) {
                raw = raw->elem;
            }
            if (is_span(raw) || is_array(raw) || (raw != nullptr && raw->kind == TypeKind::Str)) {
                return t_usize();
            }
        }
    }
    if (n->text == "data") {
        Type* raw = n->left != nullptr ? n->left->ty : ot;
        if (is_ptr(raw)) {
            raw = raw->elem;
        }
        if (is_span(raw)) {
            return intern_ptr(raw->elem, raw->is_const, false, false);
        }
        if (raw != nullptr && raw->kind == TypeKind::Str) {
            return intern_ptr(ty_u8, true, false, false);
        }
    }
    if (n->text == "bytes") {
        Type* raw = n->left != nullptr ? n->left->ty : ot;
        if (is_ptr(raw)) {
            raw = raw->elem;
        }
        if (raw != nullptr && raw->kind == TypeKind::Str) {
            if (is_local(n->left)) {
                mark_local(n);
            }
            return intern_sp(ty_u8, true);
        }
    }
    if (n->left != nullptr && n->left->kind == NodeKind::Name) {
        Binding* b = lookup(n->left->text);
        if (b != nullptr && b->type != nullptr && b->type->kind == TypeKind::Module) {
            mark_import(b);
            Node* d = pub_member(b->type->decl, n->text);
            if (d == nullptr) {
                fail_n(n, "lucb.check.name", "no public `" + string(n->text) + "`");
                return t_error();
            }
            n->resolved = d;
            n->left->resolved = b->decl;
            n->left->ty = b->type;
            if (d->kind == NodeKind::Func || d->kind == NodeKind::ExternFunc) {
                return func_type_of(d);
            }
            return decl_type(d);
        }
        if (b != nullptr && b->decl != nullptr && b->decl->kind == NodeKind::Enum) {
            Node* cse = enum_case(b->decl, n->text);
            if (cse == nullptr) {
                fail_n(n, "lucb.check.name", "no case `" + string(n->text) + "`");
                return t_error();
            }
            if (cse->body != nullptr) {
                fail_n(n, "lucb.check.type", "this case needs a payload");
                return t_error();
            }
            n->resolved = cse;
            n->left->resolved = b->decl;
            n->left->ty = b->type;
            return b->type;
        }
        if (b != nullptr && b->decl != nullptr &&
            (b->decl->kind == NodeKind::Struct || b->decl->kind == NodeKind::Enum ||
             b->decl->kind == NodeKind::Union)) {
            Node* method = struct_member(b->decl, n->text, NodeKind::Func);
            if (method != nullptr) {
                n->resolved = method;
                n->left->resolved = b->decl;
                n->left->ty = b->type;
                Node* owner = (method->flags & FlagStatic) != 0 ? nullptr : b->decl;
                return func_type_of(method, owner);
            }
        }
    }
    if (ot != nullptr && is_union(ot) && ot->decl != nullptr) {
        Node* field = struct_member(ot->decl, n->text, NodeKind::Field);
        if (field == nullptr) {
            fail_n(n, "lucb.check.name", "no member `" + string(n->text) + "`");
            return t_error();
        }
        n->resolved = field;
        return field->ty;
    }
    if (ot != nullptr && ot->kind == TypeKind::ErrorVal) {
        if (n->text == "code") {
            return ty_errcode != nullptr ? ty_errcode : ty_i32;
        }
        if (n->text == "message") {
            return t_str();
        }
        fail_n(n, "lucb.check.name", "no field `" + string(n->text) + "`");
        return t_error();
    }
    if (ot != nullptr && is_enum(ot) && ot->decl != nullptr) {
        Node* cse = enum_case(ot->decl, n->text);
        if (cse == nullptr) {
            fail_n(n, "lucb.check.name", "no case `" + string(n->text) + "`");
            return t_error();
        }
        if (cse->body != nullptr) {
            fail_n(n, "lucb.check.type", "this case needs a payload");
            return t_error();
        }
        n->resolved = cse;
        return ot;
    }
    if (ot == nullptr || ot->kind != TypeKind::Struct || ot->decl == nullptr) {
        fail_n(n, "lucb.check.type", "field access needs a struct");
        return t_error();
    }
    Node* field = struct_member(ot->decl, n->text, NodeKind::Field);
    if (field == nullptr) {
        fail_n(n, "lucb.check.name", "no field `" + string(n->text) + "`");
        return t_error();
    }
    if (imported_owner(ot->decl) && (field->flags & FlagPub) == 0) {
        fail_n(n, "lucb.check.name", "no public `" + string(n->text) + "`");
        return t_error();
    }
    n->resolved = field;
    if (is_local(n->left)) {
        mark_local(n);
    }
    return field->ty;
}

// A type declared in another module: its non-`pub` members are out of reach,
// whether it was imported by name or reached as `module.Type`.
auto Checker::imported_owner(Node* st) -> bool {
    if (st == nullptr) {
        return false;
    }
    Node* home = module_of(st);
    if (home != nullptr && current_module != nullptr) {
        return home != current_module;
    }
    Binding* b = lookup(st->text);
    return b != nullptr && b->import_src != nullptr;
}

} // namespace lucb

namespace lucb {

// `value.bits()`: the IEEE bits of a float as the unsigned integer of its width (§7.5).
auto Checker::check_float_bits(Node* n, Node* obj) -> Type* {
    Node* mem = n->left;
    if (count_args(n->body) != 0) {
        fail_n(n, "lucb.check.call", "`bits` takes no argument");
    }
    n->resolved = nullptr;
    mem->resolved = nullptr;
    return bits_integer(obj->ty);
}

// The unsigned integer as wide as the float `t`: the type of its bits.
auto Checker::bits_integer(Type* t) -> Type* {
    const int width = is_float(t) ? float_bits(t) : 64;
    return named_scalar(width == 16 ? "u16" : width == 32 ? "u32" : "u64");
}

// `f64.bits(u)`, `f32.bits(u)`, and `f16.bits(u)`: a float built from its bits (§7.5).
auto Checker::check_float_from_bits(Node* n, Node* obj) -> Type* {
    Node* mem = n->left;
    Type* result = named_float(obj->text);
    Type* want = bits_integer(result);
    obj->ty = result;
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "`bits` takes the integer to reinterpret");
        return result;
    }
    Type* got = check_expr(n->body->left, want);
    if (!type_eq(got, want) && got->kind != TypeKind::UntypedInt) {
        fail_n(n->body->left, "lucb.check.type", "`bits` takes `" + type_name(want) + "`");
    } else if (got->kind == TypeKind::UntypedInt) {
        n->body->left->ty = coerce(n->body->left, got, want);
    }
    n->resolved = nullptr;
    mem->resolved = nullptr;
    return result;
}

} // namespace lucb
