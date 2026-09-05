#include "check/check.h"

#include "check/type.h"
#include "support/literal.h"

#include <cstring>

namespace lucb {
namespace {

const uint32_t k_type_flags_unsupported = FlagAtomic | FlagFuncType | FlagTupleType;

struct Binding {
    string_view name;
    Type* type = nullptr;
    bool mut = false;
    bool from_local = false;
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
    Type* ty_void = nullptr;
    Type* ty_err = nullptr;
    vector<Type*> interned;
    vector<Binding> scope;
    int depth = 0;
    Node* current_fn = nullptr;
    Node* current_struct = nullptr;
    Type* return_type = nullptr;
    bool fallible_fn = false;
    bool in_catch = false;
    Type* catch_type = nullptr;
    vector<string_view> loop_labels;

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

    string_view keep(const string& s) {
        char* p = static_cast<char*>(arena->alloc(s.size() + 1, 1));
        memcpy(p, s.data(), s.size());
        p[s.size()] = 0;
        return {p, s.size()};
    }

    Type* intern_ptr(Type* elem, bool is_const, bool is_vol, bool nullable) {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Pointer && t->elem == elem && t->is_const == is_const &&
                t->is_volatile == is_vol && t->is_nullable == nullable) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Pointer, {});
        t->elem = elem;
        t->is_const = is_const;
        t->is_volatile = is_vol;
        t->is_nullable = nullable;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

    Type* intern_arr(Type* elem, uint64_t n) {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Array && t->elem == elem && t->length == n) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Array, {});
        t->elem = elem;
        t->length = n;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

