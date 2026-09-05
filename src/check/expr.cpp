#include "check/checker.h"

#include "support/literal.h"

namespace lucb {

auto Checker::check_case_payload(Node* n, Node* cse, Type* et) -> Type* {
        Node* p = cse != nullptr ? cse->body : nullptr;
        Node* a = n->body;
        while (p != nullptr && a != nullptr) {
            Type* at = check_expr(a->left != nullptr ? a->left : a, p->ty);
            if (!type_eq(at, p->ty) && !can_widen(at, p->ty)) {
                fail_n(a, "lucb.check.type",
                       "payload `" + string(p->text) + "` has type " + type_name(p->ty));
            }
            a->resolved = p;
            p = p->next;
            a = a->next;
        }
        if (p != nullptr || a != nullptr) {
            fail_n(n, "lucb.check.call", "wrong number of payload fields");
        }
        return et;
    }

auto Checker::check_case_value(Node* n, Type* expected) -> Type* {
        Type* et = expected;
        if (is_opt(et)) {
            et = et->elem;
        }
        if (!is_enum(et) || et->decl == nullptr) {
            fail_n(n, "lucb.check.type", "a case value needs an expected enum type");
            return t_error();
        }
        Node* cse = enum_case(et->decl, n->text);
        if (cse == nullptr) {
            fail_n(n, "lucb.check.name", "no case `" + string(n->text) + "`");
            return t_error();
        }
        n->resolved = cse;
        if (n->body != nullptr) {
            if (cse->body == nullptr) {
                fail_n(n, "lucb.check.type", "this case has no payload");
                return t_error();
            }
            return check_case_payload(n, cse, et);
        }
        if (cse->body != nullptr) {
            fail_n(n, "lucb.check.type", "this case needs a payload");
            return t_error();
        }
        return et;
    }

auto Checker::check_expr(Node* n, Type* expected) -> Type* {
        if (n == nullptr) {
            return t_error();
        }
        Type* t = t_error();
        switch (n->kind) {
        case NodeKind::Literal:
            t = check_literal(n, expected);
            break;
        case NodeKind::Name:
            t = check_name(n);
            break;
        case NodeKind::Self:
            t = check_self(n);
            break;
        case NodeKind::Unary:
            t = check_unary(n, expected);
            break;
        case NodeKind::Binary:
            t = check_binary(n, expected);
            break;
        case NodeKind::Call:
            t = check_call(n, expected);
            break;
        case NodeKind::Member:
            t = check_member(n, false);
            break;
        case NodeKind::CaseValue:
            t = check_case_value(n, expected);
            break;
        case NodeKind::Group:
            t = check_expr(n->left, expected);
            break;
        case NodeKind::Unit:
            t = t_unit();
            break;
        case NodeKind::Cast:
            t = check_cast(n, false);
            break;
        case NodeKind::Else:
            t = check_else(n, expected);
            break;
        case NodeKind::Catch:
            t = check_catch(n, expected);
            break;
        case NodeKind::Match:
        case NodeKind::MatchExpr:
            t = check_match(n, expected);
            break;
        case NodeKind::Index:
            t = check_index(n);
            break;
        case NodeKind::Slice:
            t = check_slice(n);
            break;
        case NodeKind::ArrayLit:
            t = check_array_lit(n, expected);
            break;
        case NodeKind::SpanMake:
            t = check_span_make(n);
            break;
        case NodeKind::New:
            t = check_new(n);
            break;
        case NodeKind::Alloc:
            t = check_alloc(n);
            break;
        case NodeKind::Formatted:
            t = check_formatted(n);
            break;
        case NodeKind::Conditional: {
            Type* tc = check_expr(n->type, t_bool());
            Type* tv = check_expr(n->left, expected);
            Type* ta = check_expr(n->right, expected);
            if (!type_eq(tc, t_bool())) {
                fail_n(n, "lucb.check.type", "a condition must be `bool`");
            }
            if (!type_eq(tv, ta)) {
                fail_n(n, "lucb.check.type", "conditional branches must have the same type");
            }
            t = tv;
            break;
        }
        default:
            fail_n(n, "lucb.check.unsupported", "this expression is not in the scalar core yet");
            t = t_error();
            break;
        }
        t = coerce(n, t, expected);
        n->ty = t;
        return t;
    }

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
            if (dest == nullptr || !is_int(dest)) {
                fail_n(n, "lucb.check.type",
                       "expected `" + type_name(expected) + "`, got an integer literal");
                return t_error();
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
                    fail_n(n, "lucb.check.mut",
                           "a mutating interface view needs a `var` receiver");
                }
                return expected;
            }
            if (got->kind == TypeKind::Interface && got->decl == expected->decl) {
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
        if (type_eq(got, expected) || can_widen(got, expected) || can_ptr_convert(got, expected, n)) {
            if (is_array(got) && is_span(expected)) {
                mark_local(n);
            }
            return expected;
        }
        if (is_opt(expected) &&
            (type_eq(got, expected->elem) || can_widen(got, expected->elem))) {
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
        if (can_widen(a, b)) {
            return b;
        }
        if (can_widen(b, a)) {
            return a;
        }
        return nullptr;
    }

auto Checker::check_literal(Node* n, Type* expected) -> Type* {
        if (n->op == TokenKind::KwNone) {
            if (expected != nullptr && is_ptr(expected) && expected->is_nullable) {
                return expected;
            }
            if (expected != nullptr && is_opt(expected)) {
                return expected;
            }
            fail_n(n, "lucb.check.type", "`none` needs an optional type");
            return t_error();
        }
        if (n->op == TokenKind::KwTrue || n->op == TokenKind::KwFalse) {
            return t_bool();
        }
        if (n->op == TokenKind::StringLit) {
            if (expected != nullptr && expected->kind == TypeKind::CStr) {
                return ty_cstr;
            }
            return t_str();
        }
        if (n->op == TokenKind::CharLit) {
            uint32_t cp = 0;
            if (!parse_char_literal(n->text, &cp)) {
                fail_n(n, "lucb.check.number", "invalid character literal");
                return t_error();
            }
            if (expected != nullptr && expected->kind == TypeKind::U8 && cp <= 127) {
                return ty_u8;
            }
            return ty_char;
        }
        if (n->op == TokenKind::IntLit) {
            ParsedInt p = parse_int_literal(n->text);
            if (!p.ok) {
                fail_n(n, "lucb.check.number", "invalid integer literal");
                return t_error();
            }
            if (!p.suffix.empty()) {
                Type* dest = named_scalar(p.suffix);
                if (dest == nullptr || !is_int(dest)) {
                    fail_n(n, "lucb.check.number", "unknown integer suffix");
                    return t_error();
                }
                if (!int_fits(p.value, false, dest)) {
                    fail_n(n, "lucb.check.number",
                           "integer literal does not fit in `" + type_name(dest) + "`");
                    return t_error();
                }
                return dest;
            }
            if (expected != nullptr && is_int(expected)) {
                return t_untyped();
            }
            return t_untyped();
        }
        if (n->op == TokenKind::FloatLit) {
            ParsedFloat p = parse_float_literal(n->text);
            if (!p.ok) {
                fail_n(n, "lucb.check.number", "invalid float literal");
                return t_error();
            }
            if (p.suffix == "f16") {
                fail_n(n, "lucb.check.unsupported", "`f16` is not in this slice");
                return t_error();
            }
            if (p.suffix == "f32") {
                return ty_f32;
            }
            if (p.suffix == "f64" || p.suffix.empty()) {
                if (expected != nullptr && expected->kind == TypeKind::F32 && p.suffix.empty()) {
                    return ty_f32;
                }
                return ty_f64;
            }
            fail_n(n, "lucb.check.number", "unknown float suffix");
            return t_error();
        }
        fail_n(n, "lucb.check.unsupported", "this literal is not in the scalar core yet");
        return t_error();
    }

auto Checker::check_name(Node* n) -> Type* {
        Binding* b = lookup(n->text);
        if (b == nullptr) {
            fail_n(n, "lucb.check.name", "unknown name `" + string(n->text) + "`");
            return t_error();
        }
        n->resolved = b->decl;
        mark_import(b);
        if (b->from_local) {
            mark_local(n);
        }
        return b->type;
    }

auto Checker::check_self(Node* n) -> Type* {
        Binding* b = lookup("self");
        if (b == nullptr) {
            fail_n(n, "lucb.check.self", "`self` is only valid in a method");
            return t_error();
        }
        n->resolved = b->decl;
        return b->type;
    }

auto Checker::check_unary(Node* n, Type* expected) -> Type* {
        if (n->op == TokenKind::KwTry) {
            Type* inner = check_expr(n->left, nullptr);
            if (!is_fail(inner)) {
                fail_n(n, "lucb.check.type", "`try` needs a fallible expression");
                return t_error();
            }
            if (!fallible_fn) {
                fail_n(n, "lucb.check.type", "`try` is only valid in a fallible function");
                return t_error();
            }
            return inner->elem != nullptr ? inner->elem : t_unit();
        }
        if (n->op == TokenKind::KwNot) {
            Type* inner = check_expr(n->left, t_bool());
            if (!type_eq(inner, t_bool())) {
                fail_n(n, "lucb.check.type", "`not` requires `bool`");
            }
            return t_bool();
        }
        if (n->op == TokenKind::Tilde) {
            Type* inner = check_expr(n->left, expected);
            if (!is_int(inner) && !is_int_enum(inner)) {
                fail_n(n, "lucb.check.type", "`~` requires an integer");
                return t_error();
            }
            return inner;
        }
        if (n->op == TokenKind::MinusPercent) {
            Type* inner = check_expr(n->left, expected);
            if (!is_int(inner)) {
                fail_n(n, "lucb.check.type", "wrapping negate requires an integer");
                return t_error();
            }
            return inner;
        }
        if (n->op == TokenKind::Plus) {
            Type* inner = check_expr(n->left, expected);
            if (!is_numeric(inner) && inner->kind != TypeKind::UntypedInt) {
                fail_n(n, "lucb.check.type", "unary `+` requires a number");
                return t_error();
            }
            return inner;
        }
        if (n->op == TokenKind::Minus) {
            Type* dest = expected;
            if (dest == nullptr || (!is_signed_int(dest) && !is_float(dest))) {
                dest = t_i64();
            }
            Type* inner = check_expr(n->left, nullptr);
            if (inner->kind == TypeKind::UntypedInt && n->left != nullptr &&
                n->left->kind == NodeKind::Literal) {
                ParsedInt p = parse_int_literal(n->left->text);
                Type* want = is_signed_int(dest) ? dest : t_i64();
                if (p.ok && p.suffix.empty() &&
                    p.value == static_cast<uint64_t>(int_max_signed(int_bits(want))) + 1) {
                    n->left->ty = want;
                    return want;
                }
                inner = coerce(n->left, inner, want);
                n->left->ty = inner;
            }
            if (is_unsigned_int(inner)) {
                fail_n(n, "lucb.check.type", "unary `-` is rejected on unsigned types; use `-%`");
                return t_error();
            }
            if (!is_signed_int(inner) && !is_float(inner)) {
                fail_n(n, "lucb.check.type", "unary `-` requires a signed number");
                return t_error();
            }
            return inner;
        }
        if (n->op == TokenKind::Star) {
            Type* inner = check_expr(n->left, nullptr);
            if (!is_ptr(inner) || inner->elem == nullptr || inner->elem->kind == TypeKind::Void) {
                fail_n(n, "lucb.check.type", "cannot dereference this type");
                return t_error();
            }
            if (is_local(n->left)) {
                mark_local(n);
            }
            return inner->elem;
        }
        if (n->op == TokenKind::Amp) {
            Type* inner = check_expr(n->left, nullptr);
            bool mut = is_mut_place(n->left);
            Type* p = intern_ptr(inner, !mut, false, false);
            if (n->left != nullptr && n->left->kind == NodeKind::Member) {
                Type* ot = n->left->left != nullptr ? n->left->left->ty : nullptr;
                if (is_ptr(ot)) {
                    ot = ot->elem;
                }
                if (ot != nullptr && ot->kind == TypeKind::Struct && ot->decl != nullptr &&
                    (ot->decl->flags & FlagPacked) != 0) {
                    int a = type_align(inner);
                    if (a > 1) {
                        fail_n(n, "lucb.check.type",
                               "cannot take the address of a packed field");
                    }
                }
            }
            if (n->left != nullptr &&
                (n->left->kind == NodeKind::Name || n->left->kind == NodeKind::Self ||
                 n->left->kind == NodeKind::Index || n->left->kind == NodeKind::Member ||
                 n->left->kind == NodeKind::Unary)) {
                mark_local(n);
            }
            return p;
        }
        fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
        return t_error();
    }

auto Checker::as_index_type(Node* n) -> Type* {
        Type* t = check_expr(n, t_usize());
        if (is_int(t) || t->kind == TypeKind::UntypedInt) {
            if (t->kind == TypeKind::UntypedInt) {
                n->ty = coerce(n, t, t_usize());
                t = n->ty;
            }
            return t;
        }
        fail_n(n, "lucb.check.type", "an index must be an integer");
        return t_error();
    }

auto Checker::check_index(Node* n) -> Type* {
        Type* base = check_expr(n->left, nullptr);
        as_index_type(n->body);
        if (is_ptr(base)) {
            if (base->elem == nullptr || base->elem->kind == TypeKind::Void) {
                fail_n(n, "lucb.check.type", "cannot index `void*`");
                return t_error();
            }
            return base->elem;
        }
        if (is_array(base) || is_span(base)) {
            if (is_local(n->left) || is_array(base)) {
                mark_local(n);
            }
            return base->elem;
        }
        fail_n(n, "lucb.check.type", "cannot index this type");
        return t_error();
    }

auto Checker::check_slice(Node* n) -> Type* {
        Type* base = check_expr(n->left, nullptr);
        if (n->body != nullptr) {
            as_index_type(n->body);
        }
        if (n->right != nullptr) {
            as_index_type(n->right);
        }
        Type* elem = nullptr;
        bool cnst = false;
        if (is_array(base)) {
            elem = base->elem;
            cnst = !is_mut_place(n->left);
            mark_local(n);
        } else if (is_span(base)) {
            elem = base->elem;
            cnst = base->is_const;
            if (is_local(n->left)) {
                mark_local(n);
            }
        } else if (base != nullptr && base->kind == TypeKind::Str) {
            elem = ty_u8;
            cnst = true;
            if (is_local(n->left)) {
                mark_local(n);
            }
        } else {
            fail_n(n, "lucb.check.type", "cannot slice this type");
            return t_error();
        }
        return intern_sp(elem, cnst);
    }

auto Checker::check_array_lit(Node* n, Type* expected) -> Type* {
        Type* elem = nullptr;
        uint64_t count = 0;
        if (expected != nullptr && is_array(expected)) {
            elem = expected->elem;
        }
        for (Node* e = n->body; e != nullptr; e = e->next) {
            Type* want = elem;
            if (want != nullptr && want->kind == TypeKind::UntypedInt) {
                want = t_i64();
            }
            Type* et = check_expr(e, want);
            if (elem == nullptr || elem->kind == TypeKind::UntypedInt) {
                if (et != nullptr && et->kind == TypeKind::UntypedInt) {
                    elem = t_i64();
                    coerce(e, et, elem);
                    e->ty = elem;
                } else {
                    elem = et;
                }
            } else if (!type_eq(et, elem) && !can_widen(et, elem)) {
                fail_n(e, "lucb.check.type", "array elements must have one type");
            }
            count++;
        }
        if (elem == nullptr) {
            fail_n(n, "lucb.check.type", "cannot infer an empty array literal");
            return t_error();
        }
        if (elem->kind == TypeKind::UntypedInt) {
            elem = t_i64();
        }
        if (expected != nullptr && is_array(expected) && expected->length != count) {
            fail_n(n, "lucb.check.type", "array literal has the wrong length");
        }
        return intern_arr(elem, count);
    }

auto Checker::check_span_make(Node* n) -> Type* {
        Type* elem = resolve_type(n->type);
        if (count_args(n->body) != 2) {
            fail_n(n, "lucb.check.call", "a span is made from a pointer and a length");
            return intern_sp(elem, false);
        }
        Type* p = check_expr(n->body->left, intern_ptr(elem, false, false, false));
        Type* len = check_expr(n->body->next != nullptr ? n->body->next->left : nullptr, t_usize());
        (void)p;
        (void)len;
        if (is_local(n->body->left)) {
            mark_local(n);
        }
        return intern_sp(elem, is_ptr(p) && p->is_const);
    }

auto Checker::is_arith(TokenKind op) -> bool {
        return op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star ||
               op == TokenKind::SlashSlash || op == TokenKind::Percent ||
               op == TokenKind::PlusPercent || op == TokenKind::MinusPercent ||
               op == TokenKind::StarPercent || op == TokenKind::PlusPipe ||
               op == TokenKind::MinusPipe || op == TokenKind::StarPipe ||
               op == TokenKind::Slash || op == TokenKind::PlusQuestion ||
               op == TokenKind::MinusQuestion || op == TokenKind::StarQuestion;
    }

auto Checker::is_bit(TokenKind op) -> bool {
        return op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret ||
               op == TokenKind::LtLt || op == TokenKind::GtGt;
    }

auto Checker::check_binary(Node* n, Type* expected) -> Type* {
        TokenKind op = n->op;
        if (op == TokenKind::KwAnd || op == TokenKind::KwOr) {
            Type* L = check_expr(n->left, t_bool());
            Type* R = check_expr(n->right, t_bool());
            if (!type_eq(L, t_bool()) || !type_eq(R, t_bool())) {
                fail_n(n, "lucb.check.type", "`and`/`or` require `bool`");
            }
            return t_bool();
        }
        Type* L = check_expr(n->left, nullptr);
        Type* R = check_expr(n->right, nullptr);
        if (is_atomic(L)) {
            L = L->elem;
        }
        if (is_atomic(R)) {
            R = R->elem;
        }
        if (is_ptr(L) || is_ptr(R)) {
            if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
                return t_bool();
            }
            if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
                op == TokenKind::GtEq) {
                if (is_ptr(L) && is_ptr(R)) {
                    return t_bool();
                }
            }
            if ((op == TokenKind::Plus || op == TokenKind::Minus) && is_ptr(L) &&
                (is_int(R) || R->kind == TypeKind::UntypedInt)) {
                if (R->kind == TypeKind::UntypedInt) {
                    n->right->ty = coerce(n->right, R, t_usize());
                }
                if (is_local(n->left)) {
                    mark_local(n);
                }
                return L;
            }
            if (op == TokenKind::Minus && is_ptr(L) && is_ptr(R) && same_pointee(L, R)) {
                return ty_isize;
            }
            fail_n(n, "lucb.check.type", "invalid pointer arithmetic");
            return t_error();
        }
        if (is_int_enum(L) || is_int_enum(R)) {
            if (R->kind == TypeKind::UntypedInt) {
                R = coerce(n->right, R, is_int_enum(L) ? L->elem : t_i64());
                n->right->ty = R;
            }
            if (L->kind == TypeKind::UntypedInt) {
                L = coerce(n->left, L, is_int_enum(R) ? R->elem : t_i64());
                n->left->ty = L;
            }
            if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
                if (!type_eq(L, R)) {
                    fail_n(n, "lucb.check.type", "operands of `==` must have the same type");
                }
                return t_bool();
            }
            if ((op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret) &&
                type_eq(L, R) && is_int_enum(L)) {
                return L;
            }
            fail_n(n, "lucb.check.type", "invalid enum operator");
            return t_error();
        }
        if (L->kind == TypeKind::UntypedInt && R->kind == TypeKind::UntypedInt &&
            expected != nullptr && is_int(expected)) {
            L = coerce(n->left, L, expected);
            n->left->ty = L;
            R = coerce(n->right, R, expected);
            n->right->ty = R;
        }
        if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
            Type* u = unify_int(L, R);
            if (u != nullptr && is_int(u)) {
                if (L->kind == TypeKind::UntypedInt) {
                    n->left->ty = coerce(n->left, L, u);
                }
                if (R->kind == TypeKind::UntypedInt) {
                    n->right->ty = coerce(n->right, R, u);
                }
                return t_bool();
            }
            if (!type_eq(L, R)) {
                fail_n(n, "lucb.check.type", "operands of `==` must have the same type");
            }
            return t_bool();
        }
        if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
            op == TokenKind::GtEq) {
            Type* u = unify_int(L, R);
            if (u == nullptr || (!is_int(u) && !is_float(u))) {
                if (is_float(L) && is_float(R)) {
                    return t_bool();
                }
                fail_n(n, "lucb.check.type", "ordered comparison requires a number");
                return t_bool();
            }
            if (L->kind == TypeKind::UntypedInt) {
                n->left->ty = coerce(n->left, L, u);
            }
            if (R->kind == TypeKind::UntypedInt) {
                n->right->ty = coerce(n->right, R, u);
            }
            return t_bool();
        }
        if (op == TokenKind::Slash) {
            if (!is_float(L) || !is_float(R)) {
                fail_n(n, "lucb.check.type", "`/` requires float operands");
                return t_error();
            }
            return float_bits(L) >= float_bits(R) ? L : R;
        }
        if (is_arith(op) || is_bit(op)) {
            Type* u = unify_int(L, R);
            if (op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star) {
                if (is_float(L) && is_float(R)) {
                    return float_bits(L) >= float_bits(R) ? L : R;
                }
            }
            if (u == nullptr || !is_int(u)) {
                fail_n(n, "lucb.check.type", "arithmetic requires integers of one signedness");
                return t_error();
            }
            if (L->kind == TypeKind::UntypedInt) {
                n->left->ty = coerce(n->left, L, u);
            }
            if (R->kind == TypeKind::UntypedInt) {
                n->right->ty = coerce(n->right, R, u);
            }
            if ((op == TokenKind::LtLt || op == TokenKind::GtGt) && is_int(R)) {
                // shift count may be any integer; result is the left type
                return n->left->ty != nullptr ? n->left->ty : u;
            }
            (void)expected;
            if (op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
                op == TokenKind::StarQuestion) {
                return intern_opt(u);
            }
            return u;
        }
        fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
        return t_error();
    }

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
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "location") {
            return check_location(n);
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
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "CAllocator") {
            if (count_args(n->body) != 0) {
                fail_n(n, "lucb.check.call", "`CAllocator()` takes no arguments");
            }
            n->resolved = nullptr;
            return ty_alloc;
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
            fail_n(n, "lucb.check.type", "`" + string(callee->text) + "` is not callable");
            return t_error();
        }
        fail_n(n, "lucb.check.unsupported", "this call is not in the scalar core yet");
        return t_error();
    }

