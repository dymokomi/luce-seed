#include "emit/emit.h"
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
        line("static const " + vt_type_name(iface) + " " + iname +
             " __attribute__((unused)) = {");
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

auto Emitter::emit_writer_rt() -> void {
        if (wrote_writer_rt) {
            return;
        }
        wrote_writer_rt = true;
        line("typedef struct lb_vt_Writer {");
        indent++;
        line("lb_r_usize (*write)(void* self, lb_cspan);");
        indent--;
        line("} lb_vt_Writer;");
        line("static lb_r_usize lb_file_write(void* self, lb_cspan bytes) {");
        indent++;
        line("FILE* f = (FILE*)self;");
        line("size_t n = bytes.length == 0 ? 0 : fwrite(bytes.data, 1, bytes.length, f);");
        line("fflush(f);");
        line("lb_r_usize r; r.failed = false; r.value = n; return r;");
        indent--;
        line("}");
        line("static const lb_vt_Writer lb_vt_file __attribute__((unused)) = { .write = lb_file_write };");
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

auto Emitter::emit_sig(Node* fn, Node* owner, bool define) -> void {
        string ret = fn_c_ret(fn);
        string name = func_ident(fn, owner);
        string sig = ret + " " + name + "(";
        bool first = true;
        if (owner != nullptr && (fn->flags & FlagStatic) == 0) {
            if ((fn->flags & FlagMutating) != 0 || fn->text == "init") {
                sig += struct_ident(owner) + "* self";
            } else {
                sig += "const " + struct_ident(owner) + "* self";
            }
            first = false;
        }
        for (Node* p = fn->right; p != nullptr; p = p->next) {
            if (!first) {
                sig += ", ";
            }
            first = false;
            if ((fn->flags & FlagExport) != 0 && is_span(p->ty)) {
                string q = p->ty->is_const ? "const " : "";
                sig += q + c_type(p->ty->elem) + "* " + string(p->text) + ", size_t " +
                       string(p->text) + "_len";
            } else {
                sig += c_type(p->ty) + " " + ident("lb_", p->text);
            }
        }
        if (first) {
            sig += "void";
        }
        sig += ")";
        if (!define) {
            line(sig + ";");
            return;
        }
        line(sig + " {");
        indent++;
        if ((fn->flags & FlagExport) != 0) {
            for (Node* p = fn->right; p != nullptr; p = p->next) {
                if (!is_span(p->ty)) {
                    continue;
                }
                string pn = ident("lb_", p->text);
                string data = string(p->text);
                string len = data + "_len";
                string cast = p->ty->is_const ? "(const void*)" : "(void*)";
                line(c_type(p->ty) + " " + pn + " = { " + data + " != NULL ? " + cast + data +
                     " : " + cast + "8, " + data + " != NULL ? " + len + " : 0 };");
            }
            if (owner != nullptr && (fn->flags & FlagStatic) == 0) {
                line("if (self == NULL) lb_trap(\"null_foreign\");");
            }
            for (Node* p = fn->right; p != nullptr; p = p->next) {
                if (needs_null_foreign(p->ty)) {
                    line("if (" + ident("lb_", p->text) + " == NULL) lb_trap(\"null_foreign\");");
                }
            }
        }
        Node* saved_fn = current_fn;
        current_fn = fn;
        scopes.reserve(64);
        scopes.push_back(Scope{});
        if (fn->body != nullptr && fn->body->kind == NodeKind::Block) {
            for (Node* s = fn->body->body; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        } else {
            emit_stmt(fn->body);
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        if ((fn->flags & FlagFallible) != 0 &&
            (fn->ty == nullptr || fn->ty->kind == TypeKind::Unit)) {
            line("return ((lb_r_unit){ .failed = false });");
        } else if (ret != "void") {
            line("lb_trap(\"unreachable\");");
        }
        current_fn = saved_fn;
        indent--;
        line("}");
        out += '\n';
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
        emit_arrays_of_decl(st);
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
        emit_arrays_of_decl(un);
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
        emit_arrays_of_decl(en);
    }

auto Emitter::emit_global(Node* g) -> void {
        string tl = (g->flags & FlagThreadLocal) != 0 ? "_Thread_local " : "";
        string ty = c_type(g->ty);
        string name = ident("lb_", g->text);
        if (g->flags & FlagUninit) {
            line(tl + ty + " " + name + ";");
            return;
        }
        string init = "0";
        if (g->left != nullptr) {
            init = emit_expr(g->left);
        } else if (g->ty != nullptr &&
                   (g->ty->kind == TypeKind::Struct || g->ty->kind == TypeKind::Union ||
                    g->ty->kind == TypeKind::Enum || is_array(g->ty) || is_opt(g->ty) ||
                    is_span(g->ty) || (g->ty->kind == TypeKind::Str) ||
                    g->ty->kind == TypeKind::Allocator ||
                    g->ty->kind == TypeKind::Interface)) {
            init = "{0}";
        }
        line(tl + ty + " " + name + " = " + init + ";");
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
        if (payload == nullptr || payload->kind == TypeKind::Unit ||
            payload->kind == TypeKind::Never) {
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
        if (t->kind == TypeKind::Pointer || t->kind == TypeKind::Span ||
            t->kind == TypeKind::Atomic) {
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
            (n->kind == NodeKind::Struct || n->kind == NodeKind::Func ||
             n->kind == NodeKind::Enum || n->kind == NodeKind::Union)) {
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
        if (is_array(t->elem)) {
            emit_array_def(t->elem);
        }
        line("typedef struct " + array_c_name(t) + " { " + c_type(t->elem) + " d[" +
             std::to_string(t->length) + "]; } " + array_c_name(t) + ";");
        arrays_done.push_back(t);
    }

auto Emitter::emit_arrays_of_decl(Node* st) -> void {
        if (st == nullptr) {
            return;
        }
        bool any = false;
        for (size_t i = 0; i < arrays.size(); i++) {
            Type* t = arrays[i];
            Type* e = t != nullptr ? t->elem : nullptr;
            if (e != nullptr && e->decl == st) {
                size_t before = arrays_done.size();
                emit_array_def(t);
                if (arrays_done.size() != before) {
                    any = true;
                }
            }
        }
        if (any) {
            out += '\n';
        }
    }

auto Emitter::emit_array_typedefs(bool funcs, bool records) -> void {
        bool any = false;
        for (size_t i = 0; i < arrays.size(); i++) {
            Type* t = arrays[i];
            if (is_func(t->elem) != funcs) {
                continue;
            }
            if (!records && array_elem_is_record(t)) {
                continue;
            }
            size_t before = arrays_done.size();
            emit_array_def(t);
            if (arrays_done.size() != before) {
                any = true;
            }
        }
        if (any) {
            out += '\n';
        }
    }

auto Emitter::emit_tup_typedefs() -> void {
        for (size_t i = 0; i < tups.size(); i++) {
            Type* t = tups[i];
            string s = "typedef struct " + tup_c_name(t) + " {";
            for (int j = 0; j < t->ntargs; j++) {
                s += " " + c_type(t->args[j]) + " a" + std::to_string(j) + ";";
            }
            s += " } " + tup_c_name(t) + ";";
            line(s);
        }
        if (!tups.empty()) {
            out += '\n';
        }
    }

auto Emitter::emit_fn_typedefs() -> void {
        for (size_t i = 0; i < fns.size(); i++) {
            Type* t = fns[i];
            string ret = "void";
            if (t->elem != nullptr && t->elem->kind != TypeKind::Unit &&
                t->elem->kind != TypeKind::Never) {
                ret = c_type(t->elem);
            }
            string s = "typedef " + ret + " (*" + fn_c_name(t) + ")(";
            if (t->ntargs == 0) {
                s += "void";
            } else {
                for (int j = 0; j < t->ntargs; j++) {
                    if (j != 0) {
                        s += ", ";
                    }
                    s += c_type(t->args[j]);
                }
            }
            s += ");";
            line(s);
        }
        if (!fns.empty()) {
            out += '\n';
        }
    }

auto Emitter::emit_opt_typedefs() -> void {
        for (size_t i = 0; i < opts.size(); i++) {
            Type* t = opts[i];
            line("LB_OPT(" + c_type(t->elem) + ", " + opt_c_name(t) + ");");
        }
        for (size_t i = 0; i < fails.size(); i++) {
            Type* t = fails[i];
            line("LB_RES(" + c_type(t) + ", " + fail_c_name(t) + ");");
        }
        if (!opts.empty() || !fails.empty()) {
            out += '\n';
        }
        bool has_usize = false;
        for (size_t i = 0; i < fails.size(); i++) {
            if (fail_c_name(fails[i]) == "lb_r_usize") {
                has_usize = true;
            }
        }
        if (!has_usize) {
            line("LB_RES(size_t, lb_r_usize);");
            out += '\n';
        }
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

auto Emitter::emit_types(Node* mod) -> void {
        if (mod == nullptr) {
            return;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::Struct || d->kind == NodeKind::ExternStruct) {
                emit_struct(d);
            } else if (d->kind == NodeKind::Union || d->kind == NodeKind::ExternUnion) {
                emit_union(d);
            } else if (d->kind == NodeKind::Enum) {
                emit_enum(d);
            }
        }
    }

auto Emitter::emit_decls(Node* mod) -> void {
        if (mod == nullptr) {
            return;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::ExternFunc) {
                string ret = d->ty != nullptr && d->ty->kind != TypeKind::Unit ? c_type(d->ty)
                                                                              : "void";
                string sig = "extern " + ret + " " + func_ident(d, nullptr) + "(";
                bool first = true;
                bool variadic = false;
                for (Node* p = d->right; p != nullptr; p = p->next) {
                    if (p->flags & FlagVariadic) {
                        variadic = true;
                        break;
                    }
                    if (!first) {
                        sig += ", ";
                    }
                    first = false;
                    sig += c_type(p->ty);
                }
                if (variadic) {
                    if (!first) {
                        sig += ", ";
                    }
                    sig += "...";
                }
                if (first && !variadic) {
                    sig += "void";
                }
                sig += ");";
                line(sig);
            } else if (d->kind == NodeKind::ExternVar) {
                line("extern " + c_type(d->ty) + " " + string(d->text) + ";");
            } else if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, false);
            } else if (d->kind == NodeKind::Struct || d->kind == NodeKind::Enum ||
                       d->kind == NodeKind::Union) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, false);
                    }
                }
            } else if (d->kind == NodeKind::Test) {
                emit_test_sig(d, false);
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Global || d->kind == NodeKind::Const) {
                emit_global(d);
            }
        }
    }

