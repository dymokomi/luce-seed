//==============================================================================================
//
//   emit/types - C type definitions in dependency order
//
//   DESCRIPTION:
//       Every C typedef a program needs: records, tagged enums, unions, fixed arrays, tuples,
//       optionals, results, function pointers, and the interface view and witness tables. A
//       definition is written only after every type it holds by value, so field order and
//       module order never matter (base.md §5, §10, §14).
//
//       The collectors (`note_*`, `walk_types`) discover the types a module mentions;
//       `define_type` and `define_decl` write them; `emit_type_defs` is the one entry point
//       the module emitter calls between the forward declarations and the function
//       prototypes.
//
//==============================================================================================

#include "emit/emitter.h"

#include "support/literal.h"

namespace lucb {

auto Emitter::emit_iface_typedef(Node* iface) -> void {
    if (iface == nullptr || iface->kind != NodeKind::Interface) {
        return;
    }
    line("typedef struct " + vt_type_name(iface->ty) + " {");
    indent++;
    for (Node* m = iface->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Func) {
            continue;
        }
        string sig = fn_c_ret(m) + " (*" + string(m->text) + ")(void* self";
        for (Node* p = m->right; p != nullptr; p = p->next) {
            sig += ", " + c_type(p->ty);
        }
        sig += ");";
        line(sig);
    }
    indent--;
    line("} " + vt_type_name(iface->ty) + ";");
    out += '\n';
}

auto Emitter::emit_vtable(Node* st, Node* iface_type_node) -> void {
    Type* iface = iface_type_node != nullptr ? iface_type_node->ty : nullptr;
    if (st == nullptr || iface == nullptr || iface->decl == nullptr) {
        return;
    }
    string iname = vt_instance_name(st, iface->decl);
    line("static const " + vt_type_name(iface) + " " + iname + " __attribute__((unused)) = {");
    indent++;
    bool first = true;
    for (Node* m = iface->decl->body; m != nullptr; m = m->next) {
        if (m->kind != NodeKind::Func) {
            continue;
        }
        Node* impl = nullptr;
        for (Node* sm = st->body; sm != nullptr; sm = sm->next) {
            if (sm->kind == NodeKind::Func && sm->text == m->text) {
                impl = sm;
                break;
            }
        }
        string fn = impl != nullptr ? func_ident(impl, st) : "NULL";
        string cast = "(" + fn_c_ret(m) + " (*)(void*";
        for (Node* p = m->right; p != nullptr; p = p->next) {
            cast += ", " + c_type(p->ty);
        }
        cast += "))";
        if (!first) {
            // already commas on lines
        }
        first = false;
        line("." + string(m->text) + " = " + cast + fn + ",");
    }
    indent--;
    line("};");
    out += '\n';
}

auto Emitter::emit_ifaces(Node* mod) -> void {
    if (mod == nullptr) {
        return;
    }
    bool need_writer = false;
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Interface) {
            emit_iface_typedef(d);
        }
        if (d->kind != NodeKind::Struct) {
            continue;
        }
        for (Node* t = d->right; t != nullptr; t = t->next) {
            if (t->ty != nullptr && t->ty->kind == TypeKind::Interface && t->ty->decl != nullptr &&
                t->ty->decl->text == "Writer") {
                need_writer = true;
            }
        }
    }
    emit_writer_rt();
    (void)need_writer;
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind != NodeKind::Struct) {
            continue;
        }
        for (Node* t = d->right; t != nullptr; t = t->next) {
            if (t->ty != nullptr && t->ty->kind == TypeKind::Interface) {
                emit_vtable(d, t);
            }
        }
    }
}

auto Emitter::type_attrs(Node* n) -> string {
    string a;
    if (n != nullptr && (n->flags & FlagPacked) != 0) {
        a += " __attribute__((packed))";
    }
    uint64_t al = 0;
    if (n != nullptr && n->type != nullptr && n->type->kind == NodeKind::Literal &&
        n->type->op == TokenKind::IntLit) {
        ParsedInt p = parse_int_literal(n->type->text);
        if (p.ok) {
            al = p.value;
        }
    }
    if (al != 0) {
        a += " __attribute__((aligned(" + std::to_string(al) + ")))";
    }
    return a;
}

auto Emitter::emit_struct(Node* st) -> void {
    line("typedef struct" + type_attrs(st) + " " + struct_ident(st) + " {");
    indent++;
    bool any = false;
    for (Node* m = st->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            string fa;
            uint64_t al = 0;
            if (m->right != nullptr && m->right->kind == NodeKind::Literal &&
                m->right->op == TokenKind::IntLit) {
                ParsedInt p = parse_int_literal(m->right->text);
                if (p.ok) {
                    al = p.value;
                }
            }
            if (al != 0) {
                fa = " __attribute__((aligned(" + std::to_string(al) + ")))";
            }
            line(c_type(m->ty) + " " + string(m->text) + fa + ";");
            any = true;
        }
    }
    if (!any) {
        line("int unused;");
    }
    indent--;
    line("} " + struct_ident(st) + ";");
    out += '\n';
}

