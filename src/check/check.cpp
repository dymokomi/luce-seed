//==============================================================================================
//
//   check/check - Declarations, modules, and the checking pass
//
//   DESCRIPTION:
//       The pass that turns a parsed module into a checked one: it collects type
//       declarations, resolves signatures before bodies so forward references and recursion
//       work, checks structs, enums, unions, interfaces and conformances, binds imports,
//       validates `main` and `test` declarations, and drives the statement and expression
//       checkers. `check_module` and `check_program` are the two entry points (base.md §9,
//       §10, §14, §16).
//
//==============================================================================================

#include "check/check.h"
#include "check/checker.h"

#include <cstdio>
#include <cstring>

namespace lucb {

// A `naked func` owns its frame: the body is asm blocks and nothing else (§9.8), and the
// blocks are what returns, so the every-path rule does not apply.
auto Checker::check_naked_body(Node* fn) -> void {
    Node* body = fn->body;
    Node* first = body != nullptr && body->kind == NodeKind::Block ? body->body : body;
    if (first == nullptr) {
        fail_n(fn, "lucb.check.naked", "a naked function's body is one asm block per architecture");
        return;
    }
    for (Node* s = first; s != nullptr; s = s->next) {
        if (s->kind != NodeKind::Asm) {
            fail_n(s, "lucb.check.naked", "a naked function's body is asm blocks only");
        }
    }
}

auto Checker::mark_local(Node* n) -> void {
    if (n != nullptr) {
        n->flags |= FlagLocal;
    }
}

auto Checker::is_local(Node* n) -> bool {
    return n != nullptr && (n->flags & FlagLocal) != 0;
}

auto Checker::fail(Span span, const char* code, const string& message) -> void {
    string p = path;
    if (current_module != nullptr && current_module->left != nullptr &&
        !current_module->left->text.empty()) {
        p = string(current_module->left->text);
    }
    diag->add(code, p, span, message);
}

auto Checker::fail_n(Node* n, const char* code, const string& message) -> void {
    fail(n != nullptr ? n->span : Span{}, code, message);
}

auto Checker::pop_scope() -> void {
    while (!scope.empty() && scope.back().depth == depth) {
        const Binding& b = scope.back();
        if (b.shadowed >= 0) {
            scope_index[b.name] = b.shadowed;
        } else {
            scope_index.erase(b.name);
        }
        scope.pop_back();
    }
    depth--;
}

auto Checker::lookup(string_view name) -> Binding* {
    auto it = scope_index.find(name);
    if (it == scope_index.end()) {
        return nullptr;
    }
    return &scope[static_cast<size_t>(it->second)];
}

auto Checker::bind(string_view name, Type* type, bool mut, Node* decl, Node* import_src) -> bool {
    if (lookup(name) != nullptr) {
        fail_n(decl, "lucb.check.shadow", "this name is already in scope");
        return false;
    }
    Binding b;
    b.name = name;
    b.type = type;
    b.mut = mut;
    b.decl = decl;
    b.import_src = import_src;
    b.depth = depth;
    auto it = scope_index.find(name);
    b.shadowed = it == scope_index.end() ? -1 : it->second;
    scope.push_back(b);
    scope_index[name] = static_cast<int>(scope.size()) - 1;
    return true;
}

static bool is_std_module(string_view name) {
    static const char* names[] = {"memory", "io", "files", "process", "atomic", "thread", "sync",
                                  "strings", "paths", "math", "time", "testing", "net", "c",
                                  "luce", "core", "debug"};
    for (const char* n : names) {
        if (name == n) {
            return true;
        }
    }
    return false;
}

auto Checker::mark_import(Binding* b) -> void {
    if (b != nullptr && b->import_src != nullptr) {
        b->import_src->flags |= FlagImportUsed;
    }
}

auto Checker::pub_member(Node* mod, string_view name) -> Node* {
    if (mod == nullptr) {
        return nullptr;
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->text == name && (d->flags & FlagPub) != 0 &&
            (d->kind == NodeKind::Func || d->kind == NodeKind::Struct ||
             d->kind == NodeKind::Enum || d->kind == NodeKind::Union ||
             d->kind == NodeKind::Interface || d->kind == NodeKind::Const ||
             d->kind == NodeKind::Global)) {
            return d;
        }
    }
    return nullptr;
}

auto Checker::decl_type(Node* d) -> Type* {
    if (d == nullptr) {
        return t_error();
    }
    if (d->ty != nullptr) {
        return d->ty;
    }
    if (d->kind == NodeKind::Func) {
        return t_unit();
    }
    return t_error();
}