auto Emitter::emit_defs(Node* mod) -> void {
        if (mod == nullptr) {
            return;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::Struct || d->kind == NodeKind::Enum ||
                d->kind == NodeKind::Union) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func) {
                        emit_sig(m, d, true);
                    }
                }
            }
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->left != nullptr && d->left->kind == NodeKind::GenericParam) {
                continue;
            }
            if (d->kind == NodeKind::Func) {
                emit_sig(d, nullptr, true);
            } else if (d->kind == NodeKind::Test) {
                emit_test_sig(d, true);
            }
        }
    }

auto Emitter::emit_test_sig(Node* t, bool define) -> void {
        string name = "lb_test_" + std::to_string(reinterpret_cast<uintptr_t>(t) & 0xffffu);
        if (!define) {
            line("lb_r_unit " + name + "(void);");
            return;
        }
        line("lb_r_unit " + name + "(void) {");
        indent++;
        Node* saved = current_fn;
        current_fn = t;
        t->flags |= FlagFallible;
        t->ty = nullptr;
        scopes.push_back(Scope{});
        if (t->body != nullptr && t->body->kind == NodeKind::Block) {
            for (Node* s = t->body->body; s != nullptr; s = s->next) {
                emit_stmt(s);
            }
        } else {
            emit_stmt(t->body);
        }
        if (!scopes.empty()) {
            unwind_scope(scopes.back());
            scopes.pop_back();
        }
        line("return ((lb_r_unit){ .failed = false });");
        current_fn = saved;
        indent--;
        line("}");
        out += '\n';
    }

