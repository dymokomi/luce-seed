//==============================================================================================
//
//   interp/ops - Operators, conversions, formatting, and hashing in the oracle
//
//   DESCRIPTION:
//       Checked, wrapping, saturating, and optional arithmetic on every width (base.md §7.2),
//       comparisons, the conversion family of §7.5 including validated text, formatted
//       strings and `format`, and the hash function the compiler supplies for `Hashable`
//       values.
//
//==============================================================================================

#include "interp/interp_impl.h"

#include "support/literal.h"
#include <cmath>
#include <cstring>

namespace lucb {

static bool utf8_ok(const char* s, size_t n) {
    size_t i = 0;
    if (s == nullptr) {
        return n == 0;
    }
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t w = 1;
        uint32_t cp = 0;
        if (c < 0x80) {
            w = 1;
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            w = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            w = 3;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            w = 4;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + w > n) {
            return false;
        }
        for (size_t k = 1; k < w; k++) {
            unsigned char x = static_cast<unsigned char>(s[i + k]);
            if ((x & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | static_cast<uint32_t>(x & 0x3Fu);
        }
        if (w == 2 && cp < 0x80) {
            return false;
        }
        if (w == 3 && cp < 0x800) {
            return false;
        }
        if (w == 4 && cp < 0x10000) {
            return false;
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            return false;
        }
        if (cp > 0x10FFFFu) {
            return false;
        }
        i += w;
    }
    return true;
}

static uint64_t mix64(uint64_t h, uint64_t x) {
    h ^= x;
    h *= 0x9E3779B97F4A7C15ULL;
    h ^= h >> 32;
    return h;
}

auto Interp::eval_formatted(Node* n) -> Value {
    string s;
    for (Node* p = n != nullptr ? n->body : nullptr; p != nullptr; p = p->next) {
        if (p->kind == NodeKind::FormatText) {
            s += unescape_format_braces(decode_string_literal(p->text));
        } else if (p->kind == NodeKind::FormatField) {
            Value f = eval(p->left);
            if (trapped) {
                return v_unit();
            }
            if (f.kind == TypeKind::Fmt) {
                s += decode_string(f.str); // a forwarded `fmt` parameter
            } else {
                s += show(f);
            }
        }
    }
    strings.push_back(s);
    return v_str(strings.back());
}

auto Interp::eval_format(Node* n) -> Value {
    Node* bufn = n->body != nullptr ? n->body->left : nullptr;
    Node* msgn = n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr;
    Value buf = eval(bufn);
    Value msg;
    if (msgn != nullptr && msgn->kind == NodeKind::Formatted) {
        msg = eval_formatted(msgn);
    } else {
        msg = eval(msgn);
    }
    if (trapped) {
        return v_unit();
    }
    string text = msg.kind == TypeKind::Str ? decode_string(msg.str) : show(msg);
    if (text.size() > buf.length) {
        Value r;
        r.kind = TypeKind::Fallible;
        r.failed = true;
        r.err_code = 1;
        r.err_msg = "memory.exhausted";
        r.type = n->ty;
        return r;
    }
    strings.push_back(text);
    Value r;
    r.kind = TypeKind::Fallible;
    r.failed = false;
    r.type = n->ty;
    r.str = strings.back();
    r.length = strings.back().size();
    return r;
}

auto Interp::eval_unary(Node* n) -> Value {
    Value x = eval(n->left);
    if (trapped) {
        return v_unit();
    }
    if (n->op == TokenKind::KwTry) {
        if (x.failed) {
            ret = x;
            returning = true;
            return v_unit();
        }
        x.kind = n->ty != nullptr ? n->ty->kind : x.kind;
        x.type = n->ty;
        return x;
    }
    if (n->op == TokenKind::KwNot) {
        return v_bool(!x.b);
    }
    if (n->op == TokenKind::Amp) {
        Value* p = lvalue(n->left);
        Value v;
        v.kind = TypeKind::Pointer;
        v.type = n->ty;
        v.ptr = p;
        if (n->ty != nullptr && n->ty->kind == TypeKind::Interface) {
            v.kind = TypeKind::Interface;
        }
        return v;
    }
    if (n->op == TokenKind::Star) {
        if (x.ptr == nullptr) {
            fail("null pointer");
            return v_unit();
        }
        return *x.ptr;
    }
    if (n->op == TokenKind::Plus) {
        return x;
    }
    Type* t = n->ty != nullptr ? n->ty : x.type;
    if (n->op == TokenKind::Tilde) {
        Type* bits_t = is_int_enum(t) ? t->elem : t;
        uint64_t u = ~as_u(x, bits_t);
        if (is_int_enum(t)) {
            Value v;
            v.kind = TypeKind::Enum;
            v.type = t;
            v.u = u & int_mask(int_bits(bits_t));
            return v;
        }
        return v_int(t, u);
    }
    if (is_float(t)) {
        if (n->op == TokenKind::Minus) {
            return v_float(t, -x.f);
        }
    }
    int bits = int_bits(t);
    if (n->op == TokenKind::MinusPercent) {
        if (is_signed_int(t)) {
            uint64_t r = 0u - static_cast<uint64_t>(as_s(x, t));
            return v_int(t, r);
        }
        return v_int(t, 0u - as_u(x, t));
    }
    if (n->op == TokenKind::Minus) {
        if (n->left != nullptr && n->left->kind == NodeKind::Literal &&
            n->left->op == TokenKind::IntLit) {
            ParsedInt p = parse_int_literal(n->left->text);
            if (p.ok && p.value == static_cast<uint64_t>(int_max_signed(int_bits(t))) + 1) {
                return v_int(t, static_cast<uint64_t>(int_min(t)));
            }
        }
        int64_t a = as_s(x, t);
        if (a == int_min(t)) {
            fail("integer overflow");
            return v_unit();
        }
        return v_int(t, static_cast<uint64_t>(-a));
    }
    (void)bits;
    fail("unsupported unary operator");
    return v_unit();
}

auto Interp::cmp_num(const Value& L, const Value& R, Type* t, TokenKind op) -> bool {
    if (is_float(t) || L.kind == TypeKind::F32 || L.kind == TypeKind::F64) {
        if (op == TokenKind::Lt) {
            return L.f < R.f;
        }
        if (op == TokenKind::LtEq) {
            return L.f <= R.f;
        }
        if (op == TokenKind::Gt) {
            return L.f > R.f;
        }
        return L.f >= R.f;
    }
    if (is_unsigned_int(t) || is_unsigned_int(L.type)) {
        uint64_t a = as_u(L, t != nullptr ? t : L.type);
        uint64_t b = as_u(R, t != nullptr ? t : R.type);
        if (op == TokenKind::Lt) {
            return a < b;
        }
        if (op == TokenKind::LtEq) {
            return a <= b;
        }
        if (op == TokenKind::Gt) {
            return a > b;
        }
        return a >= b;
    }
    int64_t a = as_s(L, t != nullptr ? t : L.type);
    int64_t b = as_s(R, t != nullptr ? t : R.type);
    if (op == TokenKind::Lt) {
        return a < b;
    }
    if (op == TokenKind::LtEq) {
        return a <= b;
    }
    if (op == TokenKind::Gt) {
        return a > b;
    }
    return a >= b;
}

auto Interp::arith(Type* t, const Value& L, const Value& R, TokenKind op) -> Value {
    if (is_float(t)) {
        double a = L.f;
        double b = R.f;
        if (op == TokenKind::Plus) {
            return v_float(t, a + b);
        }
        if (op == TokenKind::Minus) {
            return v_float(t, a - b);
        }
        if (op == TokenKind::Star) {
            return v_float(t, a * b);
        }
        if (op == TokenKind::Slash) {
            return v_float(t, a / b);
        }
        fail("unsupported float operator");
        return v_unit();
    }
    int bits = int_bits(t);
    bool sig = is_signed_int(t);
    if (op == TokenKind::Amp) {
        return v_int(t, as_u(L, t) & as_u(R, t));
    }
    if (op == TokenKind::Pipe) {
        return v_int(t, as_u(L, t) | as_u(R, t));
    }
    if (op == TokenKind::Caret) {
        return v_int(t, as_u(L, t) ^ as_u(R, t));
    }
    if (op == TokenKind::LtLt || op == TokenKind::GtGt) {
        uint64_t n = as_u(R, R.type != nullptr ? R.type : t);
        if (n >= static_cast<uint64_t>(bits)) {
            fail("shift count out of range");
            return v_unit();
        }
        if (op == TokenKind::LtLt) {
            return v_int(t, as_u(L, t) << n);
        }
        if (sig) {
            return v_int(t, static_cast<uint64_t>(as_s(L, t) >> n));
        }
        return v_int(t, as_u(L, t) >> n);
    }
    if (sig) {
        int64_t a = as_s(L, t);
        int64_t b = as_s(R, t);
        int64_t r = 0;
        if (op == TokenKind::Plus || op == TokenKind::PlusQuestion || op == TokenKind::Minus ||
            op == TokenKind::MinusQuestion || op == TokenKind::Star ||
            op == TokenKind::StarQuestion) {
            bool ov = false;
            if (op == TokenKind::Plus || op == TokenKind::PlusQuestion) {
                ov = __builtin_add_overflow(a, b, &r);
            } else if (op == TokenKind::Minus || op == TokenKind::MinusQuestion) {
                ov = __builtin_sub_overflow(a, b, &r);
            } else {
                ov = __builtin_mul_overflow(a, b, &r);
            }
            if (ov || r < int_min(t) || r > int_max_signed(bits)) {
                fail("integer overflow");
                return v_unit();
            }
            return v_int(t, static_cast<uint64_t>(r));
        }
        if (op == TokenKind::PlusPercent) {
            return v_int(t, static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
        }
        if (op == TokenKind::MinusPercent) {
            return v_int(t, static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
        }
        if (op == TokenKind::StarPercent) {
            return v_int(t, static_cast<uint64_t>(a) * static_cast<uint64_t>(b));
        }
        if (op == TokenKind::PlusPipe) {
            if (__builtin_add_overflow(a, b, &r) || r < int_min(t) || r > int_max_signed(bits)) {
                r = a < 0 ? int_min(t) : int_max_signed(bits);
            }
            return v_int(t, static_cast<uint64_t>(r));
        }
        if (op == TokenKind::MinusPipe) {
            if (__builtin_sub_overflow(a, b, &r) || r < int_min(t) || r > int_max_signed(bits)) {
                r = a < 0 ? int_min(t) : int_max_signed(bits);
            }
            return v_int(t, static_cast<uint64_t>(r));
        }
        if (op == TokenKind::StarPipe) {
            if (__builtin_mul_overflow(a, b, &r) || r < int_min(t) || r > int_max_signed(bits)) {
                r = ((a < 0) != (b < 0)) ? int_min(t) : int_max_signed(bits);
            }
            return v_int(t, static_cast<uint64_t>(r));
        }
        if (op == TokenKind::SlashSlash || op == TokenKind::Percent) {
            if (b == 0) {
                fail("division by zero");
                return v_unit();
            }
            if (a == int_min(t) && b == -1) {
                fail("integer overflow");
                return v_unit();
            }
            r = op == TokenKind::SlashSlash ? a / b : a % b;
            return v_int(t, static_cast<uint64_t>(r));
        }
    } else {
        uint64_t a = as_u(L, t);
        uint64_t b = as_u(R, t);
        uint64_t maxv = int_max_unsigned(bits);
        if (op == TokenKind::Plus || op == TokenKind::PlusQuestion) {
            if (bits >= 64 ? a > UINT64_MAX - b : a + b > maxv) {
                fail("integer overflow");
                return v_unit();
            }
            return v_int(t, a + b);
        }
        if (op == TokenKind::Minus || op == TokenKind::MinusQuestion) {
            if (a < b) {
                fail("integer overflow");
                return v_unit();
            }
            return v_int(t, a - b);
        }
        if (op == TokenKind::Star || op == TokenKind::StarQuestion) {
            if (b != 0 && a > maxv / b) {
                fail("integer overflow");
                return v_unit();
            }
            return v_int(t, a * b);
        }
        if (op == TokenKind::PlusPercent) {
            return v_int(t, a + b);
        }
        if (op == TokenKind::MinusPercent) {
            return v_int(t, a - b);
        }
        if (op == TokenKind::StarPercent) {
            return v_int(t, a * b);
        }
        if (op == TokenKind::PlusPipe) {
            if (bits >= 64 ? a > UINT64_MAX - b : a + b > maxv) {
                return v_int(t, maxv);
            }
            return v_int(t, a + b);
        }
        if (op == TokenKind::MinusPipe) {
            return v_int(t, a < b ? 0 : a - b);
        }
        if (op == TokenKind::StarPipe) {
            if (b != 0 && a > maxv / b) {
                return v_int(t, maxv);
            }
            return v_int(t, a * b);
        }
        if (op == TokenKind::SlashSlash || op == TokenKind::Percent) {
            if (b == 0) {
                fail("division by zero");
                return v_unit();
            }
            return v_int(t, op == TokenKind::SlashSlash ? a / b : a % b);
        }
    }
    fail("unsupported binary operator");
    return v_unit();
}

auto Interp::eval_binary(Node* n) -> Value {
    if (n->op == TokenKind::KwAnd) {
        Value L = eval(n->left);
        if (trapped || !L.b) {
            return v_bool(false);
        }
        return eval(n->right);
    }
    if (n->op == TokenKind::KwOr) {
        Value L = eval(n->left);
        if (trapped || L.b) {
            return v_bool(true);
        }
        return eval(n->right);
    }
    Value L = eval(n->left);
    Value R = eval(n->right);
    if (trapped) {
        return v_unit();
    }
    TokenKind op = n->op;
    Type* t = n->ty;
    if (L.kind == TypeKind::Pointer || R.kind == TypeKind::Pointer) {
        if ((L.punned || R.punned) && op != TokenKind::EqEq && op != TokenKind::NotEq) {
            fail("pointer reinterpretation is not modelled by the interpreter; build it");
            return v_unit();
        }
        if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
            bool eq = L.ptr == R.ptr;
            return v_bool(op == TokenKind::EqEq ? eq : !eq);
        }
        if (op == TokenKind::Plus && L.kind == TypeKind::Pointer && L.ptr != nullptr) {
            L.ptr += static_cast<ptrdiff_t>(as_s(R, R.type));
            L.type = n->ty;
            return L;
        }
        if (op == TokenKind::Minus && L.kind == TypeKind::Pointer && R.kind == TypeKind::Pointer) {
            return v_int(n->ty, static_cast<uint64_t>(L.ptr - R.ptr));
        }
        if (op == TokenKind::Minus && L.kind == TypeKind::Pointer && L.ptr != nullptr) {
            L.ptr -= static_cast<ptrdiff_t>(as_s(R, R.type));
            L.type = n->ty;
            return L;
        }
        if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
            op == TokenKind::GtEq) {
            if (op == TokenKind::Lt) {
                return v_bool(L.ptr < R.ptr);
            }
            if (op == TokenKind::LtEq) {
                return v_bool(L.ptr <= R.ptr);
            }
            if (op == TokenKind::Gt) {
                return v_bool(L.ptr > R.ptr);
            }
            return v_bool(L.ptr >= R.ptr);
        }
    }
    if (L.kind == TypeKind::Enum || R.kind == TypeKind::Enum || is_int_enum(t)) {
        Type* bits_t = is_int_enum(t) ? t->elem : t;
        uint64_t a = as_u(L, bits_t != nullptr ? bits_t : L.type);
        uint64_t b = as_u(R, bits_t != nullptr ? bits_t : R.type);
        if (op == TokenKind::Amp || op == TokenKind::Pipe || op == TokenKind::Caret) {
            Value v;
            v.kind = TypeKind::Enum;
            v.type = t;
            if (op == TokenKind::Amp) {
                v.u = a & b;
            } else if (op == TokenKind::Pipe) {
                v.u = a | b;
            } else {
                v.u = a ^ b;
            }
            if (bits_t != nullptr) {
                v.u &= int_mask(int_bits(bits_t));
            }
            return v;
        }
    }
    if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
        bool eq = false;
        if (L.kind == TypeKind::Bool) {
            eq = L.b == R.b;
        } else if (L.kind == TypeKind::Str) {
            eq = show(L) == show(R);
        } else if (is_float(L.type)) {
            eq = L.f == R.f;
        } else if (L.kind == TypeKind::Enum || R.kind == TypeKind::Enum) {
            eq = L.u == R.u;
        } else {
            eq = as_u(L, L.type) == as_u(R, R.type);
        }
        return v_bool(op == TokenKind::EqEq ? eq : !eq);
    }
    if (op == TokenKind::Lt || op == TokenKind::LtEq || op == TokenKind::Gt ||
        op == TokenKind::GtEq) {
        Type* ct = L.type != nullptr ? L.type : R.type;
        return v_bool(cmp_num(L, R, ct, op));
    }
    Type* at = t != nullptr ? t : L.type;
    if (is_opt(at)) {
        at = at->elem;
    }
    Value r = arith(at, L, R, op);
    if (op == TokenKind::PlusQuestion || op == TokenKind::MinusQuestion ||
        op == TokenKind::StarQuestion) {
        if (trapped) {
            trapped = false;
            trap = {};
            Value none;
            none.kind = TypeKind::Optional;
            none.type = t;
            none.present = false;
            return none;
        }
        r.present = true;
        r.kind = TypeKind::Optional;
        r.type = t;
    }
    return r;
}

auto Interp::eval_conv(Node* srcn, Type* dest, bool checked) -> Value {
    Value x = eval(srcn);
    if (trapped || dest == nullptr) {
        return x;
    }
    Type* src = srcn != nullptr ? srcn->ty : x.type;
    if (is_float(src) && is_int(dest)) {
        double a = x.f;
        int bits = int_bits(dest);
        if (checked) {
            if (a != a || a < static_cast<double>(int_min(dest)) ||
                (is_signed_int(dest) ? a > static_cast<double>(int_max_signed(bits))
                                     : a < 0 || a > static_cast<double>(int_max_unsigned(bits)))) {
                fail("integer conversion out of range");
                return v_unit();
            }
            if (is_signed_int(dest)) {
                return v_int(dest, static_cast<uint64_t>(static_cast<int64_t>(a)));
            }
            return v_int(dest, static_cast<uint64_t>(a));
        }
        if (a != a) {
            return v_int(dest, 0);
        }
        if (is_signed_int(dest)) {
            if (a <= static_cast<double>(int_min(dest))) {
                return v_int(dest, static_cast<uint64_t>(int_min(dest)));
            }
            if (a >= static_cast<double>(int_max_signed(bits))) {
                return v_int(dest, static_cast<uint64_t>(int_max_signed(bits)));
            }
            return v_int(dest, static_cast<uint64_t>(static_cast<int64_t>(a)));
        }
        if (a < 0) {
            return v_int(dest, 0);
        }
        if (a >= static_cast<double>(int_max_unsigned(bits))) {
            return v_int(dest, int_max_unsigned(bits));
        }
        return v_int(dest, static_cast<uint64_t>(a));
    }
    if (is_int(src) && is_float(dest)) {
        double a = is_signed_int(src) ? static_cast<double>(as_s(x, src))
                                      : static_cast<double>(as_u(x, src));
        return v_float(dest, a);
    }
    if (is_float(src) && is_float(dest)) {
        return v_float(dest, x.f);
    }
    if (is_int_enum(src) && is_int(dest)) {
        return v_int(dest, as_u(x, src->elem != nullptr ? src->elem : dest));
    }
    if (src != nullptr && src->kind == TypeKind::ErrorCode && is_int(dest)) {
        return v_int(dest, x.u);
    }
    if (is_int(src) && dest != nullptr && dest->kind == TypeKind::ErrorCode) {
        return v_int(dest, as_u(x, src));
    }
    if (is_int(src) && is_int_enum(dest)) {
        uint64_t u = as_u(x, src);
        if (checked && dest->decl != nullptr) {
            bool ok = false;
            uint64_t next = 0;
            for (Node* c = dest->decl->body; c != nullptr; c = c->next) {
                if (c->kind != NodeKind::EnumCase) {
                    continue;
                }
                uint64_t v = next;
                if (c->left != nullptr && c->left->kind == NodeKind::Literal) {
                    ParsedInt p = parse_int_literal(c->left->text);
                    if (p.ok) {
                        v = p.value;
                    }
                }
                if (v == u) {
                    ok = true;
                    break;
                }
                next = v + 1;
            }
            if (!ok) {
                fail("invalid enum value");
                return v_unit();
            }
        }
        Value v;
        v.kind = TypeKind::Enum;
        v.type = dest;
        v.u = u;
        return v;
    }
    if (is_int(src) && is_int(dest)) {
        int64_t s = is_signed_int(src) ? as_s(x, src) : static_cast<int64_t>(as_u(x, src));
        int to_bits = int_bits(dest);
        if (checked) {
            if (is_signed_int(dest)) {
                if (s < int_min(dest) || s > int_max_signed(to_bits)) {
                    fail("integer conversion out of range");
                    return v_unit();
                }
                return v_int(dest, static_cast<uint64_t>(s));
            }
            if (s < 0 || static_cast<uint64_t>(s) > int_max_unsigned(to_bits)) {
                fail("integer conversion out of range");
                return v_unit();
            }
            return v_int(dest, static_cast<uint64_t>(s));
        }
        return v_int(dest, static_cast<uint64_t>(s));
    }
    if ((src != nullptr && src->kind == TypeKind::Char) && is_int(dest)) {
        if (checked && is_signed_int(dest) &&
            x.u > static_cast<uint64_t>(int_max_signed(int_bits(dest)))) {
            fail("integer conversion out of range");
            return v_unit();
        }
        if (checked && is_unsigned_int(dest) && x.u > int_max_unsigned(int_bits(dest))) {
            fail("integer conversion out of range");
            return v_unit();
        }
        return v_int(dest, x.u);
    }
    if (is_ptr(dest)) {
        if (is_ptr(src) && src->elem != nullptr && dest->elem != nullptr &&
            !type_eq(src->elem, dest->elem) && src->elem->kind != TypeKind::Unit &&
            dest->elem->kind != TypeKind::Unit && x.ptr != nullptr) {
            x.punned = true;
        }
        x.type = dest;
        x.kind = TypeKind::Pointer;
        return x;
    }
    if (dest->kind == TypeKind::Str &&
        (src != nullptr &&
         (src->kind == TypeKind::CStr || ((is_span(src) || is_array(src)) && src->elem != nullptr &&
                                          src->elem->kind == TypeKind::U8)))) {
        return eval_str_conv(x, src, dest, checked);
    }
    x.type = dest;
    x.kind = dest->kind;
    return x;
}

auto Interp::eval_str_conv(const Value& x, Type* src, Type* result_ty, bool checked) -> Value {
    string text;
    if (src != nullptr && src->kind == TypeKind::CStr) {
        text = cstr_text(x);
        if (text.empty() && !x.str.empty()) {
            text = decode_string(x.str);
        }
    } else if (x.kind == TypeKind::Str) {
        text = decode_string(x.str);
    } else {
        size_t nlen = x.length != 0 ? x.length : x.fields.size();
        const Value* p = x.ptr != nullptr ? x.ptr : x.fields.data();
        text.resize(nlen);
        for (size_t i = 0; i < nlen; i++) {
            text[i] = static_cast<char>(p != nullptr ? p[i].u : 0);
        }
    }
    if (checked && !utf8_ok(text.data(), text.size())) {
        Value e;
        e.failed = true;
        e.kind = TypeKind::Fallible;
        e.type = result_ty;
        e.err_code = 3;
        e.err_msg = "invalid_utf8";
        return e;
    }
    strings.push_back(text);
    Value s = v_str(strings.back());
    s.length = strings.back().size();
    if (checked) {
        return ok_payload(s, result_ty);
    }
    return s;
}

auto Interp::hash_value(const Value& v, Type* t) -> uint64_t {
    if (hash_seed == 0) {
        hash_seed =
            0x9E3779B97F4A7C15ULL ^ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&hash_seed));
        if (hash_seed == 0) {
            hash_seed = 0x9E3779B97F4A7C15ULL;
        }
    }
    uint64_t h = hash_seed;
    if (t == nullptr) {
        t = v.type;
    }
    if (t != nullptr && t->kind == TypeKind::Str) {
        string s = decode_string(v.str);
        for (size_t i = 0; i < s.size(); i++) {
            h = mix64(h, static_cast<unsigned char>(s[i]));
        }
        return mix64(h, s.size());
    }
    if (t != nullptr && t->kind == TypeKind::Bool) {
        return mix64(h, v.b ? 1 : 0);
    }
    if (is_float(t) || v.kind == TypeKind::F32 || v.kind == TypeKind::F64) {
        uint64_t bits = 0;
        if (t != nullptr && t->kind == TypeKind::F32) {
            float f = static_cast<float>(v.f);
            memcpy(&bits, &f, sizeof(float));
        } else {
            memcpy(&bits, &v.f, sizeof(double));
        }
        return mix64(h, bits);
    }
    if (is_ptr(t) || (t != nullptr && t->kind == TypeKind::CStr)) {
        return mix64(h, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(v.ptr)));
    }
    if (is_array(t) || v.kind == TypeKind::Array) {
        size_t nlen = v.length != 0 ? v.length : v.fields.size();
        const Value* p = v.ptr != nullptr ? v.ptr : v.fields.data();
        Type* elem = t != nullptr ? t->elem : nullptr;
        for (size_t i = 0; i < nlen; i++) {
            h = mix64(h, hash_value(p[i], elem));
        }
        return h;
    }
    if (is_opt(t) || v.kind == TypeKind::Optional) {
        if (!v.present) {
            return mix64(h, 0);
        }
        Value inner = v;
        Type* elem = t != nullptr ? t->elem : nullptr;
        if (elem != nullptr) {
            inner.kind = elem->kind;
            inner.type = elem;
        }
        return mix64(hash_value(inner, elem), 1);
    }
    if (is_tup(t) || v.kind == TypeKind::Tuple || v.kind == TypeKind::Struct) {
        for (size_t i = 0; i < v.fields.size(); i++) {
            Type* ft = nullptr;
            if (is_tup(t) && t != nullptr && static_cast<int>(i) < t->ntargs) {
                ft = t->args[static_cast<int>(i)];
            } else if (t != nullptr && t->kind == TypeKind::Struct && t->decl != nullptr) {
                int k = 0;
                for (Node* m = t->decl->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Field) {
                        if (k == static_cast<int>(i)) {
                            ft = m->ty;
                            break;
                        }
                        k++;
                    }
                }
            }
            h = mix64(h, hash_value(v.fields[i], ft));
        }
        return h;
    }
    if (t != nullptr && t->kind == TypeKind::Enum && !is_int_enum(t)) {
        return mix64(h, v.u);
    }
    return mix64(h, v.u);
}

