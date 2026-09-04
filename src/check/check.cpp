#include "check/check.h"

#include "check/type.h"
#include "support/literal.h"

namespace lucb {
namespace {

const uint32_t k_type_flags_unsupported =
    FlagStar | FlagSpan | FlagArray | FlagOptional | FlagFallible | FlagAtomic | FlagFuncType |
    FlagVoid | FlagTupleType | FlagConst | FlagVolatile;

struct Binding {
    string_view name;
    Type* type = nullptr;
    bool mut = false;
    Node* decl = nullptr;
    int depth = 0;
};

struct Checker {
    Arena* arena = nullptr;
    DiagnosticBag* diag = nullptr;
    string path;
    Type* ty_error = nullptr;
    Type* ty_never = nullptr;
    Type* ty_unit = nullptr;
    Type* ty_bool = nullptr;
    Type* ty_i8 = nullptr;
    Type* ty_i16 = nullptr;
    Type* ty_i32 = nullptr;
    Type* ty_i64 = nullptr;
    Type* ty_isize = nullptr;
    Type* ty_u8 = nullptr;
    Type* ty_u16 = nullptr;
    Type* ty_u32 = nullptr;
    Type* ty_u64 = nullptr;
    Type* ty_usize = nullptr;
    Type* ty_f32 = nullptr;
    Type* ty_f64 = nullptr;
    Type* ty_char = nullptr;
    Type* ty_str = nullptr;
    Type* ty_untyped = nullptr;
    vector<Binding> scope;
    int depth = 0;
    Node* current_fn = nullptr;
    Node* current_struct = nullptr;
    Type* return_type = nullptr;

    Type* t_error() { return ty_error; }
    Type* t_never() { return ty_never; }
    Type* t_unit() { return ty_unit; }
    Type* t_bool() { return ty_bool; }
    Type* t_i64() { return ty_i64; }
    Type* t_str() { return ty_str; }
    Type* t_usize() { return ty_usize; }
    Type* t_untyped() { return ty_untyped; }

    Type* named_scalar(string_view name) {
        if (name == "i8") {
            return ty_i8;
        }
        if (name == "i16") {
            return ty_i16;
        }
        if (name == "i32") {
            return ty_i32;
        }
        if (name == "i64") {
            return ty_i64;
        }
        if (name == "isize") {
            return ty_isize;
        }
        if (name == "u8") {
            return ty_u8;
        }
        if (name == "u16") {
            return ty_u16;
        }
        if (name == "u32") {
            return ty_u32;
        }
        if (name == "u64") {
            return ty_u64;
        }
        if (name == "usize") {
            return ty_usize;
        }
        if (name == "f32") {
            return ty_f32;
        }
        if (name == "f64") {
            return ty_f64;
        }
        if (name == "char") {
            return ty_char;
        }
        if (name == "bool") {
            return ty_bool;
        }
        if (name == "unit") {
            return ty_unit;
        }
        if (name == "never") {
            return ty_never;
        }
        if (name == "str") {
            return ty_str;
        }
        return nullptr;
    }

    Type* make_type(TypeKind kind, string_view name) {
        Type* t = arena->make<Type>();
        t->kind = kind;
        t->name = name;
        return t;
    }

    void fail(Span span, const char* code, const string& message) {
        diag->add(code, path, span, message);
    }

    void fail_n(Node* n, const char* code, const string& message) {
        fail(n != nullptr ? n->span : Span{}, code, message);
    }

    void push_scope() { depth++; }

    void pop_scope() {
        while (!scope.empty() && scope.back().depth == depth) {
            scope.pop_back();
        }
        depth--;
    }

    Binding* lookup(string_view name) {
        for (int i = static_cast<int>(scope.size()) - 1; i >= 0; i--) {
            if (scope[static_cast<size_t>(i)].name == name) {
                return &scope[static_cast<size_t>(i)];
            }
        }
        return nullptr;
    }