auto Emitter::find_main(Node* mod) -> Node* {
        if (mod == nullptr) {
            return nullptr;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && d->text == "main") {
                return d;
            }
        }
        return nullptr;
    }

auto Emitter::find_answer(Node* mod) -> Node* {
        if (mod == nullptr) {
            return nullptr;
        }
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && d->text == "answer") {
                return d;
            }
        }
        return nullptr;
    }

auto Emitter::emit_answer_unwrap(Node* mod) -> void {
        Node* fn = find_answer(mod);
        if (fn == nullptr || (fn->flags & FlagFallible) == 0) {
            return;
        }
        line("int64_t lb_answer(void) {");
        indent++;
        line(fail_c_name(fn->ty) + " r = lb_answer_impl();");
        line("if (r.failed) {");
        indent++;
        line("fprintf(stderr, \"error %d: %.*s\\n\", r.error.code, (int)r.error.message.length, "
             "r.error.message.data);");
        line("exit(1);");
        indent--;
        line("}");
        line("return r.value;");
        indent--;
        line("}");
        out += '\n';
    }

auto Emitter::emit_c_main(Node* fn) -> void {
        bool fail = fn != nullptr && (fn->flags & FlagFallible) != 0;
        Type* at = fn != nullptr && fn->right != nullptr ? fn->right->ty : nullptr;
        bool cstr = at != nullptr && is_span(at) && at->elem != nullptr &&
                    at->elem->kind == TypeKind::CStr;
        out += "int main(int argc, char** argv) {\n";
        out += "    lb_set_alloc(lb_heap_alloc());\n";
        if (cstr) {
            string aty = c_type(at);
            const char* cast = aty == "lb_cspan" ? "(const void*)" : "(void*)";
            out += "    " + aty + " args = { " + cast + "argv, (size_t)argc };\n";
            if (fail) {
                out += "    lb_r_i32 r = lb_main(args);\n";
                out += "    if (r.failed) { fprintf(stderr, \"error %d: %.*s\\n\", r.error.code, "
                       "(int)r.error.message.length, r.error.message.data); return 1; }\n";
                out += "    return (int)r.value;\n";
            } else {
                out += "    return (int)lb_main(args);\n";
            }
        } else {
            out += "    lb_str* items = (lb_str*)malloc((size_t)argc * sizeof(lb_str));\n";
            out += "    if (items == NULL) { return 1; }\n";
            out += "    for (int i = 0; i < argc; i++) {\n";
            out += "        size_t n = 0; while (argv[i][n]) n++;\n";
            out += "        lb_check_utf8(argv[i], n);\n";
            out += "        items[i].data = argv[i]; items[i].length = n;\n";
            out += "    }\n";
            out += "    lb_span args = { items, (size_t)argc };\n";
            if (fail) {
                out += "    lb_r_i32 r = lb_main(args);\n";
                out += "    int code = r.failed ? 1 : (int)r.value;\n";
                out += "    if (r.failed) { fprintf(stderr, \"error %d: %.*s\\n\", r.error.code, "
                       "(int)r.error.message.length, r.error.message.data); }\n";
                out += "    free(items);\n";
                out += "    return code;\n";
            } else {
                out += "    int code = (int)lb_main(args);\n";
                out += "    free(items);\n";
                out += "    return code;\n";
            }
        }
        out += "}\n";
    }