auto Checker::last_component(string_view path) -> string {
    size_t dot = path.rfind('.');
    if (dot == string_view::npos) {
        return string(path);
    }
    return string(path.substr(dot + 1));
}

auto Checker::set_from_local(string_view name, bool from_local) -> void {
    Binding* b = lookup(name);
    if (b != nullptr) {
        b->from_local = from_local;
    }
}

auto Checker::is_core_name(string_view name) -> bool {
    return name == "print" || name == "assert" || name == "discard" || name == "error" ||
           name == "trap" || name == "hash" || name == "format" || name == "sizeof" ||
           name == "alignof" || name == "offsetof" || name == "hex" || name == "bin" ||
           name == "pad" || named_scalar(name) != nullptr || name == "f16" || name == "cstr" ||
           name == "fmt" || name == "FixedBuffer" || name == "memory" || name == "fmt" ||
           name == "format" || name == "luce" || name == "io" || name == "files" ||
           name == "process" || name == "Writer" || name == "Location" || name == "Error" ||
           name == "ErrorCode" || name == "thread" || name == "sync" || name == "atomic" ||
           name == "c";
}

auto Checker::const_u64(Node* n, uint64_t* out) -> bool {
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
        return const_u64(n->left, out);
    }
    if (n->kind == NodeKind::Call && n->left != nullptr && n->left->kind == NodeKind::Name &&
        n->left->text == "sizeof") {
        if (n->body == nullptr || n->body->left == nullptr) {
            return false;
        }
        Type* t = type_from_expr_or_name(n->body->left);
        *out = static_cast<uint64_t>(type_size(t));
        return true;
    }
    return false;
}

auto Checker::check_foreign_sig(Node* fn, bool exported) -> void {
    if (is_generic_decl(fn)) {
        fail_n(fn, "lucb.check.unsupported", "a generic cannot be `extern` or `export`");
        return;
    }
    if (exported && (fn->flags & FlagFallible) != 0) {
        fail_n(fn, "lucb.check.unsupported", "a fallible `export` is not in this slice");
    }
    const char* where = exported ? "an `export` signature cannot use `str`; write `cstr`"
                                 : "an `extern` signature cannot use `str`; write `cstr`";
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        if (p->flags & FlagVariadic) {
            continue;
        }
        if (p->flags & FlagOut) {
            fail_n(p, "lucb.check.unsupported", "`out` parameters are not in this slice");
        }
        if (p->ty != nullptr && p->ty->kind == TypeKind::Str) {
            fail_n(p, "lucb.check.type", where);
        } else if (p->ty != nullptr && !is_c_repr(p->ty)) {
            fail_n(p, "lucb.check.type", "this type is not C-representable");
        }
    }
    if (fn->ty != nullptr && fn->ty->kind == TypeKind::Str) {
        fail_n(fn, "lucb.check.type", where);
    } else if (fn->ty != nullptr && is_span(fn->ty)) {
        fail_n(fn, "lucb.check.type", "a span cannot be an `export` or `extern` result");
    } else if (fn->ty != nullptr && !is_c_repr(fn->ty)) {
        fail_n(fn, "lucb.check.type", "this type is not C-representable");
    }
}

auto Checker::check_params(Node* fn) -> void {
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        if (p->text == "self") {
            fail_n(p, "lucb.check.self",
                   "do not write `self` as a parameter; methods take it implicitly");
        }
        p->ty = resolve_type(p->type);
        if (p->left != nullptr) {
            Type* dt = check_expr(p->left, p->ty);
            if (!type_eq(dt, p->ty) && !can_widen(dt, p->ty)) {
                fail_n(p, "lucb.check.type",
                       "default has type " + type_name(dt) + ", expected " + type_name(p->ty));
            }
        }
        bind(p->text, p->ty, false, p);
    }
}

auto Checker::check_func(Node* fn, Node* owner) -> void {
    bool generic = is_generic_decl(fn);
    bool saved_generic = checking_generic_template;
    if (generic) {
        checking_generic_template = true;
        push_scope();
        bind_generic_params(fn);
    }
    Type* result = t_unit();
    if (fn->type != nullptr) {
        result = resolve_type(fn->type);
    }
    if (is_fail(result)) {
        fn->flags |= FlagFallible;
        result = result->elem != nullptr ? result->elem : t_unit();
    }
    fn->ty = result;
    Node* saved_fn = current_fn;
    Node* saved_st = current_struct;
    Type* saved_ret = return_type;
    bool saved_fail = fallible_fn;
    current_fn = fn;
    current_struct = owner;
    return_type = result;
    fallible_fn = (fn->flags & FlagFallible) != 0;
    push_scope();
    if (owner != nullptr && (fn->flags & FlagStatic) == 0) {
        if (fn->text == "init") {
            fn->flags |= FlagMutating;
        }
        bool mut = (fn->flags & FlagMutating) != 0;
        bind("self", owner->ty, mut, owner);
    }
    check_params(fn);
    check_stmt(fn->body);
    if ((fn->flags & FlagNaked) != 0) {
        check_naked_body(fn);
    } else if (!type_eq(result, t_unit()) && !always_returns(fn->body)) {
        fail_n(fn, "lucb.check.return", "this function must return a value on every path");
    }
    pop_scope();
    current_fn = saved_fn;
    current_struct = saved_st;
    return_type = saved_ret;
    fallible_fn = saved_fail;
    if (generic) {
        pop_scope();
        checking_generic_template = saved_generic;
    }
}

