//==============================================================================================
//
//   check/resolve - Type interning and resolution of written types
//
//   DESCRIPTION:
//       Types are interned so pointer equality is type equality (type.h). This unit builds
//       them: scalars by name, the `c` aliases, pointers, arrays, spans, optionals, results,
//       tuples, atomics, interfaces, and function types, and turns a written type in the
//       syntax tree into a resolved `Type*` (base.md §5). It also answers the small
//       structural questions the rest of the checker asks: a struct's member, an enum's case
//       and tag, whether a type crosses the C boundary (§17).
//
//==============================================================================================

#include "check/checker.h"

#include "support/literal.h"

namespace lucb {

auto Checker::named_scalar(string_view name) -> Type* {
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
    if (name == "Allocator" || name == "CAllocator") {
        return ty_alloc;
    }
    if (name == "fmt") {
        return ty_fmt;
    }
    if (name == "Error") {
        return ty_err;
    }
    if (name == "ErrorCode") {
        return ty_errcode;
    }
    return nullptr;
}

// A `c.` type at `at`: the `c` module is a standard module, visible only after `import c`
// (§16.6).
auto Checker::imported_c_type(Node* at, string_view name) -> Type* {
    Type* t = c_alias(name);
    if (t != nullptr && !c_imported) {
        fail_n(at, "lucb.check.import", "`" + string(name) + "` needs `import c`");
    }
    return t;
}

auto Checker::c_alias(string_view name) -> Type* {
    if (name == "c.int") {
        return ty_i32;
    }
    if (name == "c.uint") {
        return ty_u32;
    }
    if (name == "c.short") {
        return ty_i16;
    }
    if (name == "c.ushort") {
        return ty_u16;
    }
    if (name == "c.schar") {
        return ty_i8;
    }
    if (name == "c.uchar") {
        return ty_u8;
    }
    if (name == "c.longlong") {
        return ty_i64;
    }
    if (name == "c.ulonglong") {
        return ty_u64;
    }
    if (name == "c.float") {
        return ty_f32;
    }
    if (name == "c.double") {
        return ty_f64;
    }
    if (name == "c.bool") {
        return ty_bool;
    }
    if (name == "c.size" || name == "c.uintptr") {
        return ty_usize;
    }
    if (name == "c.ssize" || name == "c.ptrdiff" || name == "c.intptr") {
        return ty_isize;
    }
    if (name == "c.long") {
        return ty_c_long;
    }
    if (name == "c.ulong") {
        return ty_c_ulong;
    }
    if (name == "c.char") {
        return ty_c_char;
    }
    if (name == "c.wchar") {
        return ty_c_wchar;
    }
    if (name == "c.va_list") {
        return ty_c_va_list;
    }
    if (name == "c.str") {
        return ty_cstr;
    }
    return nullptr;
}

auto Checker::make_type(TypeKind kind, string_view name) -> Type* {
    Type* t = arena->make<Type>();
    t->kind = kind;
    t->name = name;
    return t;
}

auto Checker::keep(const string& s) -> string_view {
    char* p = static_cast<char*>(arena->alloc(s.size() + 1, 1));
    memcpy(p, s.data(), s.size());
    p[s.size()] = 0;
    return {p, s.size()};
}

auto Checker::intern_ptr(Type* elem, bool is_const, bool is_vol, bool nullable) -> Type* {
    for (size_t i = 0; i < interned.size(); i++) {
        Type* t = interned[i];
        if (t->kind == TypeKind::Pointer && t->decl == nullptr && t->elem == elem &&
            t->is_const == is_const && t->is_volatile == is_vol && t->is_nullable == nullable) {
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

auto Checker::intern_arr(Type* elem, uint64_t n) -> Type* {
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

auto Checker::intern_iface(Node* decl, bool nullable) -> Type* {
    for (size_t i = 0; i < interned.size(); i++) {
        Type* t = interned[i];
        if (t->kind == TypeKind::Interface && t->decl == decl && t->is_nullable == nullable) {
            return t;
        }
    }
    Type* t = make_type(TypeKind::Interface, decl != nullptr ? decl->text : string_view{});
    t->decl = decl;
    t->is_nullable = nullable;
    t->name = keep(type_name(t));
    interned.push_back(t);
    return t;
}

auto Checker::intern_opt(Type* elem) -> Type* {
    if (is_ptr(elem) && !elem->is_nullable) {
        return intern_ptr(elem->elem, elem->is_const, elem->is_volatile, true);
    }
    if (elem != nullptr && elem->kind == TypeKind::Interface && !elem->is_nullable) {
        return intern_iface(elem->decl, true);
    }
    if (is_func(elem) && !elem->is_nullable) {
        return intern_func(elem->args, elem->ntargs, elem->elem, true);
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

auto Checker::intern_fail(Type* elem) -> Type* {
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

auto Checker::intern_sp(Type* elem, bool is_const) -> Type* {
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

auto Checker::intern_atomic(Type* elem) -> Type* {
    for (size_t i = 0; i < interned.size(); i++) {
        Type* t = interned[i];
        if (t->kind == TypeKind::Atomic && t->elem == elem) {
            return t;
        }
    }
    Type* t = make_type(TypeKind::Atomic, {});
    t->elem = elem;
    t->name = keep(type_name(t));
    interned.push_back(t);
    return t;
}

auto Checker::intern_tup(Type** elems, int n) -> Type* {
    for (size_t i = 0; i < interned.size(); i++) {
        Type* t = interned[i];
        if (t->kind != TypeKind::Tuple || t->ntargs != n) {
            continue;
        }
        bool same = true;
        for (int j = 0; j < n; j++) {
            if (t->args[j] != elems[j]) {
                same = false;
                break;
            }
        }
        if (same) {
            return t;
        }
    }
    Type* t = make_type(TypeKind::Tuple, {});
    t->ntargs = n;
    t->args =
        static_cast<Type**>(arena->alloc(sizeof(Type*) * static_cast<size_t>(n), alignof(Type*)));
    for (int j = 0; j < n; j++) {
        t->args[j] = elems[j];
    }
    t->name = keep(type_name(t));
    interned.push_back(t);
    return t;
}

auto Checker::intern_func(Type** params, int n, Type* result, bool nullable) -> Type* {
    for (size_t i = 0; i < interned.size(); i++) {
        Type* t = interned[i];
        if (t->kind != TypeKind::Func || t->ntargs != n || t->elem != result ||
            t->is_nullable != nullable) {
            continue;
        }
        bool same = true;
        for (int j = 0; j < n; j++) {
            if (t->args[j] != params[j]) {
                same = false;
                break;
            }
        }
        if (same) {
            return t;
        }
    }
    Type* t = make_type(TypeKind::Func, {});
    t->elem = result;
    t->ntargs = n;
    t->is_nullable = nullable;
    if (n > 0) {
        t->args = static_cast<Type**>(
            arena->alloc(sizeof(Type*) * static_cast<size_t>(n), alignof(Type*)));
        for (int j = 0; j < n; j++) {
            t->args[j] = params[j];
        }
    }
    t->name = keep(type_name(t));
    interned.push_back(t);
    return t;
}

auto Checker::func_type_of(Node* fn, Node* owner) -> Type* {
    vector<Type*> ps;
    if (owner != nullptr && fn != nullptr && (fn->flags & FlagStatic) == 0) {
        bool mut = (fn->flags & FlagMutating) != 0;
        ps.push_back(intern_ptr(owner->ty != nullptr ? owner->ty : t_error(), !mut, false, false));
    }
    if (fn != nullptr) {
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (p->flags & FlagVariadic) {
                continue;
            }
            ps.push_back(p->ty != nullptr ? p->ty : t_error());
        }
    }
    Type* result = fn != nullptr && fn->ty != nullptr ? fn->ty : t_unit();
    if (fn != nullptr && (fn->flags & FlagFallible) != 0 && !is_fail(result)) {
        result = intern_fail(result);
    }
    return intern_func(ps.empty() ? nullptr : ps.data(), static_cast<int>(ps.size()), result,
                       false);
}

auto Checker::make_fail_thunk(Node* fn, Type* ft) -> Node* {
    if (fn == nullptr || ft == nullptr) {
        return fn;
    }
    Node* syn = arena->make<Node>();
    syn->kind = NodeKind::Func;
    syn->text = keep("_th" + std::to_string(nlambda++));
    syn->flags = FlagFallible;
    Type* payload = is_fail(ft->elem) && ft->elem->elem != nullptr ? ft->elem->elem : t_unit();
    syn->ty = payload;
    Node* params = nullptr;
    Node* args = nullptr;
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        if (p->flags & FlagVariadic) {
            continue;
        }
        Node* np = arena->make<Node>();
        np->kind = NodeKind::Param;
        np->text = p->text;
        np->ty = p->ty;
        np->span = p->span;
        append_node(&params, np);
        Node* a = arena->make<Node>();
        a->kind = NodeKind::Param;
        Node* nm = arena->make<Node>();
        nm->kind = NodeKind::Name;
        nm->text = p->text;
        nm->ty = p->ty;
        nm->resolved = np;
        a->left = nm;
        append_node(&args, a);
    }
    syn->right = params;
    Node* call = arena->make<Node>();
    call->kind = NodeKind::Call;
    Node* callee = arena->make<Node>();
    callee->kind = NodeKind::Name;
    callee->text = fn->text;
    callee->resolved = fn;
    callee->ty = func_type_of(fn, nullptr);
    call->left = callee;
    call->body = args;
    call->resolved = fn;
    call->ty = fn->ty;
    Node* ret = arena->make<Node>();
    ret->kind = NodeKind::Return;
    ret->left = call;
    Node* block = arena->make<Node>();
    block->kind = NodeKind::Block;
    block->body = ret;
    syn->body = block;
    if (!checking_generic_template) {
        pending_clones.push_back(syn);
    }
    return syn;
}

auto Checker::func_converts(Type* from, Type* to) -> bool {
    if (!is_func(from) || !is_func(to)) {
        return false;
    }
    if (from->is_nullable && !to->is_nullable) {
        return false;
    }
    if (from->ntargs != to->ntargs) {
        return false;
    }
    for (int i = 0; i < from->ntargs; i++) {
        if (from->args[i] != to->args[i] && !type_eq(from->args[i], to->args[i])) {
            return false;
        }
    }
    Type* fr = from->elem != nullptr ? from->elem : t_unit();
    Type* tr = to->elem != nullptr ? to->elem : t_unit();
    if (type_eq(fr, tr)) {
        return true;
    }
    if (is_fail(tr) && type_eq(fr, tr->elem != nullptr ? tr->elem : t_unit())) {
        return true;
    }
    return false;
}

auto Checker::atomic_ok(Type* t) -> bool {
    if (t == nullptr) {
        return false;
    }
    if (is_int(t) || t->kind == TypeKind::Bool || is_ptr(t)) {
        return type_size(t) <= static_cast<int>(sizeof(void*));
    }
    return false;
}

auto Checker::resolve_type(Node* n) -> Type* {
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
    if (n->flags & FlagFuncType) {
        vector<Type*> ps;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            ps.push_back(resolve_type(a));
        }
        Type* result = n->left != nullptr ? resolve_type(n->left) : t_unit();
        n->ty = intern_func(ps.empty() ? nullptr : ps.data(), static_cast<int>(ps.size()), result,
                            false);
        return n->ty;
    }
    if (n->flags & FlagTupleType) {
        int nfields = 0;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            nfields++;
        }
        Type* elems[8];
        if (nfields < 2 || nfields > 8) {
            fail_n(n, "lucb.check.type", "a tuple needs between 2 and 8 elements");
            n->ty = t_error();
            return n->ty;
        }
        int i = 0;
        for (Node* a = n->body; a != nullptr; a = a->next) {
            elems[i++] = resolve_type(a);
        }
        n->ty = intern_tup(elems, nfields);
        return n->ty;
    }
    if (n->flags & FlagAtomic) {
        Type* inner = resolve_type(n->left);
        if (!atomic_ok(inner)) {
            fail_n(n, "lucb.check.type",
                   "`@T` needs an integer, `bool`, or pointer of at most pointer width");
            n->ty = t_error();
            return n->ty;
        }
        n->ty = intern_atomic(inner);
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
        if (elem->kind == TypeKind::Void || (n->left != nullptr && (n->left->flags & FlagVoid))) {
            elem = ty_void;
        }
        n->ty =
            intern_ptr(elem, (n->flags & FlagConst) != 0, (n->flags & FlagVolatile) != 0, false);
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
    Type* calias = imported_c_type(n, n->text);
    if (calias != nullptr) {
        n->ty = calias;
        return n->ty;
    }
    Binding* b = lookup(n->text);
    if (b != nullptr && b->type != nullptr) {
        if (b->decl != nullptr && b->decl->kind == NodeKind::TypeAlias) {
            if (b->decl->ty == nullptr || b->decl->ty->kind == TypeKind::Error) {
                if (b->decl->flags & FlagUsed) {
                    fail_n(n, "lucb.check.type", "this alias is recursive");
                    n->ty = t_error();
                    return n->ty;
                }
                b->decl->flags |= FlagUsed;
                Type* t = resolve_type(b->decl->type);
                b->decl->flags &= ~FlagUsed;
                b->decl->ty = t;
                b->type = t;
            }
        }
        bool type_bind =
            b->decl == nullptr || b->decl->kind == NodeKind::Struct ||
            b->decl->kind == NodeKind::Enum || b->decl->kind == NodeKind::Union ||
            b->decl->kind == NodeKind::GenericParam || b->decl->kind == NodeKind::Interface ||
            b->decl->kind == NodeKind::ExternType || b->decl->kind == NodeKind::ExternStruct ||
            b->decl->kind == NodeKind::ExternUnion || b->decl->kind == NodeKind::TypeAlias ||
            b->type->kind == TypeKind::Allocator || b->type->kind == TypeKind::Param ||
            b->type->kind == TypeKind::Interface;
        if (type_bind) {
            Type* t = b->type;
            if (n->body != nullptr) {
                if (b->decl == nullptr || !is_generic_decl(b->decl)) {
                    fail_n(n, "lucb.check.type",
                           "`" + string(n->text) + "` does not take type arguments");
                } else {
                    vector<Type*> targs;
                    for (Node* a = n->body; a != nullptr; a = a->next) {
                        targs.push_back(resolve_type(a));
                    }
                    if (static_cast<int>(targs.size()) != count_generics(b->decl)) {
                        fail_n(n, "lucb.check.type", "wrong number of type arguments");
                        t = t_error();
                    } else {
                        t = instantiate_struct(b->decl, targs, n);
                    }
                }
            } else if (b->decl != nullptr && is_generic_decl(b->decl) &&
                       !checking_generic_template) {
                fail_n(n, "lucb.check.type", "`" + string(n->text) + "` needs type arguments");
            }
            n->ty = t;
            n->resolved = b->decl;
            return n->ty;
        }
    }
    size_t dot = n->text.find('.');
    if (dot != string_view::npos && dot > 0 && dot + 1 < n->text.size()) {
        Binding* mb = lookup(n->text.substr(0, dot));
        if (mb != nullptr && mb->type != nullptr && mb->type->kind == TypeKind::Module) {
            mark_import(mb);
            Node* d = pub_member(mb->type->decl, n->text.substr(dot + 1));
            if (d == nullptr && (mb->type->decl->flags & FlagBuiltin) != 0) {
                // `memory.Allocator`, `io.Writer`: a standard module's type that this compiler
                // knows as a builtin bound by name, spelled through its module
                string_view member = n->text.substr(dot + 1);
                Type* builtin = named_scalar(member);
                if (builtin == nullptr) {
                    Binding* bb = lookup(member);
                    if (bb != nullptr && bb->type != nullptr &&
                        (bb->decl == nullptr || (bb->decl->flags & FlagBuiltin) != 0)) {
                        builtin = bb->type;
                    }
                }
                if (builtin != nullptr) {
                    n->ty = builtin;
                    return builtin;
                }
            }
            if (d != nullptr) {
                Type* t = decl_type(d);
                if (n->body != nullptr) {
                    // `module.List[i64]`: instantiate the imported generic here.
                    if (!is_generic_decl(d)) {
                        fail_n(n, "lucb.check.type",
                               "`" + string(n->text) + "` does not take type arguments");
                    } else {
                        vector<Type*> targs;
                        for (Node* a = n->body; a != nullptr; a = a->next) {
                            targs.push_back(resolve_type(a));
                        }
                        if (static_cast<int>(targs.size()) != count_generics(d)) {
                            fail_n(n, "lucb.check.type", "wrong number of type arguments");
                            t = t_error();
                        } else {
                            t = instantiate_struct(d, targs, n);
                        }
                    }
                } else if (is_generic_decl(d) && !checking_generic_template) {
                    fail_n(n, "lucb.check.type", "`" + string(n->text) + "` needs type arguments");
                }
                n->ty = t;
                n->resolved = d;
                return n->ty;
            }
        }
    }
    fail_n(n, "lucb.check.type", "unknown type `" + string(n->text) + "`");
    n->ty = t_error();
    return n->ty;
}

auto Checker::struct_member(Node* st, string_view name, NodeKind kind) -> Node* {
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

auto Checker::enum_case(Node* en, string_view name) -> Node* {
    return struct_member(en, name, NodeKind::EnumCase);
}

auto Checker::enum_tag(Node* en, Node* cse) -> int {
    int i = 0;
    if (en == nullptr) {
        return -1;
    }
    for (Node* m = en->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::EnumCase) {
            continue;
        }
        if (m == cse) {
            return i;
        }
        i++;
    }
    return -1;
}

auto Checker::is_c_repr(Type* t) -> bool {
    if (t == nullptr) {
        return true;
    }
    if (t->kind == TypeKind::Unit || t->kind == TypeKind::Never || t->kind == TypeKind::Void) {
        return true;
    }
    if (is_int(t) || is_float(t) || t->kind == TypeKind::Bool || t->kind == TypeKind::Char ||
        t->kind == TypeKind::CStr) {
        return true;
    }
    if (is_ptr(t) || t->kind == TypeKind::Struct || t->kind == TypeKind::Union || is_int_enum(t) ||
        is_func(t) || is_span(t) || t->kind == TypeKind::ErrorCode) {
        return true;
    }
    return false;
}

} // namespace lucb