    bool bind(string_view name, Type* type, bool mut, Node* decl) {
        if (lookup(name) != nullptr) {
            fail_n(decl, "lucb.check.shadow", "this name is already in scope");
            return false;
        }
        Binding b;
        b.name = name;
        b.type = type;
        b.mut = mut;
        b.decl = decl;
        b.depth = depth;
        scope.push_back(b);
        return true;
    }

    bool is_core_name(string_view name) {
        return name == "print" || name == "assert" || name == "discard" || name == "error" ||
               name == "trap" || name == "hash" || name == "format" || name == "location" ||
               name == "sizeof" || name == "alignof" || name == "offsetof" || name == "hex" ||
               named_scalar(name) != nullptr || name == "f16" || name == "cstr" || name == "fmt";
    }

    Type* resolve_type(Node* n) {
        if (n == nullptr) {
            return t_unit();
        }
        if (n->ty != nullptr) {
            return n->ty;
        }
        if ((n->flags & k_type_flags_unsupported) != 0) {
            fail_n(n, "lucb.check.unsupported", "this type is not in the scalar core yet");
            n->ty = t_error();
            return n->ty;
        }
        if (n->left != nullptr && n->text.empty() && n->flags == 0) {
            n->ty = resolve_type(n->left);
            return n->ty;
        }
        if (n->text == "f16") {
            fail_n(n, "lucb.check.unsupported", "`f16` is not in this slice");
            n->ty = t_error();
            return n->ty;
        }
        Type* named = named_scalar(n->text);
        if (named != nullptr) {
            n->ty = named;
            return n->ty;
        }
        Binding* b = lookup(n->text);
        if (b != nullptr && b->type != nullptr && b->type->kind == TypeKind::Struct) {
            n->ty = b->type;
            n->resolved = b->decl;
            return n->ty;
        }
        fail_n(n, "lucb.check.type", "unknown type `" + string(n->text) + "`");
        n->ty = t_error();
        return n->ty;
    }

    Node* struct_member(Node* st, string_view name, NodeKind kind) {
        if (st == nullptr) {
            return nullptr;
        }
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == kind && m->text == name) {
                return m;
            }
        }
        return nullptr;
    }

