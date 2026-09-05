//==============================================================================================
//
//   interp/value - Interpreter values
//
//   DESCRIPTION:
//       One tagged `Value` for every Base type, with constructors and the width-aware integer
//       readers. Pointers are `Value*` into deque storage, so they survive growth; a
//       reinterpreted pointer is marked and refused rather than mis-modelled.
//
//==============================================================================================

#pragma once

#include "check/type.h"
#include "parse/ast.h"

#include <deque>

namespace lucb {

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
    Node* fn = nullptr;
    size_t length = 0;
    bool present = true;
    bool failed = false;
    int32_t err_code = 0;
    string_view err_msg;
    // A pointer reinterpreted to another pointee type. The interpreter models
    // memory as typed values, not bytes, so arithmetic or access through such
    // a pointer is refused rather than mis-modelled; the C backend is exact.
    bool punned = false;
};

inline Value v_unit() {
    Value v;
    return v;
}

inline Value v_bool(bool b) {
    Value v;
    v.kind = TypeKind::Bool;
    v.b = b;
    return v;
}

inline Value v_int(Type* t, uint64_t u) {
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

inline Value v_i64(int64_t i) {
    Value v;
    v.kind = TypeKind::I64;
    v.u = static_cast<uint64_t>(i);
    return v;
}

inline Value v_float(Type* t, double f) {
    Value v;
    v.type = t;
    v.kind = t != nullptr ? t->kind : TypeKind::F64;
    v.f = t != nullptr && t->kind == TypeKind::F32 ? static_cast<double>(static_cast<float>(f)) : f;
    return v;
}

inline Value v_str(string_view s) {
    Value v;
    v.kind = TypeKind::Str;
    v.str = s;
    return v;
}

inline Value v_zero(Type* t) {
    Value v;
    if (t == nullptr) {
        return v;
    }
    v.type = t;
    v.kind = t->kind;
    if ((t->kind == TypeKind::Struct || t->kind == TypeKind::Union) && t->decl != nullptr) {
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                v.fields.push_back(v_zero(m->ty));
            }
        }
    }
    if (t->kind == TypeKind::Enum) {
        v.u = 0;
        v.present = true;
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

inline int bits_of(const Value& v, Type* t) {
    int bits = int_bits(t != nullptr ? t : v.type);
    if (bits == 0) {
        return 64;
    }
    return bits;
}

inline int64_t as_s(const Value& v, Type* t) {
    int bits = bits_of(v, t);
    uint64_t u = v.u & int_mask(bits);
    if (bits < 64 && (u & (uint64_t{1} << (bits - 1))) != 0) {
        return static_cast<int64_t>(u | ~int_mask(bits));
    }
    return static_cast<int64_t>(u);
}

inline uint64_t as_u(const Value& v, Type* t) {
    return v.u & int_mask(bits_of(v, t));
}

inline int field_index(Node* st, string_view name) {
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
    Node* decl = nullptr;
    Value value;
};

struct Frame {
    std::deque<Slot> slots;
};

} // namespace lucb