auto Emitter::emit_union(Node* un) -> void {
    line("typedef union " + struct_ident(un) + " {");
    indent++;
    bool any = false;
    for (Node* m = un->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            line(c_type(m->ty) + " " + string(m->text) + ";");
            any = true;
        }
    }
    if (!any) {
        line("int unused;");
    }
    indent--;
    line("} " + struct_ident(un) + ";");
    out += '\n';
}

auto Emitter::emit_enum(Node* en) -> void {
    if (is_int_enum(en->ty)) {
        return;
    }
    line("typedef struct " + struct_ident(en) + " {");
    indent++;
    line("int32_t tag;");
    bool any_payload = false;
    for (Node* c = en->body; c != nullptr; c = c->next) {
        if (c->kind == NodeKind::EnumCase && c->body != nullptr) {
            any_payload = true;
            break;
        }
    }
    if (any_payload) {
        line("union {");
        indent++;
        for (Node* c = en->body; c != nullptr; c = c->next) {
            if (c->kind != NodeKind::EnumCase || c->body == nullptr) {
                continue;
            }
            line("struct {");
            indent++;
            for (Node* p = c->body; p != nullptr; p = p->next) {
                line(c_type(p->ty) + " " + string(p->text) + ";");
            }
            indent--;
            line("} " + string(c->text) + ";");
        }
        indent--;
        line("} u;");
    }
    indent--;
    line("} " + struct_ident(en) + ";");
    out += '\n';
}

auto Emitter::note_opt(Type* t) -> void {
    if (t == nullptr) {
        return;
    }
    for (size_t i = 0; i < opts.size(); i++) {
        if (opts[i] == t) {
            return;
        }
    }
    opts.push_back(t);
}

auto Emitter::note_fail(Type* payload) -> void {
    if (payload == nullptr || payload->kind == TypeKind::Unit || payload->kind == TypeKind::Never) {
        return;
    }
    for (size_t i = 0; i < fails.size(); i++) {
        if (fails[i] == payload) {
            return;
        }
    }
    fails.push_back(payload);
}

auto Emitter::note_tup(Type* t) -> void {
    if (t == nullptr || t->kind != TypeKind::Tuple) {
        return;
    }
    for (int i = 0; i < t->ntargs; i++) {
        note_type(t->args[i]);
    }
    for (size_t i = 0; i < tups.size(); i++) {
        if (tups[i] == t) {
            return;
        }
    }
    tups.push_back(t);
}

auto Emitter::note_fn(Type* t) -> void {
    if (t == nullptr || t->kind != TypeKind::Func) {
        return;
    }
    for (int i = 0; i < t->ntargs; i++) {
        note_type(t->args[i]);
    }
    note_type(t->elem);
    for (size_t i = 0; i < fns.size(); i++) {
        if (fns[i] == t) {
            return;
        }
    }
    fns.push_back(t);
}

auto Emitter::note_type(Type* t) -> void {
    if (t == nullptr || t->kind == TypeKind::Param) {
        return;
    }
    if (t->kind == TypeKind::Array) {
        note_type(t->elem);
        for (size_t i = 0; i < arrays.size(); i++) {
            if (arrays[i] == t) {
                return;
            }
        }
        arrays.push_back(t);
        return;
    }
    if (is_opt(t)) {
        note_type(t->elem);
        if (t->elem != nullptr && t->elem->kind != TypeKind::Param) {
            note_opt(t);
        }
        return;
    }
    if (is_fail(t)) {
        note_type(t->elem);
        if (t->elem != nullptr && t->elem->kind != TypeKind::Param) {
            note_fail(t->elem);
        }
        return;
    }
    if (t->kind == TypeKind::Pointer || t->kind == TypeKind::Span || t->kind == TypeKind::Atomic) {
        note_type(t->elem);
    }
    if (t->kind == TypeKind::Tuple) {
        note_tup(t);
    }
    if (t->kind == TypeKind::Func) {
        note_fn(t);
    }
    if (t->kind == TypeKind::Struct && t->decl != nullptr) {
        for (size_t i = 0; i < noted_structs.size(); i++) {
            if (noted_structs[i] == t) {
                return;
            }
        }
        noted_structs.push_back(t);
        for (Node* m = t->decl->body; m != nullptr; m = m->next) {
            if (m->kind == NodeKind::Field) {
                note_type(m->ty);
            }
        }
    }
}