    Type* intern_opt(Type* elem) {
        if (is_ptr(elem) && !elem->is_nullable) {
            return intern_ptr(elem->elem, elem->is_const, elem->is_volatile, true);
        }
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Optional && t->elem == elem) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Optional, {});
        t->elem = elem;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

    Type* intern_fail(Type* elem) {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Fallible && t->elem == elem) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Fallible, {});
        t->elem = elem;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

    Type* intern_sp(Type* elem, bool is_const) {
        for (size_t i = 0; i < interned.size(); i++) {
            Type* t = interned[i];
            if (t->kind == TypeKind::Span && t->elem == elem && t->is_const == is_const) {
                return t;
            }
        }
        Type* t = make_type(TypeKind::Span, {});
        t->elem = elem;
        t->is_const = is_const;
        t->name = keep(type_name(t));
        interned.push_back(t);
        return t;
    }

    void mark_local(Node* n) {
        if (n != nullptr) {
            n->flags |= FlagLocal;
        }
    }

    bool is_local(Node* n) {
        return n != nullptr && (n->flags & FlagLocal) != 0;
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

    void set_from_local(string_view name, bool from_local) {
        Binding* b = lookup(name);
        if (b != nullptr) {
            b->from_local = from_local;
        }
    }

    bool is_core_name(string_view name) {
        return name == "print" || name == "assert" || name == "discard" || name == "error" ||
               name == "trap" || name == "hash" || name == "format" || name == "location" ||
               name == "sizeof" || name == "alignof" || name == "offsetof" || name == "hex" ||
               named_scalar(name) != nullptr || name == "f16" || name == "cstr" || name == "fmt";
    }

    bool const_u64(Node* n, uint64_t* out) {
        if (n == nullptr || out == nullptr) {
            return false;
        }
        if (n->kind == NodeKind::Literal && n->op == TokenKind::IntLit) {
            ParsedInt p = parse_int_literal(n->text);
            if (!p.ok) {
                return false;
            }
            *out = p.value;
            return true;
        }
        if (n->kind == NodeKind::Group) {
            return const_u64(n->left, out);
        }
        if (n->kind == NodeKind::Call && n->left != nullptr && n->left->kind == NodeKind::Name &&
            n->left->text == "sizeof") {
            if (n->body == nullptr || n->body->left == nullptr) {
                return false;
            }
            Type* t = type_from_expr_or_name(n->body->left);
            *out = static_cast<uint64_t>(type_size(t));
            return true;
        }
        return false;
    }

    Type* resolve_type(Node* n) {
        if (n == nullptr) {
            return t_unit();
        }
        if (n->ty != nullptr) {
            return n->ty;
        }
        if ((n->flags & k_type_flags_unsupported) != 0) {
            fail_n(n, "lucb.check.unsupported", "this type is not in this slice");
            n->ty = t_error();
            return n->ty;
        }
        if (n->flags & FlagFallible) {
            Type* inner = t_unit();
            if (n->left != nullptr) {
                inner = resolve_type(n->left);
            } else if (n->text == "unit" || n->text.empty()) {
                inner = t_unit();
            } else {
                inner = named_scalar(n->text);
                if (inner == nullptr) {
                    inner = t_unit();
                }
            }
            n->ty = intern_fail(inner);
            return n->ty;
        }
        if (n->flags & FlagOptional) {
            Type* inner = resolve_type(n->left);
            n->ty = intern_opt(inner);
            return n->ty;
        }
        if (n->flags & FlagStar) {
            Type* elem = resolve_type(n->left);
            if (elem->kind == TypeKind::Void ||
                (n->left != nullptr && (n->left->flags & FlagVoid))) {
                elem = ty_void;
            }
            n->ty = intern_ptr(elem, (n->flags & FlagConst) != 0, (n->flags & FlagVolatile) != 0,
                               false);
            return n->ty;
        }
        if (n->flags & FlagSpan) {
            Type* elem = resolve_type(n->left);
            n->ty = intern_sp(elem, (n->flags & FlagConst) != 0);
            return n->ty;
        }
        if (n->flags & FlagArray) {
            Type* elem = resolve_type(n->left);
            uint64_t len = 0;
            if (!const_u64(n->right, &len) || len == 0) {
                fail_n(n, "lucb.check.type", "array length must be a positive constant");
                n->ty = t_error();
                return n->ty;
            }
            n->ty = intern_arr(elem, len);
            return n->ty;
        }
        if (n->flags & FlagVoid) {
            n->ty = ty_void;
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
            Type* dest = expected;
            if (is_opt(expected)) {
                dest = expected->elem;
            }
            if (dest == nullptr || !is_int(dest)) {
                fail_n(n, "lucb.check.type",
                       "expected `" + type_name(expected) + "`, got an integer literal");
                return t_error();
            }
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
            return is_opt(expected) ? expected : dest;
        }
        if (expected == nullptr) {
            return got;
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

    bool same_pointee(const Type* a, const Type* b) {
        return a != nullptr && b != nullptr && a->elem == b->elem;
    }

    bool can_ptr_convert(Type* from, Type* to, Node* n) {
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
        if (b->from_local) {
            mark_local(n);
        }
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

    Type* as_index_type(Node* n) {
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

    Type* check_index(Node* n) {
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

    Type* check_slice(Node* n) {
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

    Type* check_array_lit(Node* n, Type* expected) {
        Type* elem = nullptr;
        uint64_t count = 0;
        if (expected != nullptr && is_array(expected)) {
            elem = expected->elem;
        }
        for (Node* e = n->body; e != nullptr; e = e->next) {
            Type* et = check_expr(e, elem);
            if (elem == nullptr) {
                elem = et;
            } else if (!type_eq(et, elem) && !can_widen(et, elem)) {
                fail_n(e, "lucb.check.type", "array elements must have one type");
            }
            count++;
        }
        if (elem == nullptr) {
            fail_n(n, "lucb.check.type", "cannot infer an empty array literal");
            return t_error();
        }
        if (expected != nullptr && is_array(expected) && expected->length != count) {
            fail_n(n, "lucb.check.type", "array literal has the wrong length");
        }
        return intern_arr(elem, count);
    }

    Type* check_span_make(Node* n) {
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

    bool is_arith(TokenKind op) {
        return op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star ||
               op == TokenKind::SlashSlash || op == TokenKind::Percent ||
               op == TokenKind::PlusPercent || op == TokenKind::MinusPercent ||
               op == TokenKind::StarPercent || op == TokenKind::PlusPipe ||
               op == TokenKind::MinusPipe || op == TokenKind::StarPipe ||
               op == TokenKind::Slash || op == TokenKind::PlusQuestion ||
               op == TokenKind::MinusQuestion || op == TokenKind::StarQuestion;
    }

    bool is_bit(TokenKind op) {
        return op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret ||
               op == TokenKind::LtLt || op == TokenKind::GtGt;
    }

    Type* check_binary(Node* n, Type* expected) {
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

    Type* check_error(Node* n) {
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

    Type* check_else(Node* n, Type* expected) {
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

    Type* check_catch(Node* n, Type* expected) {
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

    bool pattern_covers_rest(Node* pat) {
        return pat != nullptr && pat->text == "_";
    }

    Type* check_match(Node* n, Type* expected) {
        Type* scrut = check_expr(n->left, nullptr);
        Type* result = expected;
        bool saw_rest = false;
        bool saw_true = false;
        bool saw_false = false;
        bool saw_none = false;
        bool saw_some = false;
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
        return result != nullptr ? result : t_unit();
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
        Type* recv = ot;
        if (is_ptr(ot) && ot->elem != nullptr) {
            recv = ot->elem;
        }
        if (recv == nullptr || recv->kind != TypeKind::Struct || recv->decl == nullptr) {
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
        return check_func_call(n, method, obj);
    }

    Type* check_member(Node* n, bool as_call) {
        (void)as_call;
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
            if (n->left != nullptr && is_local(n->left)) {
                set_from_local(n->text, true);
            }
            break;
        }
        case NodeKind::Assign: {
            Type* lt = check_expr(n->left, nullptr);
            if (!is_mut_place(n->left)) {
                fail_n(n, "lucb.check.mut", "this place is not assignable");
            }
            if (n->op == TokenKind::Eq) {
                Type* rt = check_expr(n->right, lt);
                if (!type_eq(lt, rt) && !can_widen(rt, lt) && !can_ptr_convert(rt, lt, n->right)) {
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
            if (n->flags & FlagIfLet) {
                Node* let = n->left;
                Type* ot = check_expr(let != nullptr ? let->left : nullptr, nullptr);
                if (!is_opt(ot) && !(is_ptr(ot) && ot->is_nullable)) {
                    fail_n(n, "lucb.check.type", "`if let` needs an optional");
                }
                Type* payload = is_opt(ot) ? ot->elem
                                           : intern_ptr(ot->elem, ot->is_const, ot->is_volatile, false);
                push_scope();
                if (let != nullptr) {
                    bind(let->text, payload, false, let);
                }
                check_stmt(n->body);
                pop_scope();
            } else {
                Type* c = check_expr(n->left, t_bool());
                if (!type_eq(c, t_bool())) {
                    fail_n(n, "lucb.check.type", "a condition must be `bool`");
                }
                check_stmt(n->body);
            }
            if (n->right != nullptr) {
                check_stmt(n->right);
            }
            break;
        }
        case NodeKind::While: {
            if (n->flags & FlagIfLet) {
                Node* let = n->left;
                Type* ot = check_expr(let != nullptr ? let->left : nullptr, nullptr);
                if (!is_opt(ot) && !(is_ptr(ot) && ot->is_nullable)) {
                    fail_n(n, "lucb.check.type", "`while let` needs an optional");
                }
                Type* payload = is_opt(ot) ? ot->elem
                                           : intern_ptr(ot->elem, ot->is_const, ot->is_volatile, false);
                loop_labels.push_back(n->text);
                push_scope();
                if (let != nullptr) {
                    bind(let->text, payload, false, let);
                }
                check_stmt(n->body);
                pop_scope();
                loop_labels.pop_back();
            } else {
                Type* c = check_expr(n->left, t_bool());
                if (!type_eq(c, t_bool())) {
                    fail_n(n, "lucb.check.type", "a condition must be `bool`");
                }
                loop_labels.push_back(n->text);
                check_stmt(n->body);
                loop_labels.pop_back();
            }
            break;
        }
        case NodeKind::Return: {
            Type* t = t_unit();
            if (n->left != nullptr) {
                t = check_expr(n->left, return_type);
            }
            if (return_type != nullptr && !type_eq(t, return_type) && !can_widen(t, return_type) &&
                !can_ptr_convert(t, return_type, n->left)) {
                fail_n(n, "lucb.check.type", "return type is " + type_name(t) + ", expected " +
                                                 type_name(return_type));
            }
            if (n->left != nullptr && is_local(n->left) &&
                (is_ptr(return_type) || is_span(return_type) ||
                 (return_type != nullptr && return_type->kind == TypeKind::Str))) {
                fail_n(n, "lucb.check.escape", "this pointer or view must not escape the function");
            }
            break;
        }
        case NodeKind::For: {
            Type* it = n->right != nullptr && n->right->kind == NodeKind::Binary &&
                               (n->right->op == TokenKind::DotDotLt ||
                                n->right->op == TokenKind::DotDotEq)
                           ? nullptr
                           : check_expr(n->right, nullptr);
            Type* elem = nullptr;
            if (n->right != nullptr && n->right->kind == NodeKind::Binary &&
                (n->right->op == TokenKind::DotDotLt || n->right->op == TokenKind::DotDotEq)) {
                Type* a = check_expr(n->right->left, nullptr);
                Type* b = check_expr(n->right->right, nullptr);
                Type* u = unify_int(a, b);
                if (u == nullptr || (!is_int(u) && a->kind == TypeKind::UntypedInt &&
                                     b->kind == TypeKind::UntypedInt)) {
                    u = t_usize();
                    if (a->kind == TypeKind::UntypedInt) {
                        n->right->left->ty = coerce(n->right->left, a, u);
                    }
                    if (b->kind == TypeKind::UntypedInt) {
                        n->right->right->ty = coerce(n->right->right, b, u);
                    }
                }
                if (a->kind == TypeKind::UntypedInt) {
                    n->right->left->ty = coerce(n->right->left, a, u != nullptr && is_int(u) ? u : t_usize());
                }
                if (b->kind == TypeKind::UntypedInt) {
                    n->right->right->ty = coerce(n->right->right, b, u != nullptr && is_int(u) ? u : t_usize());
                }
                elem = (u != nullptr && is_int(u)) ? u : t_usize();
                n->right->ty = elem;
            } else if (n->flags & FlagByPtr) {
                if (is_array(it)) {
                    elem = intern_ptr(it->elem, !is_mut_place(n->right), false, false);
                } else if (is_span(it)) {
                    elem = intern_ptr(it->elem, it->is_const, false, false);
                } else {
                    fail_n(n, "lucb.check.type", "`for` over `&` needs an array or span");
                }
            } else if (is_array(it) || is_span(it)) {
                elem = it->elem;
            } else if (it != nullptr && it->kind == TypeKind::Str) {
                elem = ty_char;
            } else {
                fail_n(n, "lucb.check.type", "`for` needs an array, span, or `str`");
                elem = t_error();
            }
            if (n->type != nullptr) {
                Type* want = resolve_type(n->type);
                if (!type_eq(want, elem) && !can_widen(elem, want)) {
                    fail_n(n, "lucb.check.type", "loop variable has the wrong type");
                }
                elem = want;
            }
            n->ty = elem;
            loop_labels.push_back(n->text);
            push_scope();
            bind(n->text, elem, false, n);
            check_stmt(n->body);
            pop_scope();
            loop_labels.pop_back();
            break;
        }
        case NodeKind::Break:
        case NodeKind::Continue: {
            if (loop_labels.empty()) {
                fail_n(n, "lucb.check.type", "`break`/`continue` needs a loop");
            } else if (!n->text.empty()) {
                bool found = false;
                for (size_t i = 0; i < loop_labels.size(); i++) {
                    if (loop_labels[i] == n->text) {
                        found = true;
                    }
                }
                if (!found) {
                    fail_n(n, "lucb.check.name", "unknown loop label `" + string(n->text) + "`");
                }
            }
            break;
        }
        case NodeKind::Defer:
            check_expr(n->left);
            if (n->body != nullptr) {
                check_stmt(n->body);
            }
            break;
        case NodeKind::Errdefer:
            if (!fallible_fn) {
                fail_n(n, "lucb.check.type", "`errdefer` is only valid in a fallible function");
            }
            check_expr(n->left);
            break;
        case NodeKind::Recover:
            if (!in_catch) {
                fail_n(n, "lucb.check.type", "`recover` is only valid in a `catch` handler");
            }
            check_expr(n->left, catch_type != nullptr ? catch_type : return_type);
            break;
        case NodeKind::Match:
            check_match(n, nullptr);
            break;
        case NodeKind::ExprStmt: {
            Type* t = check_expr(n->left);
            if (is_fail(t)) {
                fail_n(n, "lucb.check.type", "handle this failure with `try` or `catch`");
            }
            break;
        }
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
        case NodeKind::Recover:
            return true;
        case NodeKind::ExprStmt:
            if (n->left != nullptr && n->left->kind == NodeKind::Call && n->left->left != nullptr &&
                n->left->left->kind == NodeKind::Name &&
                (n->left->left->text == "trap" || n->left->left->text == "error")) {
                return true;
            }
            return n->left != nullptr && n->left->ty != nullptr &&
                   n->left->ty->kind == TypeKind::Never;
        case NodeKind::Block: {
            bool r = false;
            for (Node* s = n->body; s != nullptr; s = s->next) {
                r = always_returns(s);
            }
            return r;
        }
        case NodeKind::If:
            return always_returns(n->body) && always_returns(n->right);
        case NodeKind::Match: {
            if (n->body == nullptr) {
                return false;
            }
            for (Node* arm = n->body; arm != nullptr; arm = arm->next) {
                if (!always_returns(arm->body)) {
                    return false;
                }
            }
            return true;
        }
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
        if (is_fail(result)) {
            fn->flags |= FlagFallible;
            result = result->elem != nullptr ? result->elem : t_unit();
        }
        fn->ty = result;
        Node* saved_fn = current_fn;
        Node* saved_st = current_struct;
        Type* saved_ret = return_type;
        bool saved_fail = fallible_fn;
        current_fn = fn;
        current_struct = owner;
        return_type = result;
        fallible_fn = (fn->flags & FlagFallible) != 0;
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
        fallible_fn = saved_fail;
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
        if (is_fail(result)) {
            fn->flags |= FlagFallible;
            result = result->elem != nullptr ? result->elem : t_unit();
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
    c.ty_void = c.make_type(TypeKind::Void, "void");
    c.ty_err = c.make_type(TypeKind::ErrorVal, "Error");
    c.check_module(module);
    return diagnostics.empty();
}

} // namespace lucb
