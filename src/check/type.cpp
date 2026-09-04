#include "check/type.h"

namespace lucb {

string type_name(const Type* t) {
    if (t == nullptr) {
        return "<missing>";
    }
    if (!t->name.empty()) {
        return string(t->name);
    }
    switch (t->kind) {
    case TypeKind::Error:
        return "<error>";
    case TypeKind::Never:
        return "never";
    case TypeKind::Unit:
        return "unit";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::I8:
        return "i8";
    case TypeKind::I16:
        return "i16";
    case TypeKind::I32:
        return "i32";
    case TypeKind::I64:
        return "i64";
    case TypeKind::Isize:
        return "isize";
    case TypeKind::U8:
        return "u8";
    case TypeKind::U16:
        return "u16";
    case TypeKind::U32:
        return "u32";
    case TypeKind::U64:
        return "u64";
    case TypeKind::Usize:
        return "usize";
    case TypeKind::F32:
        return "f32";
    case TypeKind::F64:
        return "f64";
    case TypeKind::Char:
        return "char";
    case TypeKind::Str:
        return "str";
    case TypeKind::Struct:
        return "<struct>";
    case TypeKind::UntypedInt:
        return "<integer>";
    }
    return "<unknown>";
}

bool type_eq(const Type* a, const Type* b) {
    if (a == b) {
        return true;
    }
    if (a == nullptr || b == nullptr) {
        return false;
    }
    if (a->kind == TypeKind::Error || b->kind == TypeKind::Error) {
        return true;
    }
    if (a->kind == TypeKind::Never || b->kind == TypeKind::Never) {
        return true;
    }
    return false;
}

bool is_int(const Type* t) {
    if (t == nullptr) {
        return false;
    }
    switch (t->kind) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::Isize:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
    case TypeKind::Usize:
        return true;
    default:
        return false;
    }
}

bool is_signed_int(const Type* t) {
    if (t == nullptr) {
        return false;
    }
    switch (t->kind) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::Isize:
        return true;
    default:
        return false;
    }
}

bool is_unsigned_int(const Type* t) {
    return is_int(t) && !is_signed_int(t);
}

bool is_float(const Type* t) {
    return t != nullptr && (t->kind == TypeKind::F32 || t->kind == TypeKind::F64);
}

bool is_numeric(const Type* t) {
    return is_int(t) || is_float(t);
}

int int_bits(const Type* t) {
    if (t == nullptr) {
        return 0;
    }
    switch (t->kind) {
    case TypeKind::I8:
    case TypeKind::U8:
        return 8;
    case TypeKind::I16:
    case TypeKind::U16:
        return 16;
    case TypeKind::I32:
    case TypeKind::U32:
        return 32;
    case TypeKind::I64:
    case TypeKind::U64:
        return 64;
    case TypeKind::Isize:
    case TypeKind::Usize:
        return pointer_bits();
    default:
        return 0;
    }
}

int float_bits(const Type* t) {
    if (t == nullptr) {
        return 0;
    }
    if (t->kind == TypeKind::F32) {
        return 32;
    }
    if (t->kind == TypeKind::F64) {
        return 64;
    }
    return 0;
}

uint64_t int_mask(int bits) {
    if (bits >= 64) {
        return ~uint64_t{0};
    }
    if (bits <= 0) {
        return 0;
    }
    return (uint64_t{1} << bits) - 1;
}

int64_t int_max_signed(int bits) {
    if (bits >= 64) {
        return INT64_MAX;
    }
    return static_cast<int64_t>((uint64_t{1} << (bits - 1)) - 1);
}

uint64_t int_max_unsigned(int bits) {
    return int_mask(bits);
}

int64_t int_min(const Type* t) {
    if (!is_signed_int(t)) {
        return 0;
    }
    int bits = int_bits(t);
    if (bits >= 64) {
        return INT64_MIN;
    }
    return -int_max_signed(bits) - 1;
}

bool can_widen(const Type* from, const Type* to) {
    if (from == nullptr || to == nullptr) {
        return false;
    }
    if (type_eq(from, to)) {
        return true;
    }
    if (is_int(from) && is_int(to) && is_signed_int(from) == is_signed_int(to)) {
        return int_bits(from) < int_bits(to);
    }
    if (is_float(from) && is_float(to)) {
        return float_bits(from) < float_bits(to);
    }
    return false;
}

int type_align(const Type* t) {
    if (t == nullptr) {
        return 1;
    }
    if (t->kind == TypeKind::Struct) {
        int align = 1;
        if (t->decl == nullptr) {
            return 1;
        }
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::Field) {
                continue;
            }
            int a = type_align(m->ty);
            if (a > align) {
                align = a;
            }
        }
        return align < 1 ? 1 : align;
    }
    int n = type_size(t);
    if (n <= 0) {
        return 1;
    }
    return n;
}

