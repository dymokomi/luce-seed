#include "check/type.h"

namespace lucb {

string type_name(const Type* t) {
    if (t == nullptr) {
        return "<missing>";
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
    case TypeKind::I64:
        return "i64";
    case TypeKind::Str:
        return "str";
    case TypeKind::Struct:
        if (!t->name.empty()) {
            return string(t->name);
        }
        return "<struct>";
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

bool is_zeroable(const Type* t) {
    if (t == nullptr) {
        return false;
    }
    if (t->kind == TypeKind::I64 || t->kind == TypeKind::Bool || t->kind == TypeKind::Unit ||
        t->kind == TypeKind::Str) {
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
    i64.kind = TypeKind::I64;
    i64.name = "i64";
    str.kind = TypeKind::Str;
    str.name = "str";
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