auto Emitter::walk_types(Node* n) -> void {
    if (n == nullptr) {
        return;
    }
    if (n->left != nullptr && n->left->kind == NodeKind::GenericParam &&
        (n->kind == NodeKind::Struct || n->kind == NodeKind::Func || n->kind == NodeKind::Enum ||
         n->kind == NodeKind::Union)) {
        walk_types(n->next);
        return;
    }
    note_type(n->ty);
    walk_types(n->left);
    walk_types(n->right);
    walk_types(n->body);
    walk_types(n->type);
    walk_types(n->next);
}

auto Emitter::emit_type_forwards(Node* mod) -> void {
    if (mod == nullptr) {
        return;
    }
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
            continue;
        }
        if (d->kind == NodeKind::Struct || d->kind == NodeKind::ExternStruct) {
            string n = struct_ident(d);
            line("typedef struct " + n + " " + n + ";");
        } else if (d->kind == NodeKind::Union || d->kind == NodeKind::ExternUnion) {
            string n = struct_ident(d);
            line("typedef union " + n + " " + n + ";");
        } else if (d->kind == NodeKind::Enum && d->ty != nullptr && !is_int_enum(d->ty)) {
            string n = struct_ident(d);
            line("typedef struct " + n + " " + n + ";");
        }
    }
}

auto Emitter::array_elem_is_record(Type* t) -> bool {
    Type* e = t != nullptr ? t->elem : nullptr;
    while (e != nullptr && is_array(e)) {
        e = e->elem;
    }
    if (e == nullptr) {
        return false;
    }
    return e->kind == TypeKind::Struct || e->kind == TypeKind::Union ||
           (e->kind == TypeKind::Enum && !is_int_enum(e));
}

auto Emitter::emit_array_def(Type* t) -> void {
    if (t == nullptr || t->kind != TypeKind::Array) {
        return;
    }
    for (size_t i = 0; i < arrays_done.size(); i++) {
        if (arrays_done[i] == t) {
            return;
        }
    }
    if (typedef_done(array_c_name(t))) {
        arrays_done.push_back(t);
        return;
    }
    define_type(t->elem);
    line("typedef struct " + array_c_name(t) + " { " + c_type(t->elem) + " d[" +
         std::to_string(t->length) + "]; } " + array_c_name(t) + ";");
    arrays_done.push_back(t);
    typedefs_done.push_back(array_c_name(t));
}

auto Emitter::typedef_done(const string& name) -> bool {
    for (size_t i = 0; i < typedefs_done.size(); i++) {
        if (typedefs_done[i] == name) {
            return true;
        }
    }
    return false;
}

// Write the typedef for `t` after everything it holds by value.
auto Emitter::define_type(Type* t) -> void {
    if (t == nullptr || t->kind == TypeKind::Param) {
        return;
    }
    if (t->kind == TypeKind::Pointer || t->kind == TypeKind::Span || t->kind == TypeKind::Atomic) {
        // Only the pointee's *name* is needed; records are forward-declared,
        // but an array/tuple/optional pointee is a typedef that must exist.
        Type* e = t->elem;
        if (e != nullptr && e->kind != TypeKind::Struct && e->kind != TypeKind::Union &&
            e->kind != TypeKind::Enum) {
            define_type(e);
        }
        return;
    }
    if (t->kind == TypeKind::Array) {
        emit_array_def(t);
        return;
    }
    if (is_opt(t)) {
        define_type(t->elem);
        if (t->elem == nullptr || t->elem->kind == TypeKind::Param) {
            return;
        }
        string name = opt_c_name(t);
        if (typedef_done(name)) {
            return;
        }
        line("LB_OPT(" + c_type(t->elem) + ", " + name + ");");
        typedefs_done.push_back(name);
        return;
    }
    if (is_fail(t)) {
        define_fail(t->elem);
        return;
    }
    if (t->kind == TypeKind::Tuple) {
        for (int i = 0; i < t->ntargs; i++) {
            define_type(t->args[i]);
        }
        string name = tup_c_name(t);
        if (typedef_done(name)) {
            return;
        }
        string d = "typedef struct " + name + " {";
        for (int j = 0; j < t->ntargs; j++) {
            d += " " + c_type(t->args[j]) + " a" + std::to_string(j) + ";";
        }
        line(d + " } " + name + ";");
        typedefs_done.push_back(name);
        return;
    }
    if (t->kind == TypeKind::Func) {
        for (int i = 0; i < t->ntargs; i++) {
            define_type(t->args[i]);
        }
        define_type(t->elem);
        string name = fn_c_name(t);
        if (typedef_done(name)) {
            return;
        }
        string ret = "void";
        if (t->elem != nullptr && t->elem->kind != TypeKind::Unit &&
            t->elem->kind != TypeKind::Never) {
            ret = c_type(t->elem);
        }
        string d = "typedef " + ret + " (*" + name + ")(";
        if (t->ntargs == 0) {
            d += "void";
        } else {
            for (int j = 0; j < t->ntargs; j++) {
                if (j != 0) {
                    d += ", ";
                }
                d += c_type(t->args[j]);
            }
        }
        line(d + ");");
        typedefs_done.push_back(name);
        return;
    }
    if ((t->kind == TypeKind::Struct || t->kind == TypeKind::Union || t->kind == TypeKind::Enum) &&
        t->decl != nullptr) {
        define_decl(t->decl);
    }
}