auto Checker::check_struct(Node* st) -> void {
    bool generic = is_generic_decl(st);
    bool saved_generic = checking_generic_template;
    if (generic) {
        checking_generic_template = true;
        push_scope();
        bind_generic_params(st);
        for (Node* m = st->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                m->ty = resolve_type(m->type);
            }
        }
    }
    for (Node* m = st->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            if (struct_member(st, m->text, NodeKind::Func) != nullptr) {
                fail_n(m, "lucb.check.shadow", "a method already uses this name");
            }
            for (Node* o = st->body; o != m; o = o->next) {
                if (o->kind == NodeKind::Field && o->text == m->text) {
                    fail_n(m, "lucb.check.shadow", "duplicate field");
                }
            }
        }
    }
    for (Node* m = st->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Func) {
            check_func(m, st);
        } else if (m->kind == NodeKind::Field) {
            if (m->left != nullptr) {
                Type* dt = check_expr(m->left, m->ty);
                if (!type_eq(dt, m->ty) && !can_widen(dt, m->ty)) {
                    fail_n(m, "lucb.check.type",
                           "field `" + string(m->text) + "` has type " + type_name(m->ty));
                }
            }
        } else {
            fail_n(m, "lucb.check.unsupported", "this member is not in the scalar core yet");
        }
    }
    if (generic) {
        pop_scope();
        checking_generic_template = saved_generic;
    }
    check_implements(st);
}

auto Checker::sig_matches(Node* impl, Node* req) -> bool {
    if (impl == nullptr || req == nullptr) {
        return false;
    }
    Node* ip = impl->right;
    Node* rp = req->right;
    while (ip != nullptr && rp != nullptr) {
        if (!type_eq(ip->ty, rp->ty) && !can_widen(ip->ty, rp->ty)) {
            return false;
        }
        ip = ip->next;
        rp = rp->next;
    }
    if (ip != nullptr || rp != nullptr) {
        return false;
    }
    Type* ir = impl->ty != nullptr ? impl->ty : t_unit();
    Type* rr = req->ty != nullptr ? req->ty : t_unit();
    if ((req->flags & FlagFallible) != 0) {
        if ((impl->flags & FlagFallible) != 0) {
            return type_eq(ir, rr);
        }
        return type_eq(ir, rr);
    }
    if ((impl->flags & FlagFallible) != 0) {
        return false;
    }
    return type_eq(ir, rr);
}

auto Checker::check_implements(Node* st) -> void {
    for (Node* t = st->right; t != nullptr; t = t->next) {
        Type* iface = resolve_type(t);
        if (iface == nullptr || iface->kind != TypeKind::Interface || iface->decl == nullptr) {
            fail_n(t, "lucb.check.type", "a conformance names an interface");
            continue;
        }
        t->ty = iface;
        for (Node* req = iface->decl->body; req != nullptr; req = req->next) {
            if (req->kind != NodeKind::Func) {
                continue;
            }
            Node* impl = struct_member(st, req->text, NodeKind::Func);
            if (impl == nullptr) {
                fail_n(st, "lucb.check.type",
                       "`" + string(st->text) + "` is missing `" + string(req->text) + "` for `" +
                           string(iface->decl->text) + "`");
                continue;
            }
            if ((req->flags & FlagMutating) != 0 && (impl->flags & FlagMutating) == 0) {
                fail_n(impl, "lucb.check.mut", "`" + string(req->text) + "` must be `mutating`");
            }
            if (!sig_matches(impl, req)) {
                fail_n(impl, "lucb.check.type",
                       "`" + string(impl->text) + "` does not match `" + string(iface->decl->text) +
                           "." + string(req->text) + "`");
            }
        }
    }
}

