//==============================================================================================
//
//   check/convert - Conversions: widening, checked T(x), C-style (T)x, and pointer casts
//
//   DESCRIPTION:
//       The conversion rules of base.md §7.5: implicit same-signedness widening is the one
//       silent conversion; `T(x)` traps on a value that does not fit; `(T)x` truncates or
//       reinterprets as C does; pointer casts follow §7.7. `coerce` is the single place an
//       expression's type is adjusted to its context.
//
//==============================================================================================

#include <vector>
#include "check/checker.h"

#include "support/literal.h"

namespace lucb {

auto Checker::int_fits(uint64_t mag, bool neg, Type* dest) -> bool {
    if (dest == nullptr || !is_int(dest)) {
        return false;
    }
    int bits = int_bits(dest);
    if (is_unsigned_int(dest)) {
        if (neg) {
            return mag == 0;
        }
        return mag <= int_max_unsigned(bits);
    }
    if (!neg) {
        return mag <= static_cast<uint64_t>(int_max_signed(bits));
    }
    uint64_t limit = static_cast<uint64_t>(int_max_signed(bits)) + 1;
    return mag <= limit;
}

auto Checker::coerce(Node* n, Type* got, Type* expected) -> Type* {
    if (got == nullptr) {
        return t_error();
    }
    if (got->kind == TypeKind::UntypedInt) {
        if (expected == nullptr) {
            return t_untyped();
        }
        Type* dest = expected;
        if (is_opt(expected)) {
            dest = expected->elem;
        }
        if (dest != nullptr && dest->kind == TypeKind::UntypedInt) {
            dest = t_i64();
        }
        if (dest != nullptr && dest->kind == TypeKind::ErrorCode) {
            return is_opt(expected) ? expected : dest;
        }
        if (dest == nullptr || !is_int(dest)) {
            fail_n(n, "lucb.check.type",
                   "expected `" + type_name(expected) + "`, got an integer literal");
            return t_error();
        }
        if (n != nullptr && n->kind == NodeKind::Unary && n->op == TokenKind::Minus &&
            n->left != nullptr && n->left->kind == NodeKind::Literal &&
            n->left->op == TokenKind::IntLit) {
            // `-literal` that stayed untyped: the negative value must fit, so the most
            // negative value of a signed type is accepted although its magnitude alone is not
            ParsedInt p = parse_int_literal(n->left->text);
            if (!p.ok) {
                fail_n(n, "lucb.check.number", "invalid integer literal");
                return t_error();
            }
            if (is_unsigned_int(dest)) {
                fail_n(n, "lucb.check.type", "unary `-` is rejected on unsigned types; use `-%`");
                return t_error();
            }
            if (p.value > static_cast<uint64_t>(int_max_signed(int_bits(dest))) + 1) {
                fail_n(n, "lucb.check.number",
                       "integer literal does not fit in `" + type_name(dest) + "`");
                return t_error();
            }
            n->left->ty = dest;
            n->ty = dest;
            return is_opt(expected) ? expected : dest;
        }
        if (n != nullptr && n->kind == NodeKind::Literal && n->op == TokenKind::IntLit) {
            ParsedInt p = parse_int_literal(n->text);
            if (!p.ok) {
                fail_n(n, "lucb.check.number", "invalid integer literal");
                return t_error();
            }
            if (!int_fits(p.value, false, dest)) {
                fail_n(n, "lucb.check.number",
                       "integer literal does not fit in `" + type_name(dest) + "`");
                return t_error();
            }
        }
        return is_opt(expected) ? expected : dest;
    }
    if (got->kind == TypeKind::Never) {
        return expected != nullptr ? expected : got;
    }
    if (expected == nullptr) {
        return got;
    }
    if (expected->kind == TypeKind::Interface) {
        Type* conc = got;
        bool from_ptr = is_ptr(got);
        if (from_ptr) {
            conc = got->elem;
        }
        if (conc != nullptr && conc->kind == TypeKind::Struct &&
            struct_implements(conc->decl, expected)) {
            if (iface_has_mutating(expected) &&
                ((from_ptr && got->is_const) || (n != nullptr && !from_ptr && !is_mut_place(n)))) {
                fail_n(n, "lucb.check.mut", "a mutating interface view needs a `var` receiver");
            }
            return expected;
        }
        if (got->kind == TypeKind::Interface && got->decl == expected->decl) {
            return expected;
        }
        if (is_fixed(conc) && expected->decl != nullptr && expected->decl->text == "Allocator") {
            return expected; // the builtin FixedBuffer implements Allocator
        }
    }
    if (expected->kind == TypeKind::Allocator) {
        Type* conc = is_ptr(got) ? got->elem : got;
        if (got->kind == TypeKind::Allocator || is_fixed(conc) ||
            (conc != nullptr && conc->kind == TypeKind::Struct && conc->decl != nullptr &&
             struct_implements(conc->decl, ty_alloc))) {
            if (is_ptr(got) && got->is_const) {
                fail_n(n, "lucb.check.mut", "an allocator view needs a mutable pointer");
            }
            return expected;
        }
    }
    if (expected->kind == TypeKind::Fmt) {
        if (got->kind == TypeKind::Fmt || got->kind == TypeKind::Str) {
            return expected;
        }
    }
    if (expected->kind == TypeKind::CStr && got->kind == TypeKind::Str && n != nullptr &&
        n->kind == NodeKind::Literal && n->op == TokenKind::StringLit) {
        return expected;
    }
    if (is_atomic(got) && expected != nullptr &&
        (type_eq(got->elem, expected) || can_widen(got->elem, expected))) {
        return expected;
    }
    if (is_func(got) && is_func(expected) && func_converts(got, expected)) {
        if (n != nullptr && n->resolved != nullptr &&
            (n->resolved->kind == NodeKind::Func || n->resolved->kind == NodeKind::ExternFunc) &&
            !is_fail(got->elem) && is_fail(expected->elem) &&
            (n->resolved->flags & FlagFallible) == 0) {
            n->resolved = make_fail_thunk(n->resolved, expected);
        }
        return expected;
    }
    if (type_eq(got, expected) || can_widen(got, expected) || can_ptr_convert(got, expected, n)) {
        if (is_array(got) && is_span(expected) && place_is_local(n)) {
            mark_local(n);
        }
        return expected;
    }
    if (is_opt(expected) && (type_eq(got, expected->elem) || can_widen(got, expected->elem))) {
        return expected;
    }
    if (got->kind == TypeKind::Char && expected->kind == TypeKind::U8 &&
        n->kind == NodeKind::Literal) {
        uint32_t cp = 0;
        if (parse_char_literal(n->text, &cp) && cp <= 127) {
            return expected;
        }
    }
    fail_n(n, "lucb.check.type",
           "expected `" + type_name(expected) + "`, got `" + type_name(got) + "`");
    return t_error();
}

auto Checker::same_pointee(const Type* a, const Type* b) -> bool {
    return a != nullptr && b != nullptr && a->elem == b->elem;
}

auto Checker::can_ptr_convert(Type* from, Type* to, Node* n) -> bool {
    if (from == nullptr || to == nullptr) {
        return false;
    }
    if (is_array(from) && is_span(to) && from->elem == to->elem) {
        if (!to->is_const && n != nullptr && !is_mut_place(n)) {
            return false;
        }
        return true;
    }
    if (is_span(from) && is_span(to) && from->elem == to->elem) {
        if (from->is_const && !to->is_const) {
            return false;
        }
        return true;
    }
    if (from->kind == TypeKind::Str && is_span(to) && to->elem == ty_u8 && to->is_const) {
        return true;
    }
    if (!is_ptr(from) || !is_ptr(to)) {
        return false;
    }
    if (from->is_nullable && !to->is_nullable) {
        return false;
    }
    if (from->is_const && !to->is_const) {
        return false;
    }
    if (from->is_volatile && !to->is_volatile) {
        return false;
    }
    if (from->elem == to->elem) {
        return true;
    }
    if (to->elem != nullptr && to->elem->kind == TypeKind::Void) {
        return true;
    }
    return false;
}

auto Checker::unify_int(Type* a, Type* b) -> Type* {
    if (a == nullptr || b == nullptr) {
        return t_error();
    }
    if (a->kind == TypeKind::UntypedInt) {
        return b->kind == TypeKind::UntypedInt ? t_i64() : b;
    }
    if (b->kind == TypeKind::UntypedInt) {
        return a;
    }
    if (type_eq(a, b)) {
        return a;
    }
    if (is_int(a) && is_int(b) && is_signed_int(a) == is_signed_int(b) &&
        int_bits(a) == int_bits(b)) {
        if (a->kind == TypeKind::Usize || a->kind == TypeKind::Isize) {
            return a;
        }
        if (b->kind == TypeKind::Usize || b->kind == TypeKind::Isize) {
            return b;
        }
        return a;
    }
    if (can_widen(a, b)) {
        return b;
    }
    if (can_widen(b, a)) {
        return a;
    }
    return nullptr;
}

auto Checker::as_index_type(Node* n) -> Type* {
    Type* t = check_expr(n, nullptr);
    if (t != nullptr && t->kind == TypeKind::UntypedInt) {
        n->ty = coerce(n, t, t_usize());
        return n->ty;
    }
    if (is_int(t)) {
        return t;
    }
    fail_n(n, "lucb.check.type", "an index must be an integer");
    return t_error();
}

auto Checker::check_checked_conv(Node* n, Type* dest) -> Type* {
    if (count_args(n->body) != 1) {
        fail_n(n, "lucb.check.call", "a conversion takes one argument");
        return dest;
    }
    Node* arg = n->body->left;
    Type* src = check_expr(arg, nullptr);
    if (!convert_ok(n, src, dest, true)) {
        return t_error();
    }
    if (dest != nullptr && dest->kind == TypeKind::Str && src != nullptr && !type_eq(src, dest)) {
        return intern_fail(dest);
    }
    return dest;
}

auto Checker::check_cast(Node* n, bool checked) -> Type* {
    Type* dest = resolve_type(n->type);
    if (n->type != nullptr) {
        n->type->ty = dest; // the written target; n->ty may later widen to `T?`
    }
    Type* src = check_expr(n->left, nullptr);
    if (!convert_ok(n, src, dest, checked)) {
        return t_error();
    }
    // a reinterpreting cast of a local address keeps the escape taint of §6.6
    if (n->left != nullptr && is_local(n->left) &&
        (is_ptr(dest) || is_span(dest) || (dest != nullptr && dest->kind == TypeKind::Str))) {
        mark_local(n);
    }
    return dest;
}

auto Checker::convert_ok(Node* n, Type* src, Type* dest, bool checked) -> bool {
    if (src == nullptr || dest == nullptr) {
        return false;
    }
    if (is_atomic(src)) {
        src = src->elem;
    }
    Node* srcn = n->kind == NodeKind::Cast ? n->left : (n->body != nullptr ? n->body->left : n);
    std::vector<Node*> groups;
    while (srcn != nullptr && srcn->kind == NodeKind::Group && srcn->left != nullptr) {
        // `(i8)(-100)`: the parentheses are not the operand, but they carry its type
        groups.push_back(srcn);
        srcn = srcn->left;
    }
    if (src->kind == TypeKind::UntypedInt) {
        bool negated = srcn != nullptr && srcn->kind == NodeKind::Unary && srcn->op == TokenKind::Minus;
        if (checked && is_int(dest)) {
            src = coerce(srcn, src, dest);
        } else if (is_int(dest) && !negated) {
            src = dest;
        } else {
            // `(i8)(-300)` truncates as C does: the negated literal is an `i64` first
            src = coerce(srcn, src, t_i64());
        }
        if (srcn != nullptr) {
            srcn->ty = src;
        }
    }
    for (Node* g : groups) {
        g->ty = src;
    }
    if (type_eq(src, dest) || can_widen(src, dest)) {
        return true;
    }
    if (is_ptr(src) && is_ptr(dest)) {
        return true;
    }
    if (src->kind == TypeKind::Never) {
        return true;
    }
    if (is_ptr(src) && (dest->kind == TypeKind::Usize || dest->kind == TypeKind::Isize)) {
        return true;
    }
    if ((src->kind == TypeKind::Usize || src->kind == TypeKind::Isize) && is_ptr(dest)) {
        return true;
    }
    if (is_func(src) && is_ptr(dest) && dest->elem != nullptr &&
        dest->elem->kind == TypeKind::Void) {
        return true;
    }
    if (is_ptr(src) && src->elem != nullptr && src->elem->kind == TypeKind::Void && is_func(dest)) {
        return true;
    }
    if (is_func(src) && is_func(dest) && func_converts(src, dest)) {
        return true;
    }
    if (is_int(src) && is_int(dest)) {
        if (checked && n->kind == NodeKind::Call && n->body != nullptr &&
            n->body->left != nullptr && n->body->left->kind == NodeKind::Literal) {
            ParsedInt p = parse_int_literal(n->body->left->text);
            if (p.ok && !int_fits(p.value, false, dest)) {
                fail_n(n, "lucb.check.number", "this conversion cannot succeed for this literal");
                return false;
            }
        }
        return true;
    }
    if (is_int(src) && is_float(dest)) {
        return true;
    }
    if (is_float(src) && is_int(dest)) {
        return true;
    }
    if (is_float(src) && is_float(dest)) {
        return true;
    }
    if (src->kind == TypeKind::Char && is_int(dest)) {
        return true;
    }
    if (is_int(src) && dest->kind == TypeKind::Char) {
        return true;
    }
    if (is_int_enum(src) && is_int(dest)) {
        return true;
    }
    if (is_int(src) && is_int_enum(dest)) {
        return true;
    }
    if (src != nullptr && src->kind == TypeKind::ErrorCode && is_int(dest)) {
        return true;
    }
    if (is_int(src) && dest != nullptr && dest->kind == TypeKind::ErrorCode) {
        return true;
    }
    if (dest->kind == TypeKind::Str) {
        if (src->kind == TypeKind::CStr) {
            return true;
        }
        if ((is_span(src) || is_array(src)) && src->elem != nullptr &&
            src->elem->kind == TypeKind::U8) {
            return true;
        }
    }
    if (dest->kind == TypeKind::CStr && src->kind == TypeKind::Str) {
        return true;
    }
    // `cstr` is a pointer to C text: it converts to and from object pointers as one
    if (src->kind == TypeKind::CStr && is_ptr(dest)) {
        return true;
    }
    if (is_ptr(src) && dest->kind == TypeKind::CStr) {
        return true;
    }
    fail_n(n, "lucb.check.type",
           "cannot convert `" + type_name(src) + "` to `" + type_name(dest) + "`");
    return false;
}

} // namespace lucb