auto Checker::type_from_expr_or_name(Node* a) -> Type* {
        if (a == nullptr) {
            return t_error();
        }
        if (a->kind == NodeKind::Name) {
            Type* named = named_scalar(a->text);
            if (named != nullptr && lookup(a->text) == nullptr) {
                a->ty = named;
                return named;
            }
            Binding* b = lookup(a->text);
            if (b != nullptr && b->decl != nullptr &&
                (b->decl->kind == NodeKind::Struct || b->decl->kind == NodeKind::Enum ||
                 b->decl->kind == NodeKind::Union)) {
                a->ty = b->type;
                a->resolved = b->decl;
                return b->type;
            }
        }
        return check_expr(a, nullptr);
    }

auto Checker::check_sizeof(Node* n) -> Type* {
        if (count_args(n->body) != 1) {
            fail_n(n, "lucb.check.call", "`sizeof` takes one argument");
            return t_usize();
        }
        Type* t = type_from_expr_or_name(n->body->left);
        if (t->kind == TypeKind::Error) {
            return t_usize();
        }
        n->body->left->ty = t;
        return t_usize();
    }

auto Checker::check_assert(Node* n) -> Type* {
        n->resolved = nullptr;
        int count = count_args(n->body);
        if (count < 1 || count > 2) {
            fail_n(n, "lucb.check.call", "`assert` takes a condition");
            return t_unit();
        }
        Type* c = check_expr(n->body->left, t_bool());
        if (!type_eq(c, t_bool())) {
            fail_n(n, "lucb.check.type", "`assert` needs a `bool`");
        }
        if (count == 2 && n->body->next != nullptr) {
            Type* m = check_expr(n->body->next->left, t_str());
            if (!type_eq(m, t_str())) {
                fail_n(n, "lucb.check.type", "`assert` message must be `str`");
            }
        }
        return t_unit();
    }