auto Checker::check_interface(Node* iface) -> void {
    if (is_generic_decl(iface)) {
        fail_n(iface, "lucb.check.unsupported", "generic interfaces are not in this slice");
        return;
    }
    for (Node* m = iface->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Func) {
            fail_n(m, "lucb.check.unsupported", "an interface may only declare methods");
            continue;
        }
        if (is_generic_decl(m)) {
            fail_n(m, "lucb.check.unsupported", "generic methods are not in this slice");
        }
        resolve_sig(m);
    }
}

auto Checker::collect_type_decl(Node* d, TypeKind kind) -> void {
    if (lookup(d->text) != nullptr) {
        fail_n(d, "lucb.check.shadow", "this name is already in scope");
        return;
    }
    Type* t = make_type(kind, d->text);
    t->decl = d;
    t->packed = (d->flags & FlagPacked) != 0;
    uint64_t al = 0;
    if (const_u64(d->type, &al)) {
        t->align_to = static_cast<int>(al);
    }
    d->ty = t;
    if (kind == TypeKind::Interface) {
        interned.push_back(t);
    }
    bind(d->text, t, false, d);
}

auto Checker::collect_module(Node* mod) -> void {
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Struct) {
            collect_type_decl(d, TypeKind::Struct);
        } else if (d->kind == NodeKind::Enum) {
            collect_type_decl(d, TypeKind::Enum);
        } else if (d->kind == NodeKind::Union) {
            collect_type_decl(d, TypeKind::Union);
        } else if (d->kind == NodeKind::Interface) {
            collect_type_decl(d, TypeKind::Interface);
        } else if (d->kind == NodeKind::Func || d->kind == NodeKind::ExternFunc) {
            if (is_core_name(d->text)) {
                fail_n(d, "lucb.check.shadow", "this name belongs to the language");
            }
            if (lookup(d->text) != nullptr) {
                fail_n(d, "lucb.check.shadow", "this name is already in scope");
                continue;
            }
            bind(d->text, t_unit(), false, d);
        } else if (d->kind == NodeKind::ExternType) {
            Type* t = nullptr;
            if (d->type != nullptr) {
                t = resolve_type(d->type);
            } else {
                t = make_type(TypeKind::Pointer, d->text);
                t->elem = ty_void;
                t->decl = d;
            }
            d->ty = t;
            bind(d->text, t, false, d);
        } else if (d->kind == NodeKind::ExternVar) {
            bind(d->text, t_error(), true, d);
        } else if (d->kind == NodeKind::ExternStruct) {
            collect_type_decl(d, TypeKind::Struct);
        } else if (d->kind == NodeKind::ExternUnion) {
            collect_type_decl(d, TypeKind::Union);
        } else if (d->kind == NodeKind::Global) {
            bind(d->text, t_error(), true, d);
        } else if (d->kind == NodeKind::Const) {
            bind(d->text, t_error(), false, d);
        } else if (d->kind == NodeKind::TypeAlias) {
            if (is_core_name(d->text)) {
                fail_n(d, "lucb.check.shadow", "this name belongs to the language");
            }
            if (lookup(d->text) != nullptr) {
                fail_n(d, "lucb.check.shadow", "this name is already in scope");
                continue;
            }
            bind(d->text, t_error(), false, d);
        } else if (d->kind == NodeKind::Import || d->kind == NodeKind::FromImport ||
                   d->kind == NodeKind::Test || d->kind == NodeKind::Assert ||
                   d->kind == NodeKind::Asm) {
            continue;
        } else {
            fail_n(d, "lucb.check.unsupported", "this declaration is not in this slice");
        }
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Func || d->kind == NodeKind::ExternFunc) {
            resolve_sig(d);
            Binding* b = lookup(d->text);
            if (b != nullptr && b->decl == d) {
                b->type = d->ty;
            }
        }
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if ((d->kind == NodeKind::Struct || d->kind == NodeKind::Union ||
             d->kind == NodeKind::ExternStruct || d->kind == NodeKind::ExternUnion) &&
            !is_generic_decl(d)) {
            for (Node* m = d->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Field) {
                    m->ty = resolve_type(m->type);
                    if (is_fail(m->ty)) {
                        fail_n(m, "lucb.check.type", "`T!` cannot be stored");
                    }
                }
            }
        } else if (d->kind == NodeKind::Enum) {
            if (d->right != nullptr && d->right->kind == NodeKind::Type) {
                Type* backing = resolve_type(d->right);
                if (!is_int(backing)) {
                    fail_n(d, "lucb.check.type", "an integer-backed enum needs an integer type");
                    backing = ty_u32;
                }
                if (d->ty != nullptr) {
                    d->ty->elem = backing;
                }
            }
            for (Node* m = d->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::EnumCase) {
                    if (m->text == "none") {
                        fail_n(m, "lucb.check.type", "a case may not be named `none`");
                    }
                    m->ty = d->ty;
                    for (Node* p = m->body; p != nullptr; p = p->next) {
                        p->ty = resolve_type(p->type);
                    }
                }
            }
        } else if (d->kind == NodeKind::ExternVar) {
            Type* t = d->type != nullptr ? resolve_type(d->type) : t_error();
            d->ty = t;
            Binding* b = lookup(d->text);
            if (b != nullptr && b->decl == d) {
                b->type = t;
            }
        } else if (d->kind == NodeKind::Global || d->kind == NodeKind::Const) {
            Type* t = d->type != nullptr ? resolve_type(d->type) : nullptr;
            if (d->left != nullptr) {
                bool saved_const = in_top_const;
                in_top_const = d->kind == NodeKind::Const;
                Type* init = check_expr(d->left, t);
                in_top_const = saved_const;
                if (t == nullptr) {
                    if (init != nullptr && init->kind == TypeKind::UntypedInt) {
                        init = coerce(d->left, init, t_i64());
                        d->left->ty = init;
                    }
                    t = init;
                }
            } else if (t == nullptr) {
                fail_n(d, "lucb.check.type", "this binding needs a type or an initialiser");
                t = t_error();
            } else if (d->kind == NodeKind::Global && !is_zeroable(t) &&
                       (d->flags & FlagUninit) == 0) {
                fail_n(d, "lucb.check.type", "this type has no zero value; write an initialiser");
            }
            d->ty = t;
            Binding* b = lookup(d->text);
            if (b != nullptr && b->decl == d) {
                b->type = t;
            }
        } else if (d->kind == NodeKind::TypeAlias) {
            if (d->ty == nullptr || d->ty->kind == TypeKind::Error) {
                if (d->flags & FlagUsed) {
                    fail_n(d, "lucb.check.type", "this alias is recursive");
                    d->ty = t_error();
                } else {
                    d->flags |= FlagUsed;
                    Type* t = resolve_type(d->type);
                    d->flags &= ~FlagUsed;
                    d->ty = t;
                }
            }
            Binding* b = lookup(d->text);
            if (b != nullptr && b->decl == d) {
                b->type = d->ty;
            }
        }
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Func || d->kind == NodeKind::ExternFunc) {
            resolve_sig(d);
            Binding* b = lookup(d->text);
            if (b != nullptr && b->decl == d) {
                b->type = d->ty;
            }
            if (d->kind == NodeKind::ExternFunc) {
                check_foreign_sig(d, false);
            } else if ((d->flags & FlagExport) != 0) {
                check_foreign_sig(d, true);
            }
        } else if (d->kind == NodeKind::Interface) {
            for (Node* m = d->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Func) {
                    resolve_sig(m);
                }
            }
        } else if (d->kind == NodeKind::Struct || d->kind == NodeKind::Enum ||
                   d->kind == NodeKind::Union) {
            bool g = is_generic_decl(d);
            if (g) {
                push_scope();
                bind_generic_params(d);
            }
            for (Node* m = d->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Func) {
                    resolve_sig(m);
                    if ((m->flags & FlagExport) != 0) {
                        check_foreign_sig(m, true);
                    }
                }
            }
            if (g) {
                pop_scope();
            }
        }
    }
}

