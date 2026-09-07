//==============================================================================================
//
//   check/expr - Expressions: literals, names, operators, places, and matching
//
//   DESCRIPTION:
//       The expression checker for everything that is not a call or a conversion: literals
//       with contextual typing, names and `self`, unary and binary operators including the
//       checked, wrapping, saturating, and optional families (base.md §7), indexing and
//       slicing, array and tuple literals, `else`, `catch`, and `match` expressions, and the
//       place rules that decide what may be assigned or addressed (§6.5, §6.6).
//
//==============================================================================================

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
        t = check_name(n, expected);
        break;
    case NodeKind::Lambda:
        t = check_lambda(n, expected);
        break;
    case NodeKind::Tuple:
        t = check_tuple_expr(n, expected);
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
    case NodeKind::Return: {
        Type* rt = t_unit();
        if (n->left != nullptr) {
            rt = check_expr(n->left, return_type);
        }
        if (return_type != nullptr && !type_eq(rt, return_type) && !can_widen(rt, return_type) &&
            !can_ptr_convert(rt, return_type, n->left) && !type_eq(rt, t_never())) {
            fail_n(n, "lucb.check.type",
                   "return type is " + type_name(rt) + ", expected " + type_name(return_type));
        }
        t = t_never();
        break;
    }
    case NodeKind::Break:
    case NodeKind::Continue:
        t = t_never();
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
        // An untyped literal branch takes the other branch's type: `x if c else 0`.
        if (tv != nullptr && ta != nullptr && tv->kind == TypeKind::UntypedInt &&
            ta->kind != TypeKind::UntypedInt) {
            tv = coerce(n->left, tv, ta);
            n->left->ty = tv;
        } else if (tv != nullptr && ta != nullptr && ta->kind == TypeKind::UntypedInt &&
                   tv->kind != TypeKind::UntypedInt) {
            ta = coerce(n->right, ta, tv);
            n->right->ty = ta;
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
    if (n->kind != NodeKind::Return && n->kind != NodeKind::Break &&
        n->kind != NodeKind::Continue) {
        t = coerce(n, t, expected);
    }
    n->ty = t;
    return t;
}