auto Checker::check_offsetof(Node* n) -> Type* {
        int count = count_args(n->body);
        if (count != 2) {
            fail_n(n, "lucb.check.call", "`offsetof` takes a type and a field");
            return t_usize();
        }
        Type* t = type_from_expr_or_name(n->body->left);
        Node* field = n->body->next != nullptr ? n->body->next->left : nullptr;
        if (t->kind != TypeKind::Struct && !is_union(t)) {
            fail_n(n, "lucb.check.type", "`offsetof` needs a struct or union");
            return t_usize();
        }
        if (field == nullptr || field->kind != NodeKind::Name) {
            fail_n(n, "lucb.check.type", "`offsetof` needs a field name");
            return t_usize();
        }
        Node* f = struct_member(t->decl, field->text, NodeKind::Field);
        if (f == nullptr) {
            fail_n(n, "lucb.check.name", "no field `" + string(field->text) + "`");
        }
        n->body->left->ty = t;
        field->resolved = f;
        return t_usize();
    }

auto Checker::check_alignof(Node* n) -> Type* {
        if (count_args(n->body) != 1) {
            fail_n(n, "lucb.check.call", "`alignof` takes one type");
            return t_usize();
        }
        Type* t = type_from_expr_or_name(n->body->left);
        n->body->left->ty = t;
        return t_usize();
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
        return dest;
    }

