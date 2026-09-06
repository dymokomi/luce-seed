//==============================================================================================
//
//   interp/punning - Union reinterpretation for the interpreter
//
//   DESCRIPTION:
//       Encoding and decoding of values through the target's layout, used to keep a union's
//       members consistent. See punning.h.
//
//==============================================================================================

#include "interp/punning.h"

#include <cstring>

namespace lucb {

namespace {

// The `Field` node of member `index` of a struct or union declaration.
Node* member_field(Node* decl, int index) {
    int i = 0;
    for (Node* m = decl != nullptr ? decl->body : nullptr; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Field) {
            continue;
        }
        if (i == index) {
            return m;
        }
        i += 1;
    }
    return nullptr;
}

Value* element(Value& array, size_t i) {
    if (array.ptr != nullptr) {
        return array.ptr + static_cast<ptrdiff_t>(i);
    }
    return i < array.fields.size() ? &array.fields[i] : nullptr;
}

bool is_word(const Type* t) {
    return is_int(t) || t->kind == TypeKind::Bool || t->kind == TypeKind::Char ||
           t->kind == TypeKind::Enum;
}

bool encode(const Value& v, const Type* t, uint8_t* out);
bool decode(Value& v, const Type* t, const uint8_t* in);

bool encode_members(const Value& v, const Type* t, uint8_t* out, bool one) {
    Node* decl = t->decl;
    int i = 0;
    for (Node* m = decl != nullptr ? decl->body : nullptr; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Field) {
            continue;
        }
        const bool wanted = one ? i == v.active : true;
        if (wanted && static_cast<size_t>(i) < v.fields.size()) {
            const int offset = one ? 0 : type_offset(t, m->text);
            if (!encode(v.fields[static_cast<size_t>(i)], m->ty, out + offset)) {
                return false;
            }
        }
        i += 1;
    }
    return true;
}

bool decode_members(Value& v, const Type* t, const uint8_t* in, bool one) {
    Node* decl = t->decl;
    int i = 0;
    for (Node* m = decl != nullptr ? decl->body : nullptr; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Field) {
            continue;
        }
        const bool wanted = one ? i == v.active : true;
        if (wanted && static_cast<size_t>(i) < v.fields.size()) {
            const int offset = one ? 0 : type_offset(t, m->text);
            if (!decode(v.fields[static_cast<size_t>(i)], m->ty, in + offset)) {
                return false;
            }
        }
        i += 1;
    }
    return true;
}

// A value's bytes at the target's layout, little-endian. False when the type has no byte
// model here.
bool encode(const Value& v, const Type* t, uint8_t* out) {
    if (t == nullptr) {
        return false;
    }
    const int size = type_size(t);
    if (is_word(t)) {
        uint64_t u = t->kind == TypeKind::Bool ? (v.b ? 1 : 0) : v.u;
        for (int i = 0; i < size && i < 8; i++) {
            out[i] = static_cast<uint8_t>(u >> (8 * i));
        }
        return true;
    }
    if (t->kind == TypeKind::F32) {
        float f = static_cast<float>(v.f);
        std::memcpy(out, &f, sizeof f);
        return true;
    }
    if (t->kind == TypeKind::F64) {
        double d = v.f;
        std::memcpy(out, &d, sizeof d);
        return true;
    }
    if (t->kind == TypeKind::Array) {
        const int esize = type_size(t->elem);
        Value& a = const_cast<Value&>(v);
        for (uint64_t i = 0; i < t->length; i++) {
            Value* e = element(a, static_cast<size_t>(i));
            if (e == nullptr || !encode(*e, t->elem, out + static_cast<size_t>(i) * static_cast<size_t>(esize))) {
                return false;
            }
        }
        return true;
    }
    if (t->kind == TypeKind::Struct) {
        return encode_members(v, t, out, false);
    }
    if (t->kind == TypeKind::Union) {
        return v.active >= 0 ? encode_members(v, t, out, true) : true;
    }
    return false;
}

bool decode(Value& v, const Type* t, const uint8_t* in) {
    if (t == nullptr) {
        return false;
    }
    const int size = type_size(t);
    if (is_word(t)) {
        uint64_t u = 0;
        for (int i = 0; i < size && i < 8; i++) {
            u |= static_cast<uint64_t>(in[i]) << (8 * i);
        }
        if (t->kind == TypeKind::Bool) {
            v.b = u != 0;
        } else {
            v.u = is_int(t) ? (u & int_mask(int_bits(t))) : u;
        }
        return true;
    }
    if (t->kind == TypeKind::F32) {
        float f = 0;
        std::memcpy(&f, in, sizeof f);
        v.f = static_cast<double>(f);
        return true;
    }
    if (t->kind == TypeKind::F64) {
        std::memcpy(&v.f, in, sizeof v.f);
        return true;
    }
    if (t->kind == TypeKind::Array) {
        const int esize = type_size(t->elem);
        for (uint64_t i = 0; i < t->length; i++) {
            Value* e = element(v, static_cast<size_t>(i));
            if (e == nullptr || !decode(*e, t->elem, in + static_cast<size_t>(i) * static_cast<size_t>(esize))) {
                return false;
            }
        }
        return true;
    }
    if (t->kind == TypeKind::Struct) {
        return decode_members(v, t, in, false);
    }
    if (t->kind == TypeKind::Union) {
        return v.active >= 0 ? decode_members(v, t, in, true) : true;
    }
    return false;
}

} // namespace

Value* union_member(Value& u, int index) {
    if (u.type == nullptr || u.type->decl == nullptr || index < 0 ||
        static_cast<size_t>(index) >= u.fields.size()) {
        return nullptr;
    }
    if (u.active >= 0 && u.active != index) {
        Node* from = member_field(u.type->decl, u.active);
        Node* to = member_field(u.type->decl, index);
        const int size = type_size(u.type);
        vector<uint8_t> bytes(static_cast<size_t>(size > 0 ? size : 1), 0);
        if (from != nullptr && to != nullptr &&
            encode(u.fields[static_cast<size_t>(u.active)], from->ty, bytes.data())) {
            decode(u.fields[static_cast<size_t>(index)], to->ty, bytes.data());
        }
    }
    u.active = index;
    return &u.fields[static_cast<size_t>(index)];
}

} // namespace lucb