auto Emitter::emit_module(Node* mod) -> void {
        if (mod != nullptr && !mod->text.empty()) {
            src_file = string(mod->text);
        }
        out += "/* generated by lucb */\n";
        out += "#include \"lucb_rt.h\"\n";
        out += "#include <stdio.h>\n";
        out += "#include <stdlib.h>\n";
        out += "#include <string.h>\n";
        out += "#include <stdatomic.h>\n";
        out += "#include <pthread.h>\n";
        out += "#include <sched.h>\n";
        out += "#include <time.h>\n";
        out += "#include <unistd.h>\n";
        out += "typedef struct lb_Handle { size_t id; } lb_Handle;\n\n";
        arrays.clear();
        arrays_done.clear();
        opts.clear();
        fails.clear();
        tups.clear();
        fns.clear();
        noted_structs.clear();
        wrote_writer_rt = false;
        collect_from(mod);
        emit_type_forwards(mod);
        emit_array_typedefs(false);
        emit_tup_typedefs();
        emit_types(mod);
        emit_array_typedefs(false, true);
        emit_opt_typedefs();
        emit_fn_typedefs();
        emit_array_typedefs(true);
        emit_decls(mod);
        emit_ifaces(mod);
        out += '\n';
        emit_defs(mod);
        emit_answer_unwrap(mod);
        Node* main_fn = find_main(mod);
        if (main_fn != nullptr) {
            emit_c_main(main_fn);
        }
    }