    Type* check_expr(Node* n, Type* expected = nullptr) {
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
        case NodeKind::Group:
            t = check_expr(n->left, expected);
            break;
        case NodeKind::Unit:
            t = t_unit();
            break;
        case NodeKind::Cast:
            t = check_cast(n, false);
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

    bool int_fits(uint64_t mag, bool neg, Type* dest) {
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

    Type* coerce(Node* n, Type* got, Type* expected) {
        if (got == nullptr) {
            return t_error();
        }
        if (got->kind == TypeKind::UntypedInt) {
            if (expected == nullptr) {
                return t_untyped();
            }
            Type* dest = is_int(expected) ? expected : t_i64();
            ParsedInt p = parse_int_literal(n->text);
            if (!p.ok) {
                fail_n(n, "lucb.check.number", "invalid integer literal");
                return t_error();
            }
            if (is_int(expected) && !int_fits(p.value, false, dest)) {
                fail_n(n, "lucb.check.number",
                       "integer literal does not fit in `" + type_name(dest) + "`");
                return t_error();
            }
            if (!is_int(expected)) {
                fail_n(n, "lucb.check.type",
                       "expected `" + type_name(expected) + "`, got an integer literal");
                return t_error();
            }
            return dest;
        }
        if (expected == nullptr) {
            return got;
        }
        if (type_eq(got, expected) || can_widen(got, expected)) {
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

    Type* unify_int(Type* a, Type* b) {
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

    Type* check_literal(Node* n, Type* expected) {
        if (n->op == TokenKind::KwTrue || n->op == TokenKind::KwFalse) {
            return t_bool();
        }
        if (n->op == TokenKind::StringLit) {
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

    Type* check_name(Node* n) {
        Binding* b = lookup(n->text);
        if (b == nullptr) {
            fail_n(n, "lucb.check.name", "unknown name `" + string(n->text) + "`");
            return t_error();
        }
        n->resolved = b->decl;
        return b->type;
    }

    Type* check_self(Node* n) {
        Binding* b = lookup("self");
        if (b == nullptr) {
            fail_n(n, "lucb.check.self", "`self` is only valid in a method");
            return t_error();
        }
        n->resolved = b->decl;
        return b->type;
    }

    Type* check_unary(Node* n, Type* expected) {
        if (n->op == TokenKind::KwNot) {
            Type* inner = check_expr(n->left, t_bool());
            if (!type_eq(inner, t_bool())) {
                fail_n(n, "lucb.check.type", "`not` requires `bool`");
            }
            return t_bool();
        }
        if (n->op == TokenKind::Tilde) {
            Type* inner = check_expr(n->left, expected);
            if (!is_int(inner)) {
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
        if (n->op == TokenKind::Star || n->op == TokenKind::Amp) {
            fail_n(n, "lucb.check.unsupported", "pointers are not in this slice");
            return t_error();
        }
        fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
        return t_error();
    }

    bool is_arith(TokenKind op) {
        return op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star ||
               op == TokenKind::SlashSlash || op == TokenKind::Percent ||
               op == TokenKind::PlusPercent || op == TokenKind::MinusPercent ||
               op == TokenKind::StarPercent || op == TokenKind::PlusPipe ||
               op == TokenKind::MinusPipe || op == TokenKind::StarPipe ||
               op == TokenKind::Slash;
    }

    bool is_bit(TokenKind op) {
        return op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret ||
               op == TokenKind::LtLt || op == TokenKind::GtGt;
    }

    Type* check_binary(Node* n, Type* expected) {
        TokenKind op = n->op;
        if (op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
            op == TokenKind::StarQuestion) {
            fail_n(n, "lucb.check.unsupported", "`+?` needs optionals, which are not in this slice");
            return t_error();
        }
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
            return u;
        }
        fail_n(n, "lucb.check.unsupported", "this operator is not in the scalar core yet");
        return t_error();
    }

    int count_args(Node* args) {
        int n = 0;
        for (Node* a = args; a != nullptr; a = a->next) {
            n++;
        }
        return n;
    }

    Type* check_call(Node* n, Type* expected) {
        (void)expected;
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
            return check_print(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
            return check_trap(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "sizeof") {
            return check_sizeof(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "alignof") {
            return check_alignof(n);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "offsetof") {
            fail_n(n, "lucb.check.unsupported", "`offsetof` is not in this slice");
            return t_error();
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
            callee->resolved = b->decl;
            n->resolved = b->decl;
            if (b->decl != nullptr && b->decl->kind == NodeKind::Struct) {
                return check_ctor(n, b->decl);
            }
            if (b->decl != nullptr && b->decl->kind == NodeKind::Func) {
                return check_func_call(n, b->decl, nullptr);
            }
            fail_n(n, "lucb.check.type", "`" + string(callee->text) + "` is not callable");
            return t_error();
        }
        fail_n(n, "lucb.check.unsupported", "this call is not in the scalar core yet");
        return t_error();
    }

    Type* type_from_expr_or_name(Node* a) {
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
            if (b != nullptr && b->decl != nullptr && b->decl->kind == NodeKind::Struct) {
                a->ty = b->type;
                a->resolved = b->decl;
                return b->type;
            }
        }
        return check_expr(a, nullptr);
    }

    Type* check_sizeof(Node* n) {
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

    Type* check_alignof(Node* n) {
        if (count_args(n->body) != 1) {
            fail_n(n, "lucb.check.call", "`alignof` takes one type");
            return t_usize();
        }
        Type* t = type_from_expr_or_name(n->body->left);
        n->body->left->ty = t;
        return t_usize();
    }

    Type* check_checked_conv(Node* n, Type* dest) {
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

    Type* check_cast(Node* n, bool checked) {
        Type* dest = resolve_type(n->type);
        Type* src = check_expr(n->left, nullptr);
        if (!convert_ok(n, src, dest, checked)) {
            return t_error();
        }
        return dest;
    }

    bool convert_ok(Node* n, Type* src, Type* dest, bool checked) {
        if (src == nullptr || dest == nullptr) {
            return false;
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
        fail_n(n, "lucb.check.type",
               "cannot convert `" + type_name(src) + "` to `" + type_name(dest) + "`");
        return false;
    }

    Type* check_print(Node* n) {
        n->resolved = nullptr;
        int count = count_args(n->body);
        if (count != 1) {
            fail_n(n, "lucb.check.call", "`print` takes one argument");
            return t_unit();
        }
        Type* a = check_expr(n->body->left, nullptr);
        if (a != nullptr && a->kind == TypeKind::UntypedInt) {
            a = coerce(n->body->left, a, t_i64());
            n->body->left->ty = a;
        }
        if (!is_int(a) && !is_float(a) && a->kind != TypeKind::Bool && a->kind != TypeKind::Str &&
            a->kind != TypeKind::Char) {
            fail_n(n, "lucb.check.type", "`print` takes a scalar or `str`");
        }
        return t_unit();
    }

    Type* check_trap(Node* n) {
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

    Type* check_ctor(Node* n, Node* st) {
        Type* ty = st->ty;
        // Mark provided fields.
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
        return ty;
    }

    Type* check_func_call(Node* n, Node* fn, Node* recv) {
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
            if (!type_eq(at, p->ty) && !can_widen(at, p->ty)) {
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
        return result;
    }

    Type* check_method_call(Node* n) {
        Node* mem = n->left;
        Node* obj = mem->left;
        // Static: Point.origin() — obj is a type name.
        if (obj != nullptr && obj->kind == NodeKind::Name) {
            Binding* b = lookup(obj->text);
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
                return check_func_call(n, method, nullptr);
            }
        }
        Type* ot = check_expr(obj);
        if (ot == nullptr || ot->kind != TypeKind::Struct || ot->decl == nullptr) {
            fail_n(n, "lucb.check.type", "methods are called on structs");
            return t_error();
        }
        Node* method = struct_member(ot->decl, mem->text, NodeKind::Func);
        if (method == nullptr) {
            fail_n(n, "lucb.check.name", "no method `" + string(mem->text) + "`");
            return t_error();
        }
        if ((method->flags & FlagStatic) != 0) {
            fail_n(n, "lucb.check.call", "a static method is called on the type");
        }
        if ((method->flags & FlagMutating) != 0 && !is_mut_place(obj)) {
            fail_n(n, "lucb.check.mut", "a mutating method needs a `var` receiver");
        }
        mem->resolved = method;
        return check_func_call(n, method, obj);
    }

    Type* check_member(Node* n, bool as_call) {
        (void)as_call;
        Type* ot = check_expr(n->left);
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
        return field->ty;
    }

    bool is_mut_place(Node* n) {
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
            return is_mut_place(n->left);
        }
        return false;
    }

    void check_stmt(Node* n) {
        if (n == nullptr) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            push_scope();
            for (Node* s = n->body; s != nullptr; s = s->next) {
                check_stmt(s);
            }
            pop_scope();
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            Type* t = nullptr;
            if (n->type != nullptr) {
                t = resolve_type(n->type);
            }
            if (n->left != nullptr) {
                Type* init = check_expr(n->left, t);
                if (t == nullptr) {
                    if (init != nullptr && init->kind == TypeKind::UntypedInt) {
                        init = coerce(n->left, init, t_i64());
                        n->left->ty = init;
                    }
                    t = init;
                } else if (!type_eq(t, init) && !can_widen(init, t)) {
                    fail_n(n, "lucb.check.type", "initialiser has type " + type_name(init) +
                                                     ", expected " + type_name(t));
                }
            } else if (t == nullptr) {
                fail_n(n, "lucb.check.type", "this binding needs a type or an initialiser");
                t = t_error();
            } else if (!is_zeroable(t)) {
                fail_n(n, "lucb.check.type", "this type has no zero value; write an initialiser");
            }
            n->ty = t;
            bind(n->text, t, n->kind == NodeKind::Var, n);
            break;
        }
        case NodeKind::Assign: {
            Type* lt = check_expr(n->left, nullptr);
            if (!is_mut_place(n->left)) {
                fail_n(n, "lucb.check.mut", "this place is not assignable");
            }
            if (n->op == TokenKind::Eq) {
                Type* rt = check_expr(n->right, lt);
                if (!type_eq(lt, rt) && !can_widen(rt, lt)) {
                    fail_n(n, "lucb.check.type", "assignment type mismatch");
                }
            } else {
                Type* rt = check_expr(n->right, lt);
                if (!is_int(lt) || (!is_int(rt) && rt->kind != TypeKind::UntypedInt)) {
                    if (!(is_float(lt) && is_float(rt))) {
                        fail_n(n, "lucb.check.type", "compound assignment requires a number");
                    }
                }
            }
            break;
        }
        case NodeKind::If: {
            Type* c = check_expr(n->left, t_bool());
            if (n->left != nullptr && n->left->kind == NodeKind::Let) {
                fail_n(n, "lucb.check.unsupported", "`if let` is not in the scalar core yet");
            } else if (!type_eq(c, t_bool())) {
                fail_n(n, "lucb.check.type", "a condition must be `bool`");
            }
            check_stmt(n->body);
            if (n->right != nullptr) {
                check_stmt(n->right);
            }
            break;
        }
        case NodeKind::While: {
            Type* c = check_expr(n->left, t_bool());
            if (!type_eq(c, t_bool())) {
                fail_n(n, "lucb.check.type", "a condition must be `bool`");
            }
            check_stmt(n->body);
            break;
        }
        case NodeKind::Return: {
            Type* t = t_unit();
            if (n->left != nullptr) {
                t = check_expr(n->left, return_type);
            }
            if (return_type != nullptr && !type_eq(t, return_type) && !can_widen(t, return_type)) {
                fail_n(n, "lucb.check.type", "return type is " + type_name(t) + ", expected " +
                                                 type_name(return_type));
            }
            break;
        }
        case NodeKind::ExprStmt:
            check_expr(n->left);
            break;
        default:
            fail_n(n, "lucb.check.unsupported", "this statement is not in the scalar core yet");
            break;
        }
    }

    bool always_returns(Node* n) {
        if (n == nullptr) {
            return false;
        }
        switch (n->kind) {
        case NodeKind::Return:
            return true;
        case NodeKind::Block: {
            bool r = false;
            for (Node* s = n->body; s != nullptr; s = s->next) {
                r = always_returns(s);
            }
            return r;
        }
        case NodeKind::If:
            return always_returns(n->body) && always_returns(n->right);
        default:
            return false;
        }
    }

    void check_params(Node* fn) {
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (p->text == "self") {
                fail_n(p, "lucb.check.self",
                       "do not write `self` as a parameter; methods take it implicitly");
            }
            p->ty = resolve_type(p->type);
            bind(p->text, p->ty, false, p);
        }
    }

    void check_func(Node* fn, Node* owner) {
        if (fn->left != nullptr) {
            fail_n(fn, "lucb.check.unsupported", "generics are not in the scalar core yet");
        }
        Type* result = t_unit();
        if (fn->type != nullptr) {
            result = resolve_type(fn->type);
        }
        fn->ty = result;
        Node* saved_fn = current_fn;
        Node* saved_st = current_struct;
        Type* saved_ret = return_type;
        current_fn = fn;
        current_struct = owner;
        return_type = result;
        push_scope();
        if (owner != nullptr && (fn->flags & FlagStatic) == 0) {
            bool mut = (fn->flags & FlagMutating) != 0;
            bind("self", owner->ty, mut, owner);
        }
        check_params(fn);
        check_stmt(fn->body);
        if (!type_eq(result, t_unit()) && !always_returns(fn->body)) {
            fail_n(fn, "lucb.check.return", "this function must return a value on every path");
        }
        pop_scope();
        current_fn = saved_fn;
        current_struct = saved_st;
        return_type = saved_ret;
    }

    void check_struct(Node* st) {
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                if (struct_member(st, m->text, NodeKind::Func) != nullptr) {
                    fail_n(m, "lucb.check.shadow", "a method already uses this name");
                }
                for (Node* o = st->body; o != m; o = o->next) {
                    if (o->kind == NodeKind::Field && o->text == m->text) {
                        fail_n(m, "lucb.check.shadow", "duplicate field");
                    }
                }
            }
        }
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Func) {
                check_func(m, st);
            } else if (m->kind != NodeKind::Field) {
                fail_n(m, "lucb.check.unsupported", "this member is not in the scalar core yet");
            }
        }
    }

    void collect_module(Node* mod) {
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                if (lookup(d->text) != nullptr) {
                    fail_n(d, "lucb.check.shadow", "this name is already in scope");
                    continue;
                }
                Type* t = make_type(TypeKind::Struct, d->text);
                t->decl = d;
                d->ty = t;
                bind(d->text, t, false, d);
            } else if (d->kind == NodeKind::Func) {
                if (is_core_name(d->text)) {
                    fail_n(d, "lucb.check.shadow", "this name belongs to the language");
                }
                if (lookup(d->text) != nullptr) {
                    fail_n(d, "lucb.check.shadow", "this name is already in scope");
                    continue;
                }
                Type* result = t_unit();
                if (d->type != nullptr) {
                    // result types that name structs need structs already bound;
                    // resolve in a second pass. Bind as a func with placeholder.
                }
                bind(d->text, result, false, d);
            } else {
                fail_n(d, "lucb.check.unsupported",
                       "this declaration is not in the scalar core yet");
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Field) {
                        m->ty = resolve_type(m->type);
                    }
                }
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                resolve_sig(d);
                Binding* b = lookup(d->text);
                if (b != nullptr && b->decl == d) {
                    b->type = d->ty;
                }
            } else if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        resolve_sig(m);
                    }
                }
            }
        }
    }

    void resolve_sig(Node* fn) {
        Type* result = t_unit();
        if (fn->type != nullptr) {
            result = resolve_type(fn->type);
        }
        fn->ty = result;
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            p->ty = resolve_type(p->type);
        }
    }

    void check_module(Node* mod) {
        push_scope();
        collect_module(mod);
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct) {
                check_struct(d);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func) {
                check_func(d, nullptr);
            }
        }
        pop_scope();
    }
};

} // namespace

