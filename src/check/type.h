// Resolved types. Interned so pointer equality is type equality.

#pragma once

#include "parse/ast.h"

namespace lucb {

enum class TypeKind : uint8_t {
    Error,
    Never,
    Unit,
    Bool,
    I64,
    Str,
    Struct,
};

struct Type {
    TypeKind kind = TypeKind::Error;
    string_view name;
    Node* decl = nullptr; // struct declaration
};

string type_name(const Type* t);

bool type_eq(const Type* a, const Type* b);
bool is_zeroable(const Type* t);

struct TypeSet {
    Type error;
    Type never;
    Type unit;
    Type boolean;
    Type i64;
    Type str;
    vector<Type*> structs;

    TypeSet();
    Type* intern_struct(string_view name, Node* decl, Arena& arena);
};

} // namespace lucb
