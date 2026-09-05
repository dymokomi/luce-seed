#include "interp/interp.h"

#include "check/type.h"
#include "support/literal.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>

namespace lucb {
namespace {

const int k_max_frames = 10'000;

struct Value {
    TypeKind kind = TypeKind::Unit;
    Type* type = nullptr;
    bool b = false;
    uint64_t u = 0;
    double f = 0;
    string_view str;
    vector<Value> fields;
    Value* ptr = nullptr;
    size_t length = 0;
};

Value v_unit() {
    Value v;
    return v;
}

Value v_bool(bool b) {
    Value v;
    v.kind = TypeKind::Bool;
    v.b = b;
    return v;
}

Value v_int(Type* t, uint64_t u) {
    Value v;
    v.type = t;
    v.kind = t != nullptr ? t->kind : TypeKind::I64;
    if (t != nullptr && is_int(t)) {
        v.u = u & int_mask(int_bits(t));
    } else {
        v.u = u;
    }
    return v;
}

Value v_i64(int64_t i) {
    Value v;
    v.kind = TypeKind::I64;
    v.u = static_cast<uint64_t>(i);
    return v;
}

Value v_float(Type* t, double f) {
    Value v;
    v.type = t;
    v.kind = t != nullptr ? t->kind : TypeKind::F64;
    v.f = t != nullptr && t->kind == TypeKind::F32 ? static_cast<double>(static_cast<float>(f)) : f;
    return v;
}

Value v_str(string_view s) {
    Value v;
    v.kind = TypeKind::Str;
    v.str = s;
    return v;
}

Value v_zero(Type* t) {
    Value v;
    if (t == nullptr) {
        return v;
    }
    v.type = t;
    v.kind = t->kind;
    if (t->kind == TypeKind::Struct && t->decl != nullptr) {
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                v.fields.push_back(v_zero(t == nullptr ? nullptr : m->ty));
            }
        }
    }
    if (t->kind == TypeKind::Span) {
        v.length = 0;
        v.ptr = nullptr;
    }
    if (t->kind == TypeKind::Str) {
        v.str = {};
        v.length = 0;
    }
    return v;
}

int bits_of(const Value& v, Type* t) {
    int bits = int_bits(t != nullptr ? t : v.type);
    if (bits == 0) {
        return 64;
    }
    return bits;
}

int64_t as_s(const Value& v, Type* t) {
    int bits = bits_of(v, t);
    uint64_t u = v.u & int_mask(bits);
    if (bits < 64 && (u & (uint64_t{1} << (bits - 1))) != 0) {
        return static_cast<int64_t>(u | ~int_mask(bits));
    }
    return static_cast<int64_t>(u);
}

uint64_t as_u(const Value& v, Type* t) {
    return v.u & int_mask(bits_of(v, t));
}

int field_index(Node* st, string_view name) {
    int i = 0;
    for (Node* m = st->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            if (m->text == name) {
                return i;
            }
            i++;
        }
    }
    return -1;
}

struct Slot {
    string_view name;
    Value value;
};

struct Frame {
    std::deque<Slot> slots;
};

struct Interp {
    Node* module = nullptr;
    vector<Frame> frames;
    std::deque<vector<Value>> storage;
    string output;
    bool trapped = false;
    string trap;
    bool returning = false;
    Value ret;

    void fail(const string& message) {
        trapped = true;
        trap = message;
    }

    Value make_array(Type* t, vector<Value> elems) {
        storage.push_back(std::move(elems));
        Value v;
        v.kind = TypeKind::Array;
        v.type = t;
        v.ptr = storage.back().data();
        v.length = storage.back().size();
        return v;
    }