auto Checker::check_literal(Node* n, Type* expected) -> Type* {
    if (n->op == TokenKind::KwNone) {
        if (is_null_niche(expected)) {
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
    if (n->op == TokenKind::BytesLit) {
        // `b"..."` is `u8[N]` static data, N the count after escapes (§4.4)
        return intern_arr(ty_u8, decode_string_literal(n->text).size());
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
        if (p.suffix == "f16") {
            return ty_f16;
        }
        if (p.suffix == "f32") {
            return ty_f32;
        }
        if (p.suffix == "f64" || p.suffix.empty()) {
            if (is_float(expected) && p.suffix.empty()) {
                return expected;
            }
            return ty_f64;
        }
        fail_n(n, "lucb.check.number", "unknown float suffix");
        return t_error();
    }
    fail_n(n, "lucb.check.unsupported", "this literal is not in the scalar core yet");
    return t_error();
}

auto Checker::check_name(Node* n, Type* expected) -> Type* {
    if (n->flags & FlagFormatSink) {
        n->ty = ty_writer; // the formatted string's own sink, a `Writer` (§14.4)
        return ty_writer;
    }
    Binding* b = lookup(n->text);
    if (b == nullptr) {
        fail_n(n, "lucb.check.name", "unknown name `" + string(n->text) + "`");
        return t_error();
    }
    mark_referenced(b->decl);
    n->resolved = b->decl;
    mark_import(b);
    if (lambda_depth > 0 && b->depth > 1 && b->depth < lambda_depth) {
        NodeKind k = b->decl != nullptr ? b->decl->kind : NodeKind::Name;
        if (k == NodeKind::Let || k == NodeKind::Var || k == NodeKind::Param) {
            fail_n(n, "lucb.check.type", "a lambda cannot capture");
            return t_error();
        }
    }
    if (b->decl != nullptr &&
        (b->decl->kind == NodeKind::Func || b->decl->kind == NodeKind::ExternFunc)) {
        Type* ft = func_type_of(b->decl, nullptr);
        Type* want = expected;
        if (is_opt(want) && want->elem != nullptr) {
            want = want->elem;
        }
        if (want == nullptr || is_func(want) || is_void_ptr(want)) {
            // a named function is a function pointer (§5.6): `let f = take_small`
            return ft;
        }
        fail_n(n, "lucb.check.type", "a function must be called");
        return t_error();
    }
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
    if (lambda_depth > 0 && b->depth < lambda_depth) {
        fail_n(n, "lucb.check.type", "a lambda cannot capture");
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
        if (expected == nullptr && inner != nullptr && inner->kind == TypeKind::UntypedInt &&
            n->left != nullptr && n->left->kind == NodeKind::Literal) {
            // `-literal` with no context stays untyped, so `-2147483647 - 1` can still be an `i32`
            return inner;
        }
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
        if (inner != nullptr && inner->kind == TypeKind::UntypedInt) {
            // `-(3 * 4)`: an untyped expression takes the context's signed type, else `i64`
            inner = coerce(n->left, inner, is_signed_int(dest) ? dest : t_i64());
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
        if (inner->is_nullable) {
            fail_n(n, "lucb.check.type", "unwrap a nullable pointer with `if let` or `else`");
            return t_error();
        }
        if (is_local(n->left)) {
            mark_local(n);
        }
        return inner->elem;
    }
    if (n->op == TokenKind::Amp) {
        if (n->left != nullptr && n->left->kind == NodeKind::Literal) {
            fail_n(n, "lucb.check.type", "cannot take the address of a literal");
            return t_error();
        }
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
                    fail_n(n, "lucb.check.type", "cannot take the address of a packed field");
                }
            }
        }
        if (place_is_local(n->left)) {
            mark_local(n);
        }
        return p;
    }
    fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
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
        if (is_local(n->left) || (is_array(base) && place_is_local(n->left))) {
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
        if (place_is_local(n->left)) {
            mark_local(n);
        }
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
           op == TokenKind::MinusPipe || op == TokenKind::StarPipe || op == TokenKind::Slash ||
           op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
           op == TokenKind::StarQuestion;
}

auto Checker::is_bit(TokenKind op) -> bool {
    return op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret ||
           op == TokenKind::LtLt || op == TokenKind::GtGt;
}

// An unsuffixed float literal, which adapts to the narrower float beside it (§7.1).
static bool is_plain_float_lit(Node* n) {
    if (n == nullptr || n->kind != NodeKind::Literal || n->op != TokenKind::FloatLit) {
        return false;
    }
    return parse_float_literal(n->text).suffix.empty();
}

static bool is_checked_arith(TokenKind op) {
    return op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
           op == TokenKind::StarQuestion;
}

static bool is_ascii_char_lit(Node* n) {
    if (n == nullptr || n->kind != NodeKind::Literal || n->op != TokenKind::CharLit) {
        return false;
    }
    uint32_t cp = 0;
    return parse_char_literal(n->text, &cp) && cp < 128;
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
    // `x == none`: the literal takes the other operand's optional type.
    bool none_right = n->right != nullptr && n->right->kind == NodeKind::Literal &&
                      n->right->op == TokenKind::KwNone;
    bool none_left = n->left != nullptr && n->left->kind == NodeKind::Literal &&
                     n->left->op == TokenKind::KwNone;
    // context chooses a float literal's type (§7.1): `let x: f16 = 1.0 / 3.0` divides halves;
    // a case value takes the enum the context names: `let both: Flag = .read | .write`
    Type* hint = is_float(expected) || is_int_enum(expected) ? expected : nullptr;
    if (is_checked_arith(op) && is_opt(expected) && is_int(expected->elem)) {
        hint = expected->elem; // `let fits: u8? = 1 +? 2` adds bytes
    }
    // `x == .quiet`: a bare case value takes the enum the other operand has
    const bool case_right = n->right != nullptr && n->right->kind == NodeKind::CaseValue;
    const bool case_left = n->left != nullptr && n->left->kind == NodeKind::CaseValue && !case_right;
    Type* L = none_left || case_left ? nullptr : check_expr(n->left, hint);
    Type* R = none_right || case_right ? check_expr(n->right, L) : check_expr(n->right, hint);
    if (none_left || case_left) {
        L = check_expr(n->left, R);
    }
    if (is_atomic(L)) {
        L = L->elem;
    }
    if (is_atomic(R)) {
        R = R->elem;
    }
    // base.md §7.5: an ASCII character literal adapts to a `u8` operand, so
    // `byte == 'a'`, `byte < '0'`, and `byte - '0'` read as bytes.
    if (L != nullptr && R != nullptr) {
        // and a plain float literal adapts to the narrower float beside it (§7.1)
        if (is_float(L) && R->kind == TypeKind::F64 && is_plain_float_lit(n->right)) {
            n->right->ty = L;
            R = L;
        } else if (is_float(R) && L->kind == TypeKind::F64 && is_plain_float_lit(n->left)) {
            n->left->ty = R;
            L = R;
        }
        if (L->kind == TypeKind::U8 && R->kind == TypeKind::Char && is_ascii_char_lit(n->right)) {
            n->right->ty = ty_u8;
            R = ty_u8;
        } else if (R->kind == TypeKind::U8 && L->kind == TypeKind::Char &&
                   is_ascii_char_lit(n->left)) {
            n->left->ty = ty_u8;
            L = ty_u8;
        }
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
    if (L->kind == TypeKind::UntypedInt && R->kind == TypeKind::UntypedInt && expected != nullptr &&
        is_int(expected)) {
        L = coerce(n->left, L, expected);
        n->left->ty = L;
        R = coerce(n->right, R, expected);
        n->right->ty = R;
    }
    if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
        if ((L != nullptr && L->kind == TypeKind::Interface) ||
            (R != nullptr && R->kind == TypeKind::Interface)) {
            fail_n(n, "lucb.check.type", "interface views cannot be compared");
            return t_bool();
        }
        if ((L != nullptr && L->kind == TypeKind::Union) || (R != nullptr && R->kind == TypeKind::Union)) {
            fail_n(n, "lucb.check.type", "unions have no equality (§7.4)");
            return t_bool();
        }
        // an optional against a plain value of its element type: the value converts (§5.8)
        bool none_right = n->right->kind == NodeKind::Literal && n->right->op == TokenKind::KwNone;
        bool none_left = n->left->kind == NodeKind::Literal && n->left->op == TokenKind::KwNone;
        if (is_opt(L) && !is_ptr(L) && R != nullptr && !is_opt(R) && !none_right &&
            (R->kind == TypeKind::UntypedInt || type_eq(L->elem, R) || can_widen(R, L->elem))) {
            R = coerce(n->right, R, L);
            n->right->ty = R;
        } else if (is_opt(R) && !is_ptr(R) && L != nullptr && !is_opt(L) && !none_left &&
                   (L->kind == TypeKind::UntypedInt || type_eq(R->elem, L) || can_widen(L, R->elem))) {
            L = coerce(n->left, L, R);
            n->left->ty = L;
        }
        if ((L != nullptr && L->kind == TypeKind::Param &&
             (L->bounds & (BoundEquatable | BoundComparable)) == 0) ||
            (R != nullptr && R->kind == TypeKind::Param &&
             (R->bounds & (BoundEquatable | BoundComparable)) == 0)) {
            fail_n(n, "lucb.check.type", "`==` on a type parameter needs `Equatable`");
        }
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
        if ((L != nullptr && L->kind == TypeKind::ErrorCode) ||
            (R != nullptr && R->kind == TypeKind::ErrorCode)) {
            return t_bool();
        }
        bool u8_char = (L != nullptr && R != nullptr &&
                        ((L->kind == TypeKind::U8 && R->kind == TypeKind::Char) ||
                         (L->kind == TypeKind::Char && R->kind == TypeKind::U8)));
        if (u8_char) {
            if (L->kind == TypeKind::Char && n->left != nullptr) {
                n->left->ty = ty_u8;
            }
            if (R->kind == TypeKind::Char && n->right != nullptr) {
                n->right->ty = ty_u8;
            }
            return t_bool();
        }
        if (!type_eq(L, R)) {
            fail_n(n, "lucb.check.type", "operands of `==` must have the same type");
        } else if (!has_equality(L)) {
            fail_n(n, "lucb.check.type", "these values have no equality (§7.4)");
        }
        return t_bool();
    }
    if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
        op == TokenKind::GtEq) {
        if (L != nullptr && R != nullptr && L->kind == TypeKind::Str && R->kind == TypeKind::Str) {
            // text orders by bytes, then by length (§5.5)
            return t_bool();
        }
        Type* u = unify_int(L, R);
        if (u == nullptr || (!is_int(u) && !is_float(u))) {
            if (is_float(L) && is_float(R)) {
                return t_bool();
            }
            fail_n(n, "lucb.check.type", "ordered comparison requires a number or text");
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
        // two untyped operands stay untyped, so `w == 256 | 7` and `(256 << 32) | 7` take the
        // width of the operand they meet (§7.5); a checked form needs a concrete type
        if (L != nullptr && L->kind == TypeKind::UntypedInt && R != nullptr &&
            R->kind == TypeKind::UntypedInt && expected == nullptr && !is_checked_arith(op)) {
            return t_untyped();
        }
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
            if (L != nullptr && L->kind == TypeKind::UntypedInt &&
                R->kind == TypeKind::UntypedInt) {
                return t_untyped();
            }
            return n->left->ty != nullptr ? n->left->ty : u;
        }
        (void)expected;
        if (op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
            op == TokenKind::StarQuestion) {
            if (is_atomic(n->left->ty) || is_atomic(n->right->ty)) {
                // checked arithmetic does not apply to atomics (§15.1)
                fail_n(n, "lucb.check.type", "checked arithmetic does not apply to an atomic; write a `cas` loop");
                return t_error();
            }
            return intern_opt(u);
        }
        return u;
    }
    fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
    return t_error();
}

