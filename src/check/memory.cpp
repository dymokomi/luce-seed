//==============================================================================================
//
//   check/memory - Allocation expressions: new, alloc, free, with, in
//
//   DESCRIPTION:
//       Checks the allocation forms of base.md §12: the allocated type and count, the
//       allocator named by `in`, `free` of a pointer or span, and the `with` scope that makes
//       an allocator current. Allocation is written, never hidden: nothing else in the
//       checker allocates.
//
//==============================================================================================

#include "check/checker.h"

#include "support/literal.h"

namespace lucb {

auto Checker::is_fixed(Type* t) -> bool {
    return t != nullptr && t->kind == TypeKind::Struct && t->name == "FixedBuffer";
}

auto Checker::is_alloc_type(Type* t) -> bool {
    if (t == nullptr) {
        return false;
    }
    if (t->kind == TypeKind::Allocator) {
        return true;
    }
    if (t->kind == TypeKind::Interface && t->decl != nullptr && t->decl->text == "Allocator") {
        return true;
    }
    if (t->kind == TypeKind::Struct && t->decl != nullptr && struct_implements(t->decl, ty_alloc)) {
        return true;
    }
    return is_fixed(t);
}

auto Checker::check_in_allocator(Node* n) -> Type* {
    if (n == nullptr) {
        return ty_alloc;
    }
    Type* t = check_expr(n);
    if (t != nullptr &&
        (t->kind == TypeKind::Allocator ||
         (t->kind == TypeKind::Interface && t->decl != nullptr && t->decl->text == "Allocator"))) {
        return t;
    }
    if (t != nullptr && t->kind == TypeKind::Struct && t->decl != nullptr &&
        struct_implements(t->decl, ty_alloc)) {
        if (!is_mut_place(n)) {
            fail_n(n, "lucb.check.mut", "`with`/`in` needs a `var` allocator");
        }
        return t;
    }
    if (is_fixed(t)) {
        if (!is_mut_place(n)) {
            fail_n(n, "lucb.check.mut", "`with`/`in` needs a `var` FixedBuffer or an Allocator");
        }
        return t;
    }
    // `in &arena`: a pointer to an implementing struct forms the view (§14.3).
    if (is_ptr(t) && !t->is_const && t->elem != nullptr &&
        ((t->elem->kind == TypeKind::Struct && t->elem->decl != nullptr &&
          struct_implements(t->elem->decl, ty_alloc)) ||
         is_fixed(t->elem))) {
        return t;
    }
    fail_n(n, "lucb.check.type", "`with`/`in` needs an Allocator");
    return t_error();
}

auto Checker::check_new(Node* n) -> Type* {
    if (n->right != nullptr) {
        check_in_allocator(n->right);
    }
    Node* tn = n->type;
    if (tn == nullptr) {
        fail_n(n, "lucb.check.type", "`new` needs a type");
        return intern_fail(t_error());
    }
    if (tn->flags & FlagArray) {
        Type* elem = resolve_type(tn->left);
        if (tn->right != nullptr) {
            Type* ct = check_expr(tn->right, ty_usize);
            if (!is_int(ct) && ct->kind != TypeKind::UntypedInt) {
                fail_n(tn->right, "lucb.check.type", "`new T[count]` needs a `usize` count");
            }
        } else {
            fail_n(n, "lucb.check.type", "`new T[count]` needs a count");
        }
        return intern_fail(intern_sp(elem, false));
    }
    Type* t = resolve_type(tn);
    if (n->body != nullptr && n->body->kind == NodeKind::CaseValue) {
        n->body->ty = t;
        check_case_value(n->body, t);
    } else if (n->body != nullptr) {
        if (t->kind != TypeKind::Struct || t->decl == nullptr) {
            fail_n(n, "lucb.check.type", "`new T(...)` needs a struct type");
        } else {
            n->resolved = t->decl;
            check_ctor(n, t->decl);
        }
    } else if (!is_zeroable(t)) {
        fail_n(n, "lucb.check.type", "`new T` needs a zeroable type or an initialiser");
    }
    return intern_fail(intern_ptr(t, false, false, false));
}

auto Checker::check_alloc(Node* n) -> Type* {
    if (n->right != nullptr) {
        check_in_allocator(n->right);
    }
    if (n->type == nullptr) {
        int nargs = count_args(n->body);
        if (nargs != 2) {
            fail_n(n, "lucb.check.call", "`alloc(size, alignment)` takes two arguments");
        } else {
            check_expr(n->body->left, ty_usize);
            if (n->body->next != nullptr) {
                check_expr(n->body->next->left, ty_usize);
            }
        }
        return intern_fail(intern_sp(ty_u8, false));
    }
    if (n->type->flags & FlagArray) {
        Type* elem = resolve_type(n->type->left);
        if (n->type->right != nullptr) {
            Type* ct = check_expr(n->type->right, ty_usize);
            if (!is_int(ct) && ct->kind != TypeKind::UntypedInt) {
                fail_n(n->type->right, "lucb.check.type", "`alloc T[count]` needs a `usize` count");
            }
        } else {
            fail_n(n, "lucb.check.type", "`alloc T[count]` needs a count");
        }
        return intern_fail(intern_sp(elem, false));
    }
    fail_n(n, "lucb.check.type", "`alloc` needs `T[count]` or `(size, alignment)`");
    return intern_fail(t_error());
}

auto Checker::check_free(Node* n) -> void {
    Type* t = check_expr(n->left);
    if (!is_ptr(t) && !is_span(t)) {
        fail_n(n, "lucb.check.type", "`free` needs a pointer or a span");
    }
    if (n->right != nullptr) {
        check_in_allocator(n->right);
    }
}

auto Checker::check_with(Node* n) -> void {
    check_in_allocator(n->left);
    push_scope();
    check_stmt(n->body);
    pop_scope();
}

} // namespace lucb