    Value zero_of(Type* t) {
        Value v = v_zero(t);
        if (t != nullptr && t->kind == TypeKind::Array) {
            vector<Value> elems;
            elems.resize(static_cast<size_t>(t->length));
            for (size_t i = 0; i < elems.size(); i++) {
                elems[i] = zero_of(t->elem);
            }
            return make_array(t, std::move(elems));
        }
        if (t != nullptr && t->kind == TypeKind::Struct && t->decl != nullptr) {
            v.fields.clear();
            for (Node* m = t->decl->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Field) {
                    v.fields.push_back(zero_of(m->ty));
                }
            }
        }
        return v;
    }

    Slot* find_slot(string_view name) {
        if (frames.empty()) {
            return nullptr;
        }
        Frame& f = frames.back();
        for (int i = static_cast<int>(f.slots.size()) - 1; i >= 0; i--) {
            if (f.slots[static_cast<size_t>(i)].name == name) {
                return &f.slots[static_cast<size_t>(i)];
            }
        }
        return nullptr;
    }

    Value* lvalue(Node* n) {
        if (n == nullptr || trapped) {
            return nullptr;
        }
        if (n->kind == NodeKind::Name || n->kind == NodeKind::Self) {
            string_view name = n->kind == NodeKind::Self ? string_view("self") : n->text;
            Slot* s = find_slot(name);
            if (s == nullptr) {
                fail("unknown name at runtime");
                return nullptr;
            }
            return &s->value;
        }
        if (n->kind == NodeKind::Member) {
            Value* obj = lvalue(n->left);
            if (obj != nullptr && obj->kind == TypeKind::Pointer && obj->ptr != nullptr) {
                obj = obj->ptr;
            }
            if (obj == nullptr || obj->type == nullptr || obj->type->decl == nullptr) {
                fail("invalid field access");
                return nullptr;
            }
            int i = field_index(obj->type->decl, n->text);
            if (i < 0 || i >= static_cast<int>(obj->fields.size())) {
                fail("no such field");
                return nullptr;
            }
            return &obj->fields[static_cast<size_t>(i)];
        }
        if (n->kind == NodeKind::Unary && n->op == TokenKind::Star) {
            Value p = eval(n->left);
            if (trapped) {
                return nullptr;
            }
            if (p.ptr == nullptr) {
                fail("null pointer");
                return nullptr;
            }
            return p.ptr;
        }
        if (n->kind == NodeKind::Index) {
            Value* base = lvalue(n->left);
            Value idxv = eval(n->body);
            if (trapped || base == nullptr) {
                return nullptr;
            }
            size_t i = static_cast<size_t>(as_u(idxv, n->body != nullptr ? n->body->ty : nullptr));
            Type* bt = n->left != nullptr ? n->left->ty : base->type;
            if (is_ptr(bt) && base->kind == TypeKind::Pointer) {
                if (base->ptr == nullptr) {
                    fail("null pointer");
                    return nullptr;
                }
                return base->ptr + static_cast<ptrdiff_t>(i);
            }
            if (base->kind == TypeKind::Pointer && base->ptr != nullptr) {
                if (base->ptr->kind == TypeKind::Array || !base->ptr->fields.empty()) {
                    base = base->ptr;
                }
            }
            size_t nlen = base->length != 0 ? base->length : base->fields.size();
            if (is_array(bt) || is_span(bt) || base->kind == TypeKind::Array ||
                base->kind == TypeKind::Span) {
                if (i >= nlen) {
                    fail("index out of bounds");
                    return nullptr;
                }
                if (base->ptr != nullptr) {
                    return base->ptr + static_cast<ptrdiff_t>(i);
                }
                return &base->fields[i];
            }
        }
        fail("not an lvalue");
        return nullptr;
    }

    string decode_string(string_view tok) {
        // Strip quotes; keep simple escapes.
        if (tok.size() >= 2 && tok[0] == '"') {
            tok = tok.substr(1, tok.size() - 2);
        }
        string out;
        for (size_t i = 0; i < tok.size(); i++) {
            if (tok[i] == '\\' && i + 1 < tok.size()) {
                char e = tok[i + 1];
                if (e == 'n') {
                    out += '\n';
                } else if (e == 't') {
                    out += '\t';
                } else if (e == '\\' || e == '"') {
                    out += e;
                } else {
                    out += e;
                }
                i++;
            } else {
                out += tok[i];
            }
        }
        return out;
    }

    string show(const Value& v) {
        if (v.kind == TypeKind::Bool) {
            return v.b ? "true" : "false";
        }
        if (v.kind == TypeKind::Str) {
            return decode_string(v.str);
        }
        if (is_float(v.type) || v.kind == TypeKind::F32 || v.kind == TypeKind::F64) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", v.f);
            return buf;
        }
        if (is_unsigned_int(v.type) || v.kind == TypeKind::U8 || v.kind == TypeKind::U16 ||
            v.kind == TypeKind::U32 || v.kind == TypeKind::U64 || v.kind == TypeKind::Usize ||
            v.kind == TypeKind::Char) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(as_u(v, v.type)));
            return buf;
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(as_s(v, v.type)));
        return buf;
    }

    Value eval(Node* n) {
        if (n == nullptr || trapped) {
            return v_unit();
        }
        switch (n->kind) {
        case NodeKind::Literal:
            if (n->op == TokenKind::KwTrue) {
                return v_bool(true);
            }
            if (n->op == TokenKind::KwFalse) {
                return v_bool(false);
            }
            if (n->op == TokenKind::KwNone) {
                Value v;
                v.kind = TypeKind::Pointer;
                v.type = n->ty;
                v.ptr = nullptr;
                return v;
            }
            if (n->op == TokenKind::StringLit) {
                Value v = v_str(n->text);
                v.length = decode_string(n->text).size();
                v.type = n->ty;
                return v;
            }
            if (n->op == TokenKind::IntLit) {
                ParsedInt p = parse_int_literal(n->text);
                Type* t = n->ty;
                if (t == nullptr || !is_int(t)) {
                    return v_i64(static_cast<int64_t>(p.value));
                }
                return v_int(t, p.value);
            }
            if (n->op == TokenKind::FloatLit) {
                ParsedFloat p = parse_float_literal(n->text);
                return v_float(n->ty, p.value);
            }
            if (n->op == TokenKind::CharLit) {
                uint32_t cp = 0;
                parse_char_literal(n->text, &cp);
                if (n->ty != nullptr && n->ty->kind == TypeKind::U8) {
                    return v_int(n->ty, cp);
                }
                Value v;
                v.kind = TypeKind::Char;
                v.type = n->ty;
                v.u = cp;
                return v;
            }
            return v_unit();
        case NodeKind::Name:
        case NodeKind::Self: {
            Value* p = lvalue(n);
            if (p == nullptr) {
                return v_unit();
            }
            return *p;
        }
        case NodeKind::Group:
            return eval(n->left);
        case NodeKind::Unit:
            return v_unit();
        case NodeKind::Unary:
            return eval_unary(n);
        case NodeKind::Binary:
            return eval_binary(n);
        case NodeKind::Call:
            return eval_call(n);
        case NodeKind::Member:
            return eval_member(n);
        case NodeKind::Index:
            return eval_index(n);
        case NodeKind::Slice:
            return eval_slice(n);
        case NodeKind::ArrayLit:
            return eval_array_lit(n);
        case NodeKind::SpanMake:
            return eval_span_make(n);
        case NodeKind::Cast:
            return eval_conv(n->left, n->ty, false);
        case NodeKind::Conditional: {
            Value c = eval(n->type);
            if (c.b) {
                return eval(n->left);
            }
            return eval(n->right);
        }
        default:
            fail("unsupported expression at runtime");
            return v_unit();
        }
    }

    Value eval_member(Node* n) {
        Value obj = eval(n->left);
        if (trapped) {
            return v_unit();
        }
        if (obj.kind == TypeKind::Pointer && obj.ptr != nullptr) {
            obj = *obj.ptr;
        }
        if (n->text == "length") {
            if (obj.kind == TypeKind::Str) {
                return v_int(n->ty, decode_string(obj.str).size());
            }
            size_t len = obj.length != 0 ? obj.length : obj.fields.size();
            if (obj.kind == TypeKind::Array && obj.type != nullptr) {
                len = static_cast<size_t>(obj.type->length);
            }
            return v_int(n->ty != nullptr ? n->ty : nullptr, len);
        }
        if (n->text == "data") {
            Value v;
            v.kind = TypeKind::Pointer;
            v.type = n->ty;
            if (obj.kind == TypeKind::Span) {
                v.ptr = obj.ptr;
            } else if (obj.kind == TypeKind::Array && !obj.fields.empty()) {
                v.ptr = &obj.fields[0];
            }
            return v;
        }
        if (n->text == "bytes") {
            Value v;
            v.kind = TypeKind::Span;
            v.type = n->ty;
            v.str = obj.str;
            v.length = decode_string(obj.str).size();
            return v;
        }
        Value* p = lvalue(n);
        if (p == nullptr) {
            return v_unit();
        }
        return *p;
    }

    Value eval_index(Node* n) {
        Value* slot = nullptr;
        if (n->left != nullptr &&
            (n->left->kind == NodeKind::Name || n->left->kind == NodeKind::Self ||
             n->left->kind == NodeKind::Member || n->left->kind == NodeKind::Index)) {
            slot = lvalue(n);
            if (slot != nullptr && !trapped) {
                return *slot;
            }
            if (trapped) {
                return v_unit();
            }
        }
        Value base = eval(n->left);
        Value idxv = eval(n->body);
        if (trapped) {
            return v_unit();
        }
        size_t i = static_cast<size_t>(as_u(idxv, n->body != nullptr ? n->body->ty : nullptr));
        if (base.kind == TypeKind::Str || !base.str.empty()) {
            string d = decode_string(base.str);
            if (i >= d.size()) {
                fail("index out of bounds");
                return v_unit();
            }
            return v_int(n->ty, static_cast<unsigned char>(d[i]));
        }
        if (base.kind == TypeKind::Span) {
            if (i >= base.length) {
                fail("index out of bounds");
                return v_unit();
            }
            if (base.ptr != nullptr) {
                return base.ptr[i];
            }
            if (!base.str.empty()) {
                string d = decode_string(base.str);
                if (i >= d.size()) {
                    fail("index out of bounds");
                    return v_unit();
                }
                return v_int(n->ty, static_cast<unsigned char>(d[i]));
            }
        }
        if (base.kind == TypeKind::Array) {
            if (i >= base.length) {
                fail("index out of bounds");
                return v_unit();
            }
            if (base.ptr != nullptr) {
                return base.ptr[i];
            }
            return base.fields[i];
        }
        if (base.kind == TypeKind::Pointer && base.ptr != nullptr) {
            return base.ptr[i];
        }
        fail("cannot index");
        return v_unit();
    }

    Value eval_slice(Node* n) {
        Value base = eval(n->left);
        size_t len = base.length != 0 ? base.length : base.fields.size();
        if (base.kind == TypeKind::Array && base.type != nullptr) {
            len = static_cast<size_t>(base.type->length);
        }
        if (base.kind == TypeKind::Str) {
            len = decode_string(base.str).size();
        }
        size_t start = 0;
        size_t end = len;
        if (n->body != nullptr) {
            start = static_cast<size_t>(as_u(eval(n->body), n->body->ty));
        }
        if (n->right != nullptr) {
            end = static_cast<size_t>(as_u(eval(n->right), n->right->ty));
        }
        if (trapped) {
            return v_unit();
        }
        if (start > end || end > len) {
            fail("index out of bounds");
            return v_unit();
        }
        Value v;
        v.kind = TypeKind::Span;
        v.type = n->ty;
        v.length = end - start;
        if (base.ptr != nullptr) {
            v.ptr = base.ptr + static_cast<ptrdiff_t>(start);
        } else if (base.kind == TypeKind::Array && !base.fields.empty()) {
            v.ptr = &base.fields[start];
        } else {
            v.str = base.str;
        }
        return v;
    }

    Value eval_array_lit(Node* n) {
        vector<Value> elems;
        for (Node* e = n->body; e != nullptr; e = e->next) {
            elems.push_back(eval(e));
            if (trapped) {
                return v_unit();
            }
        }
        return make_array(n->ty, std::move(elems));
    }

    Value eval_span_make(Node* n) {
        Value p = eval(n->body != nullptr ? n->body->left : nullptr);
        Value len = eval(n->body != nullptr && n->body->next != nullptr ? n->body->next->left : nullptr);
        if (trapped) {
            return v_unit();
        }
        Value v;
        v.kind = TypeKind::Span;
        v.type = n->ty;
        v.ptr = p.ptr;
        v.length = static_cast<size_t>(as_u(len, n->ty));
        return v;
    }

    Value eval_unary(Node* n) {
        Value x = eval(n->left);
        if (trapped) {
            return v_unit();
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
            return v_int(t, ~as_u(x, t));
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
                if (p.ok &&
                    p.value == static_cast<uint64_t>(int_max_signed(int_bits(t))) + 1) {
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

    bool cmp_num(const Value& L, const Value& R, Type* t, TokenKind op) {
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

    Value arith(Type* t, const Value& L, const Value& R, TokenKind op) {
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
            if (op == TokenKind::Plus || op == TokenKind::Minus || op == TokenKind::Star) {
                bool ov = false;
                if (op == TokenKind::Plus) {
                    ov = __builtin_add_overflow(a, b, &r);
                } else if (op == TokenKind::Minus) {
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
            if (op == TokenKind::Plus) {
                if (bits >= 64 ? a > UINT64_MAX - b : a + b > maxv) {
                    fail("integer overflow");
                    return v_unit();
                }
                return v_int(t, a + b);
            }
            if (op == TokenKind::Minus) {
                if (a < b) {
                    fail("integer overflow");
                    return v_unit();
                }
                return v_int(t, a - b);
            }
            if (op == TokenKind::Star) {
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

    Value eval_binary(Node* n) {
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
        if (op == TokenKind::EqEq || op == TokenKind::NotEq) {
            bool eq = false;
            if (L.kind == TypeKind::Bool) {
                eq = L.b == R.b;
            } else if (L.kind == TypeKind::Str) {
                eq = show(L) == show(R);
            } else if (is_float(L.type)) {
                eq = L.f == R.f;
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
        return arith(t != nullptr ? t : L.type, L, R, op);
    }

    Value eval_conv(Node* srcn, Type* dest, bool checked) {
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
                    (is_signed_int(dest)
                         ? a > static_cast<double>(int_max_signed(bits))
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
            x.type = dest;
            x.kind = TypeKind::Pointer;
            return x;
        }
        x.type = dest;
        x.kind = dest->kind;
        return x;
    }

    Node* find_func(string_view name) {
        for (Node* d = module->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && d->text == name) {
                return d;
            }
        }
        return nullptr;
    }

    Value call_func(Node* fn, Value* self, Node* args) {
        if (static_cast<int>(frames.size()) >= k_max_frames) {
            fail("stack overflow");
            return v_unit();
        }
        Frame frame;
        if (self != nullptr) {
            Slot s;
            s.name = "self";
            s.value = *self;
            frame.slots.push_back(s);
        }
        Node* p = fn->right;
        Node* a = args;
        while (p != nullptr && a != nullptr) {
            Slot s;
            s.name = p->text;
            s.value = eval(a->left);
            if (p->ty != nullptr) {
                s.value.type = p->ty;
                s.value.kind = p->ty->kind;
            }
            if (trapped) {
                return v_unit();
            }
            frame.slots.push_back(s);
            p = p->next;
            a = a->next;
        }
        frames.push_back(frame);
        returning = false;
        exec(fn->body);
        if (self != nullptr && !frames.empty()) {
            Slot* ss = nullptr;
            Frame& top = frames.back();
            for (size_t i = 0; i < top.slots.size(); i++) {
                if (top.slots[i].name == "self") {
                    ss = &top.slots[i];
                    break;
                }
            }
            if (ss != nullptr) {
                *self = ss->value;
            }
        }
        Value result = returning ? ret : v_unit();
        returning = false;
        frames.pop_back();
        return result;
    }

    Value eval_call(Node* n) {
        Node* callee = n->left;
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "print") {
            Value a = eval(n->body != nullptr ? n->body->left : nullptr);
            if (!trapped) {
                output += show(a);
                output += '\n';
            }
            return v_unit();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && callee->text == "trap") {
            Value a = eval(n->body != nullptr ? n->body->left : nullptr);
            fail(show(a));
            return v_unit();
        }
        if (callee != nullptr && callee->kind == NodeKind::Name &&
            (callee->text == "sizeof" || callee->text == "alignof")) {
            Node* arg = n->body != nullptr ? n->body->left : nullptr;
            Type* t = arg != nullptr ? arg->ty : nullptr;
            uint64_t v = callee->text == "sizeof" ? static_cast<uint64_t>(type_size(t))
                                                  : static_cast<uint64_t>(type_align(t));
            return v_int(n->ty, v);
        }
        if (callee != nullptr && callee->kind == NodeKind::Name && n->ty != nullptr &&
            n->resolved == nullptr && n->body != nullptr) {
            Type* dest = n->ty;
            if (is_int(dest) || is_float(dest) || dest->kind == TypeKind::Char) {
                return eval_conv(n->body->left, dest, true);
            }
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Struct) {
            return eval_ctor(n, n->resolved);
        }
        if (callee != nullptr && callee->kind == NodeKind::Member) {
            Node* method = callee->resolved;
            if (method == nullptr || method->kind != NodeKind::Func) {
                fail("unknown method");
                return v_unit();
            }
            if ((method->flags & FlagStatic) != 0) {
                return call_func(method, nullptr, n->body);
            }
            Value* recv = lvalue(callee->left);
            if (recv == nullptr) {
                Value tmp = eval(callee->left);
                return call_func(method, &tmp, n->body);
            }
            return call_func(method, recv, n->body);
        }
        if (n->resolved != nullptr && n->resolved->kind == NodeKind::Func) {
            return call_func(n->resolved, nullptr, n->body);
        }
        fail("unknown call");
        return v_unit();
    }

    Value eval_ctor(Node* n, Node* st) {
        Value v = v_zero(st->ty);
        for (Node* a = n->body; a != nullptr; a = a->next) {
            if (a->text.empty() || a->left == nullptr) {
                continue;
            }
            int i = field_index(st, a->text);
            if (i < 0) {
                continue;
            }
            v.fields[static_cast<size_t>(i)] = eval(a->left);
            if (trapped) {
                return v_unit();
            }
        }
        return v;
    }

    void exec(Node* n) {
        if (n == nullptr || trapped || returning) {
            return;
        }
        switch (n->kind) {
        case NodeKind::Block:
            for (Node* s = n->body; s != nullptr; s = s->next) {
                exec(s);
                if (trapped || returning) {
                    return;
                }
            }
            break;
        case NodeKind::Let:
        case NodeKind::Var: {
            Slot s;
            s.name = n->text;
            if (n->left != nullptr) {
                if (n->ty != nullptr && n->ty->kind == TypeKind::Span &&
                    n->left->kind == NodeKind::Name) {
                    Value* src = lvalue(n->left);
                    s.value.kind = TypeKind::Span;
                    s.value.type = n->ty;
                    if (src != nullptr) {
                        s.value.ptr = src->ptr != nullptr ? src->ptr : src->fields.data();
                        s.value.length = src->length != 0 ? src->length : src->fields.size();
                        s.value.str = src->str;
                    }
                } else {
                    s.value = eval(n->left);
                    if (n->ty != nullptr && n->ty->kind == TypeKind::Span &&
                        s.value.kind == TypeKind::Array) {
                        s.value.ptr = s.value.ptr;
                        s.value.kind = TypeKind::Span;
                        s.value.type = n->ty;
                    }
                    if (n->ty != nullptr) {
                        s.value.type = n->ty;
                        s.value.kind = n->ty->kind;
                    }
                }
            } else {
                s.value = zero_of(n->ty);
            }
            if (!frames.empty()) {
                frames.back().slots.push_back(s);
            }
            break;
        }
        case NodeKind::Assign: {
            Value* dst = lvalue(n->left);
            Value src = eval(n->right);
            if (dst == nullptr || trapped) {
                return;
            }
            if (n->op == TokenKind::Eq) {
                Type* dt = n->left != nullptr ? n->left->ty : dst->type;
                src.kind = dst->kind;
                src.type = dt != nullptr ? dt : dst->type;
                if (dt != nullptr) {
                    src.kind = dt->kind;
                }
                *dst = src;
                break;
            }
            TokenKind op = TokenKind::Plus;
            if (n->op == TokenKind::PlusEq) {
                op = TokenKind::Plus;
            } else if (n->op == TokenKind::MinusEq) {
                op = TokenKind::Minus;
            } else if (n->op == TokenKind::StarEq) {
                op = TokenKind::Star;
            } else if (n->op == TokenKind::SlashSlashEq) {
                op = TokenKind::SlashSlash;
            } else if (n->op == TokenKind::PercentEq) {
                op = TokenKind::Percent;
            } else if (n->op == TokenKind::PlusPercentEq) {
                op = TokenKind::PlusPercent;
            } else if (n->op == TokenKind::MinusPercentEq) {
                op = TokenKind::MinusPercent;
            } else if (n->op == TokenKind::StarPercentEq) {
                op = TokenKind::StarPercent;
            } else if (n->op == TokenKind::PlusPipeEq) {
                op = TokenKind::PlusPipe;
            } else if (n->op == TokenKind::MinusPipeEq) {
                op = TokenKind::MinusPipe;
            } else if (n->op == TokenKind::StarPipeEq) {
                op = TokenKind::StarPipe;
            } else if (n->op == TokenKind::AmpEq) {
                op = TokenKind::Amp;
            } else if (n->op == TokenKind::PipeEq) {
                op = TokenKind::Pipe;
            } else if (n->op == TokenKind::CaretEq) {
                op = TokenKind::Caret;
            } else if (n->op == TokenKind::LtLtEq) {
                op = TokenKind::LtLt;
            } else if (n->op == TokenKind::GtGtEq) {
                op = TokenKind::GtGt;
            } else {
                fail("unsupported assignment");
                return;
            }
            Value r = arith(n->left->ty, *dst, src, op);
            if (!trapped) {
                *dst = r;
            }
            break;
        }
        case NodeKind::If: {
            Value c = eval(n->left);
            if (trapped) {
                return;
            }
            if (c.b) {
                exec(n->body);
            } else {
                exec(n->right);
            }
            break;
        }
        case NodeKind::While:
            while (!trapped && !returning) {
                Value c = eval(n->left);
                if (trapped || !c.b) {
                    break;
                }
                exec(n->body);
            }
            break;
        case NodeKind::Return:
            ret = n->left != nullptr ? eval(n->left) : v_unit();
            returning = true;
            break;
        case NodeKind::For: {
            Value it = eval(n->right);
            if (trapped) {
                return;
            }
            size_t len = it.length != 0 ? it.length : it.fields.size();
            if (it.kind == TypeKind::Array && it.type != nullptr) {
                len = static_cast<size_t>(it.type->length);
            }
            if (it.kind == TypeKind::Str) {
                string d = decode_string(it.str);
                len = d.size();
                for (size_t i = 0; i < len && !trapped && !returning; i++) {
                    Slot s;
                    s.name = n->text;
                    s.value = v_int(n->ty, static_cast<unsigned char>(d[static_cast<size_t>(i)]));
                    if (n->ty != nullptr && n->ty->kind == TypeKind::Char) {
                        s.value.kind = TypeKind::Char;
                        s.value.u = static_cast<unsigned char>(d[i]);
                    }
                    frames.back().slots.push_back(s);
                    exec(n->body);
                    frames.back().slots.pop_back();
                }
                break;
            }
            for (size_t i = 0; i < len && !trapped && !returning; i++) {
                Slot s;
                s.name = n->text;
                Value* elems = it.ptr;
                Value elem;
                if (elems != nullptr) {
                    elem = elems[i];
                } else if (i < it.fields.size()) {
                    elem = it.fields[i];
                }
                if (n->flags & FlagByPtr) {
                    Value p;
                    p.kind = TypeKind::Pointer;
                    p.type = n->ty;
                    if (elems != nullptr) {
                        p.ptr = elems + static_cast<ptrdiff_t>(i);
                    } else if (i < it.fields.size()) {
                        p.ptr = &it.fields[i];
                    }
                    s.value = p;
                } else {
                    s.value = elem;
                }
                frames.back().slots.push_back(s);
                exec(n->body);
                frames.back().slots.pop_back();
            }
            break;
        }
        case NodeKind::ExprStmt:
            eval(n->left);
            break;
        default:
            fail("unsupported statement at runtime");
            break;
        }
    }
};

} // namespace

EvalResult eval_module(Node* module) {
    EvalResult result;
    if (module == nullptr) {
        return result;
    }
    Interp ip;
    ip.module = module;
    Node* answer = ip.find_func("answer");
    if (answer == nullptr) {
        ip.fail("no `answer` function");
        result.trapped = true;
        result.trap = ip.trap;
        result.output = ip.output;
        return result;
    }
    Value v = ip.call_func(answer, nullptr, nullptr);
    result.output = ip.output;
    if (ip.trapped) {
        result.trapped = true;
        result.trap = ip.trap;
        return result;
    }
    result.ok = true;
    if (v.kind == TypeKind::I64 || (v.type != nullptr && v.type->kind == TypeKind::I64)) {
        result.has_answer = true;
        result.answer = as_s(v, v.type);
    }
    return result;
}

} // namespace lucb