bool check_module(Node* module, Arena& arena, DiagnosticBag& diagnostics, string_view path) {
    if (module == nullptr) {
        return false;
    }
    Checker c;
    c.arena = &arena;
    c.diag = &diagnostics;
    c.path = string(path);
    c.ty_error = c.make_type(TypeKind::Error, "");
    c.ty_never = c.make_type(TypeKind::Never, "never");
    c.ty_unit = c.make_type(TypeKind::Unit, "unit");
    c.ty_bool = c.make_type(TypeKind::Bool, "bool");
    c.ty_i8 = c.make_type(TypeKind::I8, "i8");
    c.ty_i16 = c.make_type(TypeKind::I16, "i16");
    c.ty_i32 = c.make_type(TypeKind::I32, "i32");
    c.ty_i64 = c.make_type(TypeKind::I64, "i64");
    c.ty_isize = c.make_type(TypeKind::Isize, "isize");
    c.ty_u8 = c.make_type(TypeKind::U8, "u8");
    c.ty_u16 = c.make_type(TypeKind::U16, "u16");
    c.ty_u32 = c.make_type(TypeKind::U32, "u32");
    c.ty_u64 = c.make_type(TypeKind::U64, "u64");
    c.ty_usize = c.make_type(TypeKind::Usize, "usize");
    c.ty_f32 = c.make_type(TypeKind::F32, "f32");
    c.ty_f64 = c.make_type(TypeKind::F64, "f64");
    c.ty_char = c.make_type(TypeKind::Char, "char");
    c.ty_str = c.make_type(TypeKind::Str, "str");
    c.ty_untyped = c.make_type(TypeKind::UntypedInt, "<integer>");
    c.check_module(module);
    return diagnostics.empty();
}

} // namespace lucb