int type_size(const Type* t) {
    if (t == nullptr) {
        return 0;
    }
    switch (t->kind) {
    case TypeKind::Bool:
        return 1;
    case TypeKind::I8:
    case TypeKind::U8:
        return 1;
    case TypeKind::I16:
    case TypeKind::U16:
        return 2;
    case TypeKind::I32:
    case TypeKind::U32:
    case TypeKind::F32:
        return 4;
    case TypeKind::I64:
    case TypeKind::U64:
    case TypeKind::F64:
        return 8;
    case TypeKind::Isize:
    case TypeKind::Usize:
        return static_cast<int>(sizeof(void*));
    case TypeKind::Char:
        return 4;
    case TypeKind::Unit:
        return 0;
    case TypeKind::Str:
        return static_cast<int>(sizeof(const char*));
    case TypeKind::Struct: {
        if (t->decl == nullptr) {
            return 0;
        }
        int size = 0;
        int align = 1;
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::Field) {
                continue;
            }
            int falign = type_align(m->ty);
            int fsize = type_size(m->ty);
            if (falign > 1 && size % falign != 0) {
                size += falign - (size % falign);
            }
            size += fsize;
            if (falign > align) {
                align = falign;
            }
        }
        if (align > 1 && size % align != 0) {
            size += align - (size % align);
        }
        return size;
    }
    default:
        return 0;
    }
}

const char* c_type_name(const Type* t) {
    if (t == nullptr) {
        return "void";
    }
    switch (t->kind) {
    case TypeKind::Bool:
        return "bool";
    case TypeKind::I8:
        return "int8_t";
    case TypeKind::I16:
        return "int16_t";
    case TypeKind::I32:
        return "int32_t";
    case TypeKind::I64:
        return "int64_t";
    case TypeKind::Isize:
        return "intptr_t";
    case TypeKind::U8:
        return "uint8_t";
    case TypeKind::U16:
        return "uint16_t";
    case TypeKind::U32:
        return "uint32_t";
    case TypeKind::U64:
        return "uint64_t";
    case TypeKind::Usize:
        return "size_t";
    case TypeKind::F32:
        return "float";
    case TypeKind::F64:
        return "double";
    case TypeKind::Char:
        return "uint32_t";
    case TypeKind::Str:
        return "const char*";
    case TypeKind::Unit:
    case TypeKind::Never:
    case TypeKind::Error:
        return "void";
    default:
        return "void";
    }
}

bool is_zeroable(const Type* t) {
    if (t == nullptr) {
        return false;
    }
    if (is_int(t) || is_float(t) || t->kind == TypeKind::Bool || t->kind == TypeKind::Unit ||
        t->kind == TypeKind::Str || t->kind == TypeKind::Char) {
        return true;
    }
    if (t->kind != TypeKind::Struct || t->decl == nullptr) {
        return false;
    }
    for (Node* m = t->decl->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            if (!is_zeroable(m->ty)) {
                return false;
            }
        }
    }
    return true;
}

TypeSet::TypeSet() {
    error.kind = TypeKind::Error;
    never.kind = TypeKind::Never;
    never.name = "never";
    unit.kind = TypeKind::Unit;
    unit.name = "unit";
    boolean.kind = TypeKind::Bool;
    boolean.name = "bool";
    i8.kind = TypeKind::I8;
    i8.name = "i8";
    i16.kind = TypeKind::I16;
    i16.name = "i16";
    i32.kind = TypeKind::I32;
    i32.name = "i32";
    i64.kind = TypeKind::I64;
    i64.name = "i64";
    isize.kind = TypeKind::Isize;
    isize.name = "isize";
    u8.kind = TypeKind::U8;
    u8.name = "u8";
    u16.kind = TypeKind::U16;
    u16.name = "u16";
    u32.kind = TypeKind::U32;
    u32.name = "u32";
    u64.kind = TypeKind::U64;
    u64.name = "u64";
    usize.kind = TypeKind::Usize;
    usize.name = "usize";
    f32.kind = TypeKind::F32;
    f32.name = "f32";
    f64.kind = TypeKind::F64;
    f64.name = "f64";
    character.kind = TypeKind::Char;
    character.name = "char";
    str.kind = TypeKind::Str;
    str.name = "str";
    untyped_int.kind = TypeKind::UntypedInt;
    untyped_int.name = "<integer>";
}

Type* TypeSet::intern_struct(string_view name, Node* decl, Arena& arena) {
    Type* t = arena.make<Type>();
    t->kind = TypeKind::Struct;
    t->name = name;
    t->decl = decl;
    structs.push_back(t);
    return t;
}

} // namespace lucb
