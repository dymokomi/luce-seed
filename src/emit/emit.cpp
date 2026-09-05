//==============================================================================================
//
//   emit/emit - Module, program, and header emission
//
//   DESCRIPTION:
//       The top of the C backend: the generated file's prologue, forward declarations,
//       prototypes, globals, function definitions, the `main` shim for `answer()` programs
//       and the argument shim for `main` programs, the test-runner entry, and the exported C
//       header (base.md §17.6). `emit_c`, `emit_program`, and `emit_header` are the entry
//       points.
//
//==============================================================================================

#include "emit/emit.h"
#include "emit/emitter.h"
#include "support/literal.h"

namespace lucb {

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
    line("static const lb_vt_Writer lb_vt_file __attribute__((unused)) = { .write = lb_file_write "
         "};");
    out += '\n';
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
            string cast = "(void*)"; // spans carry a void* data pointer
            line(c_type(p->ty) + " " + pn + " = { " + data + " != NULL ? " + cast + data + " : " +
                 cast + "8, " + data + " != NULL ? " + len + " : 0 };");
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
    if ((fn->flags & FlagFallible) != 0 && (fn->ty == nullptr || fn->ty->kind == TypeKind::Unit)) {
        line("return ((lb_r_unit){ .failed = false });");
    } else if (ret != "void") {
        line("lb_trap(\"unreachable\");");
    }
    current_fn = saved_fn;
    indent--;
    line("}");
    out += '\n';
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
                g->ty->kind == TypeKind::Allocator || g->ty->kind == TypeKind::Interface)) {
        init = "{0}";
    }
    line(tl + ty + " " + name + " = " + init + ";");
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
            string ret = d->ty != nullptr && d->ty->kind != TypeKind::Unit ? c_type(d->ty) : "void";
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
    bool cstr =
        at != nullptr && is_span(at) && at->elem != nullptr && at->elem->kind == TypeKind::CStr;
    out += "int main(int argc, char** argv) {\n";
    out += "    lb_set_alloc(lb_heap_alloc());\n";
    if (cstr) {
        string aty = c_type(at);
        const char* cast = "(void*)";
        (void)aty;
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
    typedefs_done.clear();
    decls_done.clear();
    decls_busy.clear();
    wrote_writer_rt = false;
    collect_from(mod);
    emit_type_forwards(mod);
    emit_type_defs({mod});
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
    typedefs_done.clear();
    decls_done.clear();
    decls_busy.clear();
    wrote_writer_rt = false;
    for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
        collect_from(modules[static_cast<size_t>(i)]);
    }
    for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
        emit_type_forwards(modules[static_cast<size_t>(i)]);
    }
    {
        vector<Node*> ordered;
        for (int i = static_cast<int>(modules.size()) - 1; i >= 0; i--) {
            ordered.push_back(modules[static_cast<size_t>(i)]);
        }
        emit_type_defs(ordered);
    }
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
    // Exported records are C-representable, so their definitions need no
    // runtime macros; write them in dependency order like the program does.
    typedefs_done.clear();
    decls_done.clear();
    decls_busy.clear();
    for (Node* d = mod->body; d != nullptr; d = d->next) {
        if (d->kind == NodeKind::Struct || d->kind == NodeKind::Union ||
            d->kind == NodeKind::Enum) {
            define_decl(d);
        }
    }
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