auto Checker::check_else(Node* n, Type* expected) -> Type* {
    Type* left = check_expr(n->left, nullptr);
    if (is_opt(left)) {
        Type* fb = check_expr(n->right, expected != nullptr ? expected : left->elem);
        if (!type_eq(fb, left->elem) && !can_widen(fb, left->elem) && !type_eq(fb, t_never())) {
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
    if (is_func(left) && left->is_nullable) {
        Type* inner = intern_func(left->args, left->ntargs, left->elem, false);
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
    // a handler for a value recovers one or leaves; one for `unit` may fall through (§11.4)
    if (!type_eq(payload, t_unit()) && !terminates_handler(n->body)) {
        fail_n(n, "lucb.check.type",
               "a `catch` handler must `recover` a value or leave with `return`, `error`, `trap`, `break`, or `continue`");
    }
    (void)expected;
    return payload;
}

// A handler's every path ends in `recover`, `return`, `error`, `trap`, `break`, or `continue`.
auto Checker::terminates_handler(Node* n) -> bool {
    if (n == nullptr) {
        return false;
    }
    switch (n->kind) {
    case NodeKind::Break:
    case NodeKind::Continue:
        return true;
    case NodeKind::Block:
        for (Node* s = n->body; s != nullptr; s = s->next) {
            if (terminates_handler(s)) {
                return true;
            }
        }
        return false;
    case NodeKind::If:
        return terminates_handler(n->body) && terminates_handler(n->right);
    default:
        return always_returns(n);
    }
}

auto Checker::pattern_covers_rest(Node* pat) -> bool {
    return pat != nullptr && pat->text == "_";
}

auto Checker::check_match(Node* n, Type* expected) -> Type* {
    Type* scrut = check_expr(n->left, nullptr);
    if (scrut != nullptr && scrut->kind == TypeKind::UntypedInt) {
        scrut = coerce(n->left, scrut, t_i64());
        n->left->ty = scrut;
    }
    Type* result = expected;
    bool saw_rest = false;
    bool saw_true = false;
    bool saw_false = false;
    bool saw_none = false;
    bool saw_some = false;
    vector<string_view> saw_cases;
    vector<string> saw_literals;
    for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
        push_scope();
        bool guarded = arm->type != nullptr;
        // the names the arm's first pattern binds; every alternative binds the same (§8.4)
        vector<string_view> first_names;
        int index = 0;
        for (Node* pat = arm->left; pat != nullptr; pat = pat->next, index++) {
            if (saw_rest) {
                fail_n(pat, "lucb.check.match", "this pattern is unreachable: an earlier `_` arm takes everything");
            }
            size_t bound = 0;
            auto bind_name = [&](Node* at, string_view name, Type* t) {
                bound++;
                if (index > 0) {
                    bool known = false;
                    for (string_view f : first_names) {
                        known = known || f == name;
                    }
                    if (!known) {
                        fail_n(at, "lucb.check.match", "every pattern of an arm binds the same names");
                    }
                    Binding* existing = lookup(name);
                    if (existing != nullptr && !type_eq(existing->type, t)) {
                        fail_n(at, "lucb.check.type", "shared bindings must have the same type");
                    }
                    return;
                }
                first_names.push_back(name);
                bind(name, t, false, at);
            };
            if (pat->text == "_") {
                if (!guarded) {
                    saw_rest = true;
                }
            } else if (pat->text == "none") {
                if (saw_none && !guarded) {
                    fail_n(pat, "lucb.check.match", "duplicate pattern");
                }
                if (!guarded) {
                    saw_none = true;
                }
            } else if (pat->text == "some") {
                if (saw_some && !guarded) {
                    fail_n(pat, "lucb.check.match", "duplicate pattern");
                }
                if (!guarded) {
                    saw_some = true;
                }
                if (pat->body != nullptr && !pat->body->text.empty() && is_opt(scrut)) {
                    bind_name(pat, pat->body->text, scrut->elem);
                }
            } else if (is_enum(scrut) && scrut->decl != nullptr && !pat->text.empty() &&
                       pat->text != "_" && pat->left == nullptr) {
                Node* cse = enum_case(scrut->decl, pat->text);
                if (cse == nullptr) {
                    fail_n(pat, "lucb.check.name", "no case `" + string(pat->text) + "`");
                } else {
                    bool seen = false;
                    for (string_view c : saw_cases) {
                        seen = seen || c == pat->text;
                    }
                    if (seen && !guarded) {
                        fail_n(pat, "lucb.check.match", "duplicate pattern");
                    }
                    if (!guarded) {
                        saw_cases.push_back(pat->text);
                    }
                    pat->resolved = cse;
                    Node* p = cse->body;
                    Node* b = pat->body;
                    while (p != nullptr && b != nullptr) {
                        if (b->text != "_") {
                            bind_name(b, b->text, p->ty);
                        }
                        p = p->next;
                        b = b->next;
                    }
                    if (p != nullptr || b != nullptr) {
                        fail_n(pat, "lucb.check.call", "wrong number of payload fields");
                    }
                }
            } else if (pat->left != nullptr) {
                if (pat->left->kind == NodeKind::Literal) {
                    if (pat->left->op == TokenKind::KwTrue) {
                        saw_true = true;
                    }
                    if (pat->left->op == TokenKind::KwFalse) {
                        saw_false = true;
                    }
                    if (pat->right == nullptr && !guarded) {
                        string spelled(pat->left->text);
                        for (const string& seen : saw_literals) {
                            if (seen == spelled) {
                                fail_n(pat, "lucb.check.match", "duplicate pattern");
                            }
                        }
                        saw_literals.push_back(spelled);
                    }
                }
                check_expr(pat->left, scrut);
                if (pat->right != nullptr) {
                    check_expr(pat->right, scrut);
                }
            }
            if (index > 0 && bound != first_names.size()) {
                fail_n(pat, "lucb.check.match", "every pattern of an arm binds the same names");
            }
        }
        if (arm->type != nullptr) {
            Type* g = check_expr(arm->type, t_bool());
            if (!type_eq(g, t_bool())) {
                fail_n(arm, "lucb.check.type", "a match guard must be `bool`");
            }
        }
        if (n->kind == NodeKind::MatchExpr) {
            Type* want = result;
            if (want != nullptr && want->kind == TypeKind::UntypedInt) {
                want = t_i64();
            }
            if (want == nullptr) {
                want = expected;
            }
            Type* bt = check_expr(arm->body, want);
            if (result == nullptr) {
                result = bt;
                if (result != nullptr && result->kind == TypeKind::UntypedInt) {
                    result = t_i64();
                    coerce(arm->body, bt, result);
                    arm->body->ty = result;
                }
            } else if (!type_eq(bt, result) && !can_widen(bt, result) && !type_eq(bt, t_never())) {
                fail_n(arm, "lucb.check.type", "match arms must have the same type");
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
                fail_n(n, "lucb.check.match", "`match` is missing case `" + string(c->text) + "`");
                break;
            }
        }
    }
    return result != nullptr ? result : t_unit();
}

auto Checker::check_tuple_expr(Node* n, Type* expected) -> Type* {
    int nfields = 0;
    for (Node* e = n->body; e != nullptr; e = e->next) {
        nfields++;
    }
    if (nfields < 2 || nfields > 8) {
        fail_n(n, "lucb.check.type", "a tuple needs between 2 and 8 elements");
        return t_error();
    }
    Type* elems[8];
    Node* e = n->body;
    for (int i = 0; i < nfields; i++) {
        Type* want = nullptr;
        if (is_tup(expected) && i < expected->ntargs) {
            want = expected->args[i];
        }
        elems[i] = check_expr(e, want);
        if (elems[i] != nullptr && elems[i]->kind == TypeKind::UntypedInt && want == nullptr) {
            elems[i] = coerce(e, elems[i], t_i64());
            e->ty = elems[i];
        }
        e = e->next;
    }
    return intern_tup(elems, nfields);
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
        if (n->resolved != nullptr && (n->resolved->flags & FlagConst) != 0) {
            if (current_fn == nullptr || current_fn->text != "init") {
                return false;
            }
        }
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

auto Checker::place_is_local(Node* n) -> bool {
    if (n == nullptr) {
        return false;
    }
    if (n->kind == NodeKind::Name) {
        Binding* b = lookup(n->text);
        if (b == nullptr || b->decl == nullptr) {
            return false;
        }
        if (b->decl->kind == NodeKind::Global || b->decl->kind == NodeKind::Const) {
            return false;
        }
        return b->depth > 0;
    }
    if (n->kind == NodeKind::Self) {
        return false;
    }
    if (n->kind == NodeKind::Member) {
        Type* ot = n->left != nullptr ? n->left->ty : nullptr;
        if (is_ptr(ot)) {
            return is_local(n->left);
        }
        return place_is_local(n->left);
    }
    if (n->kind == NodeKind::Index) {
        return place_is_local(n->left) || is_local(n->left);
    }
    if (n->kind == NodeKind::Unary && n->op == TokenKind::Star) {
        return is_local(n->left);
    }
    return is_local(n);
}

} // namespace lucb