auto Checker::check_enum(Node* en) -> void {
    bool saw_payload = false;
    bool saw_value = false;
    for (Node* m = en->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::EnumCase) {
            if (m->body != nullptr) {
                saw_payload = true;
            }
            if (m->left != nullptr) {
                saw_value = true;
                uint64_t v = 0;
                if (!const_u64(m->left, &v)) {
                    fail_n(m, "lucb.check.type", "enum case value must be a constant");
                }
            }
            for (Node* o = en->body; o != m; o = o->next) {
                if (o->kind == NodeKind::EnumCase && o->text == m->text) {
                    fail_n(m, "lucb.check.shadow", "duplicate case");
                }
            }
        } else if (m->kind == NodeKind::Func) {
            check_func(m, en);
        }
    }
    if (saw_payload && (saw_value || is_int_enum(en->ty))) {
        fail_n(en, "lucb.check.type", "payload cases cannot mix with integer-backed values");
    }
}

auto Checker::check_union(Node* un) -> void {
    for (Node* m = un->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            for (Node* o = un->body; o != m; o = o->next) {
                if (o->kind == NodeKind::Field && o->text == m->text) {
                    fail_n(m, "lucb.check.shadow", "duplicate member");
                }
            }
        } else if (m->kind == NodeKind::Func) {
            check_func(m, un);
        }
    }
}