auto Checker::check_cast(Node* n, bool checked) -> Type* {
        Type* dest = resolve_type(n->type);
        Type* src = check_expr(n->left, nullptr);
        if (!convert_ok(n, src, dest, checked)) {
            return t_error();
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
        Node* srcn = n->kind == NodeKind::Cast ? n->left
                                               : (n->body != nullptr ? n->body->left : n);
        if (src->kind == TypeKind::UntypedInt) {
            if (checked && is_int(dest)) {
                src = coerce(srcn, src, dest);
            } else if (is_int(dest)) {
                src = dest;
            } else {
                src = coerce(srcn, src, t_i64());
            }
            if (srcn != nullptr) {
                srcn->ty = src;
            }
        }
        if (type_eq(src, dest) || can_widen(src, dest)) {
            return true;
        }
        if (is_ptr(src) && is_ptr(dest)) {
            return true;
        }
        if (is_int(src) && is_int(dest)) {
            if (checked && n->kind == NodeKind::Call && n->body != nullptr &&
                n->body->left != nullptr && n->body->left->kind == NodeKind::Literal) {
                ParsedInt p = parse_int_literal(n->body->left->text);
                if (p.ok && !int_fits(p.value, false, dest)) {
                    fail_n(n, "lucb.check.number",
                           "this conversion cannot succeed for this literal");
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
        fail_n(n, "lucb.check.type",
               "cannot convert `" + type_name(src) + "` to `" + type_name(dest) + "`");
        return false;
    }

auto Checker::is_display(Type* t) -> bool {
        if (t == nullptr) {
            return false;
        }
        return is_int(t) || is_float(t) || t->kind == TypeKind::Bool || t->kind == TypeKind::Str ||
               t->kind == TypeKind::Char || is_ptr(t);
    }

auto Checker::check_formatted(Node* n) -> Type* {
        for (Node* p = n->body; p != nullptr; p = p->next) {
            if (p->kind == NodeKind::FormatField) {
                Type* ft = check_expr(p->left, nullptr);
                if (ft != nullptr && ft->kind == TypeKind::UntypedInt) {
                    ft = coerce(p->left, ft, t_i64());
                    p->left->ty = ft;
                }
                if (!is_display(ft)) {
                    fail_n(p, "lucb.check.type", "this value cannot be formatted");
                }
            }
        }
        return ty_fmt;
    }

auto Checker::check_print(Node* n) -> Type* {
        n->resolved = nullptr;
        int count = count_args(n->body);
        if (count != 1) {
            fail_n(n, "lucb.check.call", "`print` takes one argument");
            return t_unit();
        }
        Type* a = check_expr(n->body->left, nullptr);
        if (is_atomic(a)) {
            a = a->elem;
            n->body->left->ty = a;
        }
        if (a != nullptr && a->kind == TypeKind::UntypedInt) {
            a = coerce(n->body->left, a, t_i64());
            n->body->left->ty = a;
        }
        if (!is_int(a) && !is_float(a) && a->kind != TypeKind::Bool && a->kind != TypeKind::Str &&
            a->kind != TypeKind::Char && a->kind != TypeKind::Fmt) {
            fail_n(n, "lucb.check.type", "`print` takes a scalar, `str`, or a formatted string");
        }
        return t_unit();
    }

auto Checker::check_format(Node* n) -> Type* {
        n->resolved = nullptr;
        if (count_args(n->body) != 2) {
            fail_n(n, "lucb.check.call", "`format` takes a buffer and a formatted string");
            return intern_fail(t_str());
        }
        Type* buf = check_expr(n->body->left, intern_sp(ty_u8, false));
        if (!is_span(buf) || buf->elem == nullptr || buf->elem->kind != TypeKind::U8) {
            fail_n(n, "lucb.check.type", "`format` needs a `u8[]` buffer");
        }
        Type* msg = check_expr(n->body->next != nullptr ? n->body->next->left : nullptr, ty_fmt);
        if (msg != nullptr && msg->kind != TypeKind::Fmt && msg->kind != TypeKind::Str) {
            fail_n(n, "lucb.check.type", "`format` needs a formatted string or `str`");
        }
        return intern_fail(t_str());
    }

auto Checker::check_location(Node* n) -> Type* {
        n->resolved = nullptr;
        if (count_args(n->body) != 0) {
            fail_n(n, "lucb.check.call", "`location` takes no arguments");
        }
        return ty_location;
    }

auto Checker::check_error(Node* n) -> Type* {
        if (!fallible_fn && !in_catch) {
            fail_n(n, "lucb.check.type", "`error` is only valid in a fallible function or catch");
        }
        int count = count_args(n->body);
        if (count != 2) {
            fail_n(n, "lucb.check.call", "`error` takes a code and a message");
        } else {
            check_expr(n->body->left, nullptr);
            Type* msg = check_expr(n->body->next != nullptr ? n->body->next->left : nullptr, t_str());
            if (!type_eq(msg, t_str())) {
                fail_n(n, "lucb.check.type", "`error` message must be `str`");
            }
        }
        return t_never();
    }

auto Checker::check_else(Node* n, Type* expected) -> Type* {
        Type* left = check_expr(n->left, nullptr);
        if (is_opt(left)) {
            Type* fb = check_expr(n->right, expected != nullptr ? expected : left->elem);
            if (!type_eq(fb, left->elem) && !can_widen(fb, left->elem) &&
                !type_eq(fb, t_never())) {
                fail_n(n, "lucb.check.type", "`else` fallback must match the optional payload");
            }
            return left->elem;
        }
        if (is_ptr(left) && left->is_nullable) {
            Type* inner = intern_ptr(left->elem, left->is_const, left->is_volatile, false);
            Type* fb = check_expr(n->right, expected != nullptr ? expected : inner);
            (void)fb;
            return inner;
        }
        fail_n(n, "lucb.check.type", "`else` needs an optional");
        return t_error();
    }

auto Checker::check_catch(Node* n, Type* expected) -> Type* {
        Type* left = check_expr(n->left, nullptr);
        if (!is_fail(left)) {
            fail_n(n, "lucb.check.type", "`catch` needs a fallible expression");
            return t_error();
        }
        Type* payload = left->elem != nullptr ? left->elem : t_unit();
        bool saved = in_catch;
        Type* saved_ct = catch_type;
        in_catch = true;
        catch_type = payload;
        push_scope();
        if (!n->text.empty()) {
            bind(n->text, ty_err, false, n);
        }
        check_stmt(n->body);
        pop_scope();
        in_catch = saved;
        catch_type = saved_ct;
        (void)expected;
        return payload;
    }

auto Checker::pattern_covers_rest(Node* pat) -> bool {
        return pat != nullptr && pat->text == "_";
    }

auto Checker::check_match(Node* n, Type* expected) -> Type* {
        Type* scrut = check_expr(n->left, nullptr);
        Type* result = expected;
        bool saw_rest = false;
        bool saw_true = false;
        bool saw_false = false;
        bool saw_none = false;
        bool saw_some = false;
        vector<string_view> saw_cases;
        for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
            push_scope();
            for (Node* pat = arm->left; pat != nullptr; pat = pat->next) {
                if (pat->text == "_") {
                    saw_rest = true;
                } else if (pat->text == "none") {
                    saw_none = true;
                } else if (pat->text == "some") {
                    saw_some = true;
                    if (pat->body != nullptr && !pat->body->text.empty() && is_opt(scrut)) {
                        bind(pat->body->text, scrut->elem, false, pat);
                    }
                } else if (is_enum(scrut) && scrut->decl != nullptr && !pat->text.empty() &&
                           pat->text != "_" && pat->left == nullptr) {
                    Node* cse = enum_case(scrut->decl, pat->text);
                    if (cse == nullptr) {
                        fail_n(pat, "lucb.check.name", "no case `" + string(pat->text) + "`");
                    } else {
                        saw_cases.push_back(pat->text);
                        pat->resolved = cse;
                        Node* p = cse->body;
                        Node* b = pat->body;
                        while (p != nullptr && b != nullptr) {
                            if (b->text != "_") {
                                bind(b->text, p->ty, false, b);
                            }
                            p = p->next;
                            b = b->next;
                        }
                    }
                } else if (pat->left != nullptr && pat->left->kind == NodeKind::Literal) {
                    if (pat->left->op == TokenKind::KwTrue) {
                        saw_true = true;
                    }
                    if (pat->left->op == TokenKind::KwFalse) {
                        saw_false = true;
                    }
                    check_expr(pat->left, scrut);
                }
            }
            if (arm->type != nullptr) {
                Type* g = check_expr(arm->type, t_bool());
                if (!type_eq(g, t_bool())) {
                    fail_n(arm, "lucb.check.type", "a match guard must be `bool`");
                }
            }
            if (n->kind == NodeKind::MatchExpr) {
                Type* bt = check_expr(arm->body, expected);
                if (result == nullptr) {
                    result = bt;
                }
            } else {
                check_stmt(arm->body);
            }
            pop_scope();
        }
        if (scrut != nullptr && scrut->kind == TypeKind::Bool && !saw_rest &&
            !(saw_true && saw_false)) {
            fail_n(n, "lucb.check.match", "`match` on `bool` is not exhaustive");
        }
        if (is_opt(scrut) && !saw_rest && !(saw_none && saw_some)) {
            fail_n(n, "lucb.check.match", "`match` on an optional is not exhaustive");
        }
        if (is_int(scrut) && !saw_rest) {
            fail_n(n, "lucb.check.match", "`match` on an integer needs a `_` arm");
        }
        if (is_int_enum(scrut) && !saw_rest) {
            fail_n(n, "lucb.check.match", "`match` on an integer-backed enum needs a `_` arm");
        }
        if (is_enum(scrut) && !is_int_enum(scrut) && !saw_rest && scrut->decl != nullptr) {
            for (Node* c = scrut->decl->body; c != nullptr; c = c->next) {
                if (c->kind != NodeKind::EnumCase) {
                    continue;
                }
                bool hit = false;
                for (size_t i = 0; i < saw_cases.size(); i++) {
                    if (saw_cases[i] == c->text) {
                        hit = true;
                    }
                }
                if (!hit) {
                    fail_n(n, "lucb.check.match",
                           "`match` is missing case `" + string(c->text) + "`");
                    break;
                }
            }
        }
        return result != nullptr ? result : t_unit();
    }

auto Checker::check_trap(Node* n) -> Type* {
        int count = count_args(n->body);
        if (count != 1) {
            fail_n(n, "lucb.check.call", "`trap` takes one argument");
        } else {
            Type* a = check_expr(n->body->left);
            if (!type_eq(a, t_str())) {
                fail_n(n, "lucb.check.type", "`trap` takes a string");
            }
        }
        return t_never();
    }

auto Checker::check_ctor(Node* n, Node* st) -> Type* {
        Type* ty = st->ty;
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
                fail_n(n, "lucb.check.type",
                       "missing field `" + string(f->text) + "`");
            }
        }
        return ty;
    }

auto Checker::check_func_call(Node* n, Node* fn, Node* recv) -> Type* {
        n->resolved = fn;
        Node* params = fn->right;
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
                fail_n(a, "lucb.check.call", "argument name does not match parameter `" +
                                                 string(p->text) + "`");
            }
            Type* at = check_expr(a->left, p->ty);
            if (!type_eq(at, p->ty) && !can_widen(at, p->ty) && !can_ptr_convert(at, p->ty, a->left)) {
                fail_n(a, "lucb.check.type",
                       "parameter `" + string(p->text) + "` has type " + type_name(p->ty));
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
        if (is_float(t) && t->kind == TypeKind::F32) {
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
            fail_n(a, "lucb.check.type", "a variadic argument cannot be `str`; use `cstr`");
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
        int nparams = 0;
        int nfixed = 0;
        bool variadic = false;
        for (Node* p = fn->right; p != nullptr; p = p->next) {
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
        Type* result = fn->ty;
        if (result == nullptr) {
            result = t_unit();
        }
        return result;
    }

auto Checker::check_method_call(Node* n) -> Type* {
        Node* mem = n->left;
        Node* obj = mem->left;
        // Static: Point.origin() — obj is a type name.
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
                    return check_ctor(n, d);
                }
                if (d->kind == NodeKind::Enum && is_int_enum(d->ty)) {
                    return check_checked_conv(n, d->ty);
                }
                if (d->kind == NodeKind::Func) {
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
                            if (fb == nullptr || fb->decl == nullptr ||
                                fb->decl->kind != NodeKind::Func) {
                                fail_n(n, "lucb.check.type", "`thread.spawn` needs a function name");
                            } else {
                                entry->resolved = fb->decl;
                            }
                        }
                        if (n->body != nullptr && n->body->next != nullptr) {
                            Type* ct = check_expr(n->body->next->left, nullptr);
                            if (!is_ptr(ct) && ct->kind != TypeKind::Void) {
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
            if (mem->text == "add" || mem->text == "sub" || mem->text == "swap" ||
                mem->text == "set" || mem->text == "clear" || mem->text == "flip" ||
                mem->text == "max" || mem->text == "min") {
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
                        (a->left->kind == NodeKind::CaseValue ||
                         a->left->kind == NodeKind::Member) &&
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
            (recv->kind != TypeKind::Struct && recv->kind != TypeKind::Union) ||
            recv->decl == nullptr) {
            fail_n(n, "lucb.check.type", "methods are called on structs");
            return t_error();
        }
        Node* method = struct_member(recv->decl, mem->text, NodeKind::Func);
        if (method == nullptr) {
            fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
            return t_error();
        }
        if ((method->flags & FlagStatic) != 0) {
            fail_n(n, "lucb.check.call", "a static method is called on the type");
        }
        bool mut_ok = is_mut_place(obj) || (is_ptr(ot) && !ot->is_const);
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
            Type* t = c_alias(keep("c." + string(n->text)));
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
        if (ot == nullptr || ot->kind != TypeKind::Struct || ot->decl == nullptr) {
            fail_n(n, "lucb.check.type", "field access needs a struct");
            return t_error();
        }
        Node* field = struct_member(ot->decl, n->text, NodeKind::Field);
        if (field == nullptr) {
            fail_n(n, "lucb.check.name", "no field `" + string(n->text) + "`");
            return t_error();
        }
        n->resolved = field;
        if (is_local(n->left)) {
            mark_local(n);
        }
        return field->ty;
    }

auto Checker::is_mut_place(Node* n) -> bool {
        if (n == nullptr) {
            return false;
        }
        if (n->kind == NodeKind::Name) {
            Binding* b = lookup(n->text);
            return b != nullptr && b->mut;
        }
        if (n->kind == NodeKind::Self) {
            Binding* b = lookup("self");
            return b != nullptr && b->mut;
        }
        if (n->kind == NodeKind::Member) {
            if (n->resolved != nullptr && n->resolved->kind == NodeKind::Global) {
                return true;
            }
            Type* ot = n->left != nullptr ? n->left->ty : nullptr;
            if (is_ptr(ot)) {
                return !ot->is_const;
            }
            return is_mut_place(n->left);
        }
        if (n->kind == NodeKind::Unary && n->op == TokenKind::Star) {
            Type* p = n->left != nullptr ? n->left->ty : nullptr;
            return is_ptr(p) && !p->is_const;
        }
        if (n->kind == NodeKind::Index) {
            Type* b = n->left != nullptr ? n->left->ty : nullptr;
            if (is_ptr(b)) {
                return !b->is_const;
            }
            if (is_span(b)) {
                return !b->is_const;
            }
            if (is_array(b)) {
                return is_mut_place(n->left);
            }
        }
        return false;
    }

} // namespace lucb