auto Emitter::define_fail(Type* payload) -> void {
    if (payload == nullptr || payload->kind == TypeKind::Unit || payload->kind == TypeKind::Never ||
        payload->kind == TypeKind::Param) {
        return;
    }
    define_type(payload);
    string name = fail_c_name(payload);
    if (typedef_done(name)) {
        return;
    }
    line("LB_RES(" + c_type(payload) + ", " + name + ");");
    typedefs_done.push_back(name);
}

// Write one record after the types its members hold by value.
auto Emitter::define_decl(Node* d) -> void {
    if (d == nullptr) {
        return;
    }
    if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
        return;
    }
    if (d->kind == NodeKind::Enum && d->ty != nullptr && is_int_enum(d->ty)) {
        return;
    }
    if ((d->flags & FlagBuiltin) != 0) {
        return; // standard-module records are defined by the runtime header
    }
    for (size_t i = 0; i < decls_done.size(); i++) {
        if (decls_done[i] == d) {
            return;
        }
    }
    for (size_t i = 0; i < decls_busy.size(); i++) {
        if (decls_busy[i] == d) {
            return; // a by-value cycle; the checker already refused it
        }
    }
    decls_busy.push_back(d);
    for (Node* m = d->body; m != nullptr; m = m->next) {
        if (m->kind == NodeKind::Field) {
            define_type(m->ty);
        } else if (m->kind == NodeKind::EnumCase) {
            for (Node* p = m->body; p != nullptr; p = p->next) {
                define_type(p->ty);
            }
        }
    }
    decls_busy.pop_back();
    decls_done.push_back(d);
    if (d->kind == NodeKind::Struct || d->kind == NodeKind::ExternStruct) {
        emit_struct(d);
    } else if (d->kind == NodeKind::Union || d->kind == NodeKind::ExternUnion) {
        emit_union(d);
    } else if (d->kind == NodeKind::Enum) {
        emit_enum(d);
    }
}

auto Emitter::emit_type_defs(const vector<Node*>& modules) -> void {
    line("LB_RES(size_t, lb_r_usize);");
    typedefs_done.push_back("lb_r_usize");
    for (size_t mi = 0; mi < modules.size(); mi++) {
        Node* mod = modules[mi];
        if (mod == nullptr) {
            continue;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Struct || d->kind == NodeKind::ExternStruct ||
                d->kind == NodeKind::Union || d->kind == NodeKind::ExternUnion ||
                d->kind == NodeKind::Enum) {
                define_decl(d);
            }
        }
    }
    for (size_t i = 0; i < arrays.size(); i++) {
        define_type(arrays[i]);
    }
    for (size_t i = 0; i < tups.size(); i++) {
        define_type(tups[i]);
    }
    for (size_t i = 0; i < opts.size(); i++) {
        define_type(opts[i]);
    }
    for (size_t i = 0; i < fails.size(); i++) {
        define_fail(fails[i]);
    }
    for (size_t i = 0; i < fns.size(); i++) {
        define_type(fns[i]);
    }
    out += '\n';
}

auto Emitter::note_fail_fn(Node* fn) -> void {
    if (fn != nullptr && (fn->flags & FlagFallible) != 0) {
        note_fail(fn->ty);
    }
}

auto Emitter::collect_from(Node* mod) -> void {
    if (mod == nullptr) {
        return;
    }
    walk_types(mod);
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
            continue;
        }
        if (d->kind == NodeKind::Func) {
            note_fail_fn(d);
        } else if (d->kind == NodeKind::Struct || d->kind == NodeKind::Enum ||
                   d->kind == NodeKind::Union) {
            for (Node* m = d->body; m != nullptr; m = m->next) {
                if (m->kind == NodeKind::Func) {
                    note_fail_fn(m);
                }
            }
        }
    }
}

} // namespace lucb