auto Checker::resolve_sig(Node* fn) -> void {
    bool generic = is_generic_decl(fn);
    if (generic) {
        push_scope();
        bind_generic_params(fn);
    }
    Type* result = t_unit();
    if (fn->type != nullptr) {
        result = resolve_type(fn->type);
    }
    if (is_fail(result)) {
        fn->flags |= FlagFallible;
        result = result->elem != nullptr ? result->elem : t_unit();
    }
    fn->ty = result;
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        if (p->flags & FlagVariadic) {
            continue;
        }
        p->ty = resolve_type(p->type);
        if (is_fail(p->ty)) {
            fail_n(p, "lucb.check.type", "`T!` cannot be a parameter");
        }
    }
    if (generic) {
        pop_scope();
    }
}

// The module whose body holds `decl`, or null for a synthesized declaration.
auto Checker::module_of(Node* decl) -> Node* {
    for (size_t i = 0; i < program_modules.size(); i++) {
        Node* mod = program_modules[i];
        for (Node* d = mod != nullptr ? mod->body : nullptr; d != nullptr; d = d->next) {
            if (d == decl) {
                return mod;
            }
        }
    }
    return nullptr;
}

// Bind every top-level declaration of `mod`, as its own bodies see them.
auto Checker::bind_module_names(Node* mod) -> void {
    for (Node* d = mod != nullptr ? mod->body : nullptr; d != nullptr; d = d->next) {
        switch (d->kind) {
        case NodeKind::Func:
        case NodeKind::Struct:
        case NodeKind::Enum:
        case NodeKind::Union:
        case NodeKind::Interface:
        case NodeKind::TypeAlias:
        case NodeKind::Const:
        case NodeKind::Global:
        case NodeKind::ExternFunc:
        case NodeKind::ExternType:
        case NodeKind::ExternVar:
        case NodeKind::ExternStruct:
        case NodeKind::ExternUnion:
            if (!d->text.empty() && lookup(d->text) == nullptr) {
                bind(d->text, decl_type(d), d->kind == NodeKind::Global, d);
            }
            break;
        default:
            break;
        }
    }
}

auto Checker::bind_imports(Node* mod) -> void {
    vector<string_view> seen;
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind != NodeKind::Import && d->kind != NodeKind::FromImport) {
            continue;
        }
        // `import io` beside `from io import Writer` is two imports, not one twice
        if (d->kind == NodeKind::Import) {
            for (size_t i = 0; i < seen.size(); i++) {
                if (seen[i] == d->text) {
                    fail_n(d, "lucb.check.import", "duplicate import");
                }
            }
            seen.push_back(d->text);
        }
        Node* other = d->resolved;
        if (other == nullptr) {
            Binding* b = lookup(d->text);
            if (b != nullptr && b->type != nullptr && b->type->kind == TypeKind::Module) {
                other = b->decl;
                d->resolved = other;
            }
        }
        if (other == nullptr && is_std_module(d->text)) {
            // a standard module this seed keeps as builtins (`c`, `luce`): the import is what
            // luce-base requires; every name it could bring in is in scope already
            d->flags |= FlagImportUsed;
            for (Node* nm = d->body; nm != nullptr; nm = nm->next) {
                nm->flags |= FlagImportUsed;
            }
            continue;
        }
        if (other == nullptr || other->kind != NodeKind::Module) {
            fail_n(d, "lucb.check.import", "cannot find module `" + string(d->text) + "`");
            continue;
        }
        if (d->kind == NodeKind::Import) {
            string alias = d->left != nullptr && !d->left->text.empty() ? string(d->left->text)
                                                                        : last_component(d->text);
            Binding* existing = lookup(keep(alias));
            if (existing != nullptr && existing->decl == other) {
                existing->import_src = d;
                continue;
            }
            Type* mt = make_type(TypeKind::Module, d->text);
            mt->decl = other;
            bind(keep(alias), mt, false, other, d);
        } else {
            for (Node* nm = d->body; nm != nullptr; nm = nm->next) {
                Node* p = pub_member(other, nm->text);
                if (p == nullptr && is_std_module(d->text)) {
                    // a standard type this seed keeps as a builtin: the name is already bound
                    nm->flags |= FlagImportUsed;
                    continue;
                }
                if (p == nullptr) {
                    fail_n(nm, "lucb.check.import",
                           "no public `" + string(nm->text) + "` in `" + string(d->text) + "`");
                    continue;
                }
                bool mut = p->kind == NodeKind::Global;
                bind(nm->text, decl_type(p), mut, p, nm);
                if (is_std_module(d->text)) {
                    // the standard types are builtins here, so the name resolves without the
                    // binding; the import is what luce-base requires, and it counts as used
                    nm->flags |= FlagImportUsed;
                }
            }
        }
    }
}

