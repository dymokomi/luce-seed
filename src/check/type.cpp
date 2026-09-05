#include "check/type.h"

#include "support/literal.h"

namespace lucb {

namespace {

bool const_u64_lit(const Node* n, uint64_t* out) {
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
        return const_u64_lit(n->left, out);
    }
    return false;
}

int pad_to(int off, int align) {
    if (align <= 1) {
        return off;
    }
    int rem = off % align;
    if (rem == 0) {
        return off;
    }
    return off + (align - rem);
}

} // namespace


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
    case TypeKind::Void:
        return "void";
    case TypeKind::Pointer: {
        string s;
        if (t->is_const) {
            s += "const ";
        }
        if (t->is_volatile) {
            s += "volatile ";
        }
        s += type_name(t->elem);
        s += '*';
        if (t->is_nullable) {
            s += '?';
        }
        return s;
    }
    case TypeKind::Array:
        return type_name(t->elem) + "[" + std::to_string(t->length) + "]";
    case TypeKind::Span: {
        string s;
        if (t->is_const) {
            s += "const ";
        }
        s += type_name(t->elem);
        s += "[]";
        return s;
    }
    case TypeKind::Optional:
        return type_name(t->elem) + "?";
    case TypeKind::Fallible:
        return type_name(t->elem) + "!";
    case TypeKind::ErrorVal:
        return "Error";
    case TypeKind::Enum:
        return t->decl != nullptr ? string(t->decl->text) : "<enum>";
    case TypeKind::Union:
        return t->decl != nullptr ? string(t->decl->text) : "<union>";
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

bool is_ptr(const Type* t) {
    return t != nullptr && t->kind == TypeKind::Pointer;
}

bool is_array(const Type* t) {
    return t != nullptr && t->kind == TypeKind::Array;
}

bool is_span(const Type* t) {
    return t != nullptr && t->kind == TypeKind::Span;
}

bool is_void_ptr(const Type* t) {
    return is_ptr(t) && t->elem != nullptr && t->elem->kind == TypeKind::Void;
}

Type* elem_of(const Type* t) {
    if (t == nullptr) {
        return nullptr;
    }
    return t->elem;
}

bool is_opt(const Type* t) {
    return t != nullptr && t->kind == TypeKind::Optional;
}

bool is_fail(const Type* t) {
    return t != nullptr && t->kind == TypeKind::Fallible;
}

bool is_enum(const Type* t) {
    return t != nullptr && t->kind == TypeKind::Enum;
}

bool is_int_enum(const Type* t) {
    return is_enum(t) && t->elem != nullptr && is_int(t->elem);
}

bool is_union(const Type* t) {
    return t != nullptr && t->kind == TypeKind::Union;
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

int field_align(const Node* f) {
    int a = type_align(f != nullptr ? f->ty : nullptr);
    uint64_t n = 0;
    if (f != nullptr && const_u64_lit(f->right, &n) && static_cast<int>(n) > a) {
        a = static_cast<int>(n);
    }
    return a < 1 ? 1 : a;
}

int layout_struct(const Type* t, string_view want, int* out_off) {
    if (t == nullptr || t->decl == nullptr) {
        return 0;
    }
    bool packed = t->packed || (t->decl->flags & FlagPacked) != 0;
    int size = 0;
    int align = packed ? 1 : 1;
    if (out_off != nullptr) {
        *out_off = -1;
    }
    for (Node* m = t->decl->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Field) {
            continue;
        }
        int fa = packed ? 1 : field_align(m);
        int fs = type_size(m->ty);
        if (!packed) {
            size = pad_to(size, fa);
        }
        if (out_off != nullptr && m->text == want) {
            *out_off = size;
        }
        size += fs;
        if (fa > align) {
            align = fa;
        }
    }
    uint64_t raised = 0;
    if (const_u64_lit(t->decl->type, &raised) && static_cast<int>(raised) > align) {
        align = static_cast<int>(raised);
    }
    if (t->align_to > align) {
        align = t->align_to;
    }
    size = pad_to(size, align);
    return size;
}