auto Interp::eval_hash(Node* n) -> Value {
    Value a = eval(n->body != nullptr ? n->body->left : nullptr);
    if (trapped) {
        return v_unit();
    }
    Type* t = n->body != nullptr && n->body->left != nullptr ? n->body->left->ty : a.type;
    return v_int(n->ty, hash_value(a, t));
}

auto Interp::eval_hex(Node* n) -> Value {
    Value a = eval(n->body != nullptr ? n->body->left : nullptr);
    if (trapped) {
        return v_unit();
    }
    Type* t = n->body != nullptr && n->body->left != nullptr ? n->body->left->ty : a.type;
    uint64_t u = is_ptr(t) ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(a.ptr)) : a.u;
    char buf[32];
    snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(u));
    strings.push_back(buf);
    Value v = v_str(strings.back());
    v.type = n->ty;
    v.kind = TypeKind::Str;
    return v;
}

auto Interp::eval_bin(Node* n) -> Value {
    Value a = eval(n->body != nullptr ? n->body->left : nullptr);
    if (trapped) {
        return v_unit();
    }
    uint64_t u = a.u;
    string s;
    if (u == 0) {
        s = "0";
    } else {
        char rev[64];
        int m = 0;
        while (u != 0 && m < 64) {
            rev[m++] = static_cast<char>('0' + (u & 1u));
            u >>= 1;
        }
        while (m > 0) {
            s.push_back(rev[--m]);
        }
    }
    strings.push_back(s);
    Value v = v_str(strings.back());
    v.type = n->ty;
    v.kind = TypeKind::Str;
    return v;
}

auto Interp::eval_pad(Node* n) -> Value {
    Value a = eval(n->body != nullptr ? n->body->left : nullptr);
    Value w = eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr);
    if (trapped) {
        return v_unit();
    }
    string inner;
    Type* t = n->body != nullptr && n->body->left != nullptr ? n->body->left->ty : a.type;
    if (t != nullptr && t->kind == TypeKind::Fmt) {
        inner = decode_string(a.str);
    } else {
        inner = show(a);
    }
    size_t width = static_cast<size_t>(as_u(w, w.type));
    string s;
    if (inner.size() < width) {
        s.assign(width - inner.size(), ' ');
    }
    s += inner;
    strings.push_back(s);
    Value v = v_str(strings.back());
    v.type = n->ty;
    v.kind = TypeKind::Str;
    return v;
}

} // namespace lucb
