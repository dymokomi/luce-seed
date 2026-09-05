// Resolved types. Interned so pointer equality is type equality.

#pragma once

#include "parse/ast.h"

namespace lucb {

enum class TypeKind : uint8_t {
    Error,
    Never,
    Unit,
    Bool,
    I8,
    I16,
    I32,
    I64,
    Isize,
    U8,
    U16,
    U32,
    U64,
    Usize,
    F32,
    F64,
    Char,
    Str,
    Struct,
    UntypedInt,
    Void,
    Pointer,
    Array,
    Span,
    Optional,
    Fallible,
    ErrorVal,
    Enum,
    Union,
};

struct Type {
    TypeKind kind = TypeKind::Error;
    string_view name;
    Node* decl = nullptr; // struct declaration
    Type* elem = nullptr; // pointee / element
    uint64_t length = 0;  // array N
    bool is_const = false;
    bool is_volatile = false;
    bool is_nullable = false; // T*?
    bool packed = false;
    int align_to = 0; // 0 = natural
};

inline int pointer_bits() { return static_cast<int>(sizeof(void*) * 8); }

string type_name(const Type* t);
bool type_eq(const Type* a, const Type* b);
bool is_zeroable(const Type* t);

bool is_int(const Type* t);
bool is_signed_int(const Type* t);
bool is_unsigned_int(const Type* t);
bool is_float(const Type* t);
bool is_numeric(const Type* t);
int int_bits(const Type* t);
int float_bits(const Type* t);
uint64_t int_mask(int bits);
int64_t int_min(const Type* t);
int64_t int_max_signed(int bits);
uint64_t int_max_unsigned(int bits);
bool can_widen(const Type* from, const Type* to);
int type_size(const Type* t);
int type_align(const Type* t);

bool is_ptr(const Type* t);
bool is_array(const Type* t);
bool is_span(const Type* t);
bool is_void_ptr(const Type* t);
bool is_opt(const Type* t);
bool is_fail(const Type* t);
bool is_enum(const Type* t);
bool is_int_enum(const Type* t);
bool is_union(const Type* t);
Type* elem_of(const Type* t);
int type_offset(const Type* t, string_view field);

const char* c_type_name(const Type* t);
string c_type_spelling(const Type* t);

struct TypeSet {
    Type error;
    Type never;
    Type unit;
    Type boolean;
    Type i8, i16, i32, i64, isize;
    Type u8, u16, u32, u64, usize;
    Type f32, f64;
    Type character;
    Type str;
    Type untyped_int;
    vector<Type*> structs;

    TypeSet();
    Type* intern_struct(string_view name, Node* decl, Arena& arena);
};

} // namespace lucb