auto Emitter::emit_many(const vector<Node*>& modules, Node* entry) -> void {
        if (entry != nullptr && !entry->text.empty()) {
            src_file = string(entry->text);
        }
        out += "/* generated by lucb */\n";
        out += "#include \"lucb_rt.h\"\n";
        out += "#include <stdio.h>\n";
        out += "#include <stdlib.h>\n";
        out += "#include <string.h>\n";
        out += "#include <stdatomic.h>\n";
        out += "#include <pthread.h>\n";
        out += "#include <sched.h>\n";
        out += "#include <time.h>\n";
        out += "#include <unistd.h>\n";
        out += "typedef struct lb_Handle { size_t id; } lb_Handle;\n\n";
        arrays.clear();
        arrays_done.clear();
        opts.clear();
        fails.clear();
        tups.clear();
        fns.clear();
        noted_structs.clear();
        wrote_writer_rt = false;
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            collect_from(modules[static_cast<size_t>(i)]);
        }
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_type_forwards(modules[static_cast<size_t>(i)]);
        }
        emit_array_typedefs(false);
        emit_tup_typedefs();
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_types(modules[static_cast<size_t>(i)]);
        }
        emit_array_typedefs(false, true);
        emit_opt_typedefs();
        emit_fn_typedefs();
        emit_array_typedefs(true);
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_decls(modules[static_cast<size_t>(i)]);
        }
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_ifaces(modules[static_cast<size_t>(i)]);
        }
        out += '\n';
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            emit_defs(modules[static_cast<size_t>(i)]);
        }
        emit_answer_unwrap(entry);
        Node* main_fn = find_main(entry);
        if (main_fn != nullptr) {
            emit_c_main(main_fn);
        }
    }

auto Emitter::emit_header_mod(Node* mod) -> void {
        out += "/* generated by lucb */\n";
        out += "#pragma once\n";
        out += "#include <stdbool.h>\n";
        out += "#include <stddef.h>\n";
        out += "#include <stdint.h>\n\n";
        if (mod == nullptr) {
            return;
        }
        emit_types(mod);
        for (Node* d = mod->body; d != nullptr; d = d->next) {
            if (d->kind == NodeKind::Func && (d->flags & FlagExport) != 0) {
                emit_sig(d, nullptr, false);
            } else if (d->kind == NodeKind::Struct) {
                for (Node* m = d->body; m != nullptr; m = m->next) {
                    if (m->kind == NodeKind::Func && (m->flags & FlagExport) != 0) {
                        emit_sig(m, d, false);
                    }
                }
            }
        }
    }

string emit_c(Node* module) {
    Emitter e;
    if (module != nullptr) {
        e.emit_module(module);
    }
    return e.out;
}

string emit_program(const vector<Node*>& modules, Node* entry) {
    Emitter e;
    if (!modules.empty()) {
        e.emit_many(modules, entry != nullptr ? entry : modules[0]);
    } else if (entry != nullptr) {
        e.emit_module(entry);
    }
    return e.out;
}

string emit_header(Node* module) {
    Emitter e;
    e.emit_header_mod(module);
    return e.out;
}

} // namespace lucb