auto Checker::check_unused_imports(Node* mod) -> void {
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Import) {
            if ((d->flags & FlagImportUsed) == 0) {
                fail_n(d, "lucb.check.import", "unused import `" + string(d->text) + "`");
            }
        } else if (d->kind == NodeKind::FromImport) {
            for (Node* nm = d->body; nm != nullptr; nm = nm->next) {
                if ((nm->flags & FlagImportUsed) == 0) {
                    fail_n(nm, "lucb.check.import", "unused import `" + string(nm->text) + "`");
                }
            }
        }
    }
}

auto Checker::check_test(Node* t) -> void {
    Node* saved_fn = current_fn;
    Type* saved_ret = return_type;
    bool saved_fail = fallible_fn;
    current_fn = t;
    return_type = t_unit();
    fallible_fn = true;
    push_scope();
    check_stmt(t->body);
    pop_scope();
    current_fn = saved_fn;
    return_type = saved_ret;
    fallible_fn = saved_fail;
}

auto Checker::check_main(Node* fn) -> void {
    if ((fn->flags & FlagPub) == 0) {
        fail_n(fn, "lucb.check.type", "`main` must be `pub`");
    }
    int nparams = 0;
    for (Node* p = fn->right; p != nullptr; p = p->next) {
        nparams++;
    }
    if (nparams != 1) {
        fail_n(fn, "lucb.check.type", "`main` takes `arguments: str[]` or `cstr[]`");
    } else {
        Type* a = fn->right->ty;
        bool ok = is_span(a) && a->elem != nullptr &&
                  (a->elem->kind == TypeKind::Str || a->elem->kind == TypeKind::CStr);
        if (!ok) {
            fail_n(fn, "lucb.check.type", "`main` takes `arguments: str[]` or `cstr[]`");
        }
    }
    if (fn->ty == nullptr || fn->ty->kind != TypeKind::I32) {
        fail_n(fn, "lucb.check.type", "`main` returns `i32` or `i32!`");
    }
}

auto Checker::check_module(Node* mod) -> void {
    current_module = mod;
    if (mod != nullptr && mod->text.empty() && !path.empty()) {
        mod->text = keep(path);
    }
    if (mod != nullptr && mod->left == nullptr && !path.empty()) {
        Node* loc = arena->make<Node>();
        loc->kind = NodeKind::Name;
        loc->text = keep(path);
        mod->left = loc;
    }
    push_scope();
    bind_memory();
    bind_imports(mod);
    collect_module(mod);
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Interface) {
            check_interface(d);
        }
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Struct) {
            check_struct(d);
        } else if (d->kind == NodeKind::Enum) {
            check_enum(d);
        } else if (d->kind == NodeKind::Union) {
            check_union(d);
        }
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Func && is_generic_decl(d)) {
            check_func(d, nullptr);
        }
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Func && !is_generic_decl(d)) {
            check_func(d, nullptr);
            if (d->text == "main") {
                check_main(d);
            }
        } else if (d->kind == NodeKind::Test) {
            check_test(d);
        } else if (d->kind == NodeKind::Assert) {
            check_assert(d);
        } else if (d->kind == NodeKind::Asm) {
            check_asm(d);
        }
    }
    vector<string> export_syms;
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Func && (d->flags & FlagExport) != 0) {
            string sym = string(d->text);
            for (size_t i = 0; i < export_syms.size(); i++) {
                if (export_syms[i] == sym) {
                    fail_n(d, "lucb.check.shadow", "duplicate export symbol");
                }
            }
            export_syms.push_back(sym);
        } else if (d->kind == NodeKind::Struct) {
            for (Node* m = d->body; m != nullptr; m = m->next) {
                if (m->kind != NodeKind::Func || (m->flags & FlagExport) == 0) {
                    continue;
                }
                string sym = string(d->text) + "_" + string(m->text);
                for (size_t i = 0; i < export_syms.size(); i++) {
                    if (export_syms[i] == sym) {
                        fail_n(m, "lucb.check.shadow", "duplicate export symbol");
                    }
                }
                export_syms.push_back(sym);
            }
        }
    }
    for (size_t i = 0; i < pending_clones.size(); i++) {
        append_node(&mod->body, pending_clones[i]);
    }
    pending_clones.clear();
    check_unused_imports(mod);
    pop_scope();
}