int type_align(const Type* t) {
    if (t == nullptr) {
        return 1;
    }
    if (t->kind == TypeKind::Pointer || t->kind == TypeKind::Span || t->kind == TypeKind::Str) {
        return static_cast<int>(sizeof(void*));
    }
    if (t->kind == TypeKind::Array) {
        return type_align(t->elem);
    }
    if (t->kind == TypeKind::Struct) {
        bool packed = t->packed || (t->decl != nullptr && (t->decl->flags & FlagPacked) != 0);
        int align = packed ? 1 : 1;
        if (t->decl != nullptr) {
            for (Node* m = t->decl->body; m != nullptr; m = m->next) {
                if (m->kind != NodeKind::Field) {
                    continue;
                }
                int a = packed ? 1 : field_align(m);
                if (a > align) {
                    align = a;
                }
            }
            uint64_t raised = 0;
            if (const_u64_lit(t->decl->type, &raised) && static_cast<int>(raised) > align) {
                align = static_cast<int>(raised);
            }
        }
        if (t->align_to > align) {
            align = t->align_to;
        }
        return align < 1 ? 1 : align;
    }
    if (t->kind == TypeKind::Union) {
        int align = 1;
        if (t->decl != nullptr) {
            for (Node* m = t->decl->body; m != nullptr; m = m->next) {
                if (m->kind != NodeKind::Field) {
                    continue;
                }
                int a = type_align(m->ty);
                if (a > align) {
                    align = a;
                }
            }
        }
        return align < 1 ? 1 : align;
    }
    if (t->kind == TypeKind::Enum) {
        if (is_int_enum(t)) {
            return type_align(t->elem);
        }
        int align = 4;
        if (t->decl != nullptr) {
            for (Node* c = t->decl->body; c != nullptr; c = c->next) {
                if (c->kind != NodeKind::EnumCase) {
                    continue;
                }
                for (Node* p = c->body; p != nullptr; p = p->next) {
                    int a = type_align(p->ty);
                    if (a > align) {
                        align = a;
                    }
                }
            }
        }
        return align;
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
        return static_cast<int>(sizeof(void*) + sizeof(size_t));
    case TypeKind::Pointer:
        return static_cast<int>(sizeof(void*));
    case TypeKind::Span:
        return static_cast<int>(sizeof(void*) + sizeof(size_t));
    case TypeKind::Optional:
        return type_size(t->elem) + 1;
    case TypeKind::Fallible:
        return type_size(t->elem) + static_cast<int>(sizeof(void*) * 2 + 1);
    case TypeKind::ErrorVal:
        return static_cast<int>(sizeof(int32_t) + sizeof(void*) + sizeof(size_t));
    case TypeKind::Array:
        return type_size(t->elem) * static_cast<int>(t->length);
    case TypeKind::Void:
        return 0;
    case TypeKind::Struct:
        return layout_struct(t, {}, nullptr);
    case TypeKind::Union: {
        if (t->decl == nullptr) {
            return 0;
        }
        int size = 0;
        int align = 1;
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind != NodeKind::Field) {
                continue;
            }
            int fs = type_size(m->ty);
            int fa = type_align(m->ty);
            if (fs > size) {
                size = fs;
            }
            if (fa > align) {
                align = fa;
            }
        }
        return pad_to(size, align);
    }
    case TypeKind::Enum: {
        if (is_int_enum(t)) {
            return type_size(t->elem);
        }
        int psz = 0;
        int palign = 4;
        if (t->decl != nullptr) {
            for (Node* c = t->decl->body; c != nullptr; c = c->next) {
                if (c->kind != NodeKind::EnumCase) {
                    continue;
                }
                int s = 0;
                int a = 1;
                for (Node* p = c->body; p != nullptr; p = p->next) {
                    int fa = type_align(p->ty);
                    s = pad_to(s, fa);
                    s += type_size(p->ty);
                    if (fa > a) {
                        a = fa;
                    }
                }
                s = pad_to(s, a);
                if (s > psz) {
                    psz = s;
                }
                if (a > palign) {
                    palign = a;
                }
            }
        }
        int size = pad_to(4, palign) + psz;
        return pad_to(size, palign);
    }
    default:
        return 0;
    }
}

int type_offset(const Type* t, string_view field) {
    if (is_union(t)) {
        return 0;
    }
    int off = -1;
    layout_struct(t, field, &off);
    return off;
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
        return "lb_str";
    case TypeKind::Pointer:
        return "void*";
    case TypeKind::Span:
        return t->is_const ? "lb_cspan" : "lb_span";
    case TypeKind::ErrorVal:
        return "lb_error";
    case TypeKind::Optional:
        return "lb_opt";
    case TypeKind::Fallible:
        return "lb_res";
    case TypeKind::Array:
        return "void";
    case TypeKind::Void:
        return "void";
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
        t->kind == TypeKind::Str || t->kind == TypeKind::Char || t->kind == TypeKind::Span) {
        return true;
    }
    if (is_ptr(t) && t->is_nullable) {
        return true;
    }
    if (is_array(t)) {
        return is_zeroable(t->elem);
    }
    if (is_opt(t) || t->kind == TypeKind::ErrorVal) {
        return true;
    }
    if (is_int_enum(t) && t->decl != nullptr) {
        uint64_t next = 0;
        for (Node* c = t->decl->body; c != nullptr; c = c->next) {
            if (c->kind != NodeKind::EnumCase) {
                continue;
            }
            uint64_t v = next;
            if (c->left != nullptr) {
                const_u64_lit(c->left, &v);
            }
            if (v == 0) {
                return true;
            }
            next = v + 1;
        }
        return false;
    }
    if (is_enum(t) && t->decl != nullptr) {
        for (Node* c = t->decl->body; c != nullptr; c = c->next) {
            if (c->kind == NodeKind::EnumCase) {
                return c->body == nullptr;
            }
        }
        return false;
    }
    if (is_union(t) && t->decl != nullptr) {
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field && !is_zeroable(m->ty)) {
                return false;
            }
        }
        return true;
    }
    if (t->kind != TypeKind::Struct || t->decl == nullptr) {
        return false;
    }
    for (Node* m = t->decl->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            if (m->left != nullptr || !is_zeroable(m->ty)) {
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

string c_type_spelling(const Type* t) {
    if (t == nullptr) {
        return "void";
    }
    if (t->kind == TypeKind::Pointer) {
        string e = c_type_spelling(t->elem);
        if (t->elem != nullptr && t->elem->kind == TypeKind::Void) {
            e = "void";
        }
        string q;
        if (t->is_const) {
            q += "const ";
        }
        if (t->is_volatile) {
            q += "volatile ";
        }
        return q + e + "*";
    }
    if (t->kind == TypeKind::Array) {
        return "lb_a_" + type_name(t->elem) + "_" + std::to_string(t->length);
    }
    return c_type_name(t);
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