bool check_module(Node* module, Arena& arena, DiagnosticBag& diagnostics, string_view path) {
    if (module == nullptr) {
        return false;
    }
    Checker c;
    c.arena = &arena;
    c.diag = &diagnostics;
    c.path = string(path);
    c.ty_error = c.make_type(TypeKind::Error, "");
    c.ty_never = c.make_type(TypeKind::Never, "never");
    c.ty_unit = c.make_type(TypeKind::Unit, "unit");
    c.ty_bool = c.make_type(TypeKind::Bool, "bool");
    c.ty_i8 = c.make_type(TypeKind::I8, "i8");
    c.ty_i16 = c.make_type(TypeKind::I16, "i16");
    c.ty_i32 = c.make_type(TypeKind::I32, "i32");
    c.ty_i64 = c.make_type(TypeKind::I64, "i64");
    c.ty_isize = c.make_type(TypeKind::Isize, "isize");
    c.ty_u8 = c.make_type(TypeKind::U8, "u8");
    c.ty_u16 = c.make_type(TypeKind::U16, "u16");
    c.ty_u32 = c.make_type(TypeKind::U32, "u32");
    c.ty_u64 = c.make_type(TypeKind::U64, "u64");
    c.ty_usize = c.make_type(TypeKind::Usize, "usize");
    c.ty_f32 = c.make_type(TypeKind::F32, "f32");
    c.ty_f64 = c.make_type(TypeKind::F64, "f64");
    c.ty_char = c.make_type(TypeKind::Char, "char");
    c.ty_str = c.make_type(TypeKind::Str, "str");
    c.ty_cstr = c.make_type(TypeKind::CStr, "cstr");
    c.ty_untyped = c.make_type(TypeKind::UntypedInt, "<integer>");
    c.ty_void = c.make_type(TypeKind::Void, "void");
    c.ty_err = c.make_type(TypeKind::ErrorVal, "Error");
    c.ty_errcode = c.make_type(TypeKind::ErrorCode, "ErrorCode");
    c.ty_alloc = c.make_type(TypeKind::Allocator, "Allocator");
    c.ty_fmt = c.make_type(TypeKind::Fmt, "fmt");
    c.check_module(module);
    return diagnostics.empty();
}

bool check_program(const vector<Node*>& modules, Arena& arena, DiagnosticBag& diagnostics,
                   string_view path) {
    if (modules.empty()) {
        return false;
    }
    Checker c;
    c.arena = &arena;
    c.diag = &diagnostics;
    c.path = string(path);
    c.ty_error = c.make_type(TypeKind::Error, "");
    c.ty_never = c.make_type(TypeKind::Never, "never");
    c.ty_unit = c.make_type(TypeKind::Unit, "unit");
    c.ty_bool = c.make_type(TypeKind::Bool, "bool");
    c.ty_i8 = c.make_type(TypeKind::I8, "i8");
    c.ty_i16 = c.make_type(TypeKind::I16, "i16");
    c.ty_i32 = c.make_type(TypeKind::I32, "i32");
    c.ty_i64 = c.make_type(TypeKind::I64, "i64");
    c.ty_isize = c.make_type(TypeKind::Isize, "isize");
    c.ty_u8 = c.make_type(TypeKind::U8, "u8");
    c.ty_u16 = c.make_type(TypeKind::U16, "u16");
    c.ty_u32 = c.make_type(TypeKind::U32, "u32");
    c.ty_u64 = c.make_type(TypeKind::U64, "u64");
    c.ty_usize = c.make_type(TypeKind::Usize, "usize");
    c.ty_f32 = c.make_type(TypeKind::F32, "f32");
    c.ty_f64 = c.make_type(TypeKind::F64, "f64");
    c.ty_char = c.make_type(TypeKind::Char, "char");
    c.ty_str = c.make_type(TypeKind::Str, "str");
    c.ty_cstr = c.make_type(TypeKind::CStr, "cstr");
    c.ty_untyped = c.make_type(TypeKind::UntypedInt, "<integer>");
    c.ty_void = c.make_type(TypeKind::Void, "void");
    c.ty_err = c.make_type(TypeKind::ErrorVal, "Error");
    c.ty_errcode = c.make_type(TypeKind::ErrorCode, "ErrorCode");
    c.ty_alloc = c.make_type(TypeKind::Allocator, "Allocator");
    c.ty_fmt = c.make_type(TypeKind::Fmt, "fmt");
    c.program_modules = modules;
    for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
        if (modules[static_cast<size_t>(i)] != nullptr) {
            c.check_module(modules[static_cast<size_t>(i)]);
        }
    }
    return diagnostics.empty();
}

} // namespace lucb
