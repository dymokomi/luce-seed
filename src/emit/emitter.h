// C emitter state. Implementation header for emit/*.cpp.

#pragma once

#include "check/type.h"
#include "emit/cgen.h"
#include "parse/ast.h"

#include <cstdio>

namespace lucb {

struct Emitter {
    string out;
    int indent = 0;
    vector<Type*> arrays;
    vector<Type*> opts;
    vector<Type*> fails;
    vector<Type*> tups;
    vector<Type*> fns;
    vector<Type*> noted_structs;
    Node* current_fn = nullptr;
    string src_file = "t.lucb";
    bool wrote_writer_rt = false;
    void emit_writer_rt();
    string emit_src_file();
    string emit_src_function();
    string emit_src_location(Node* n);
    int temps = 0;
    string catch_var;
    string catch_done;

    struct Scope {
        vector<Node*> defers;
        bool loop = false;
        string_view label;
        bool restore_alloc = false;
        string alloc_save;
    };
    vector<Scope> scopes;

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    void pad() {
        for (int i = 0; i < indent; i++) {
            out += "    ";
        }
    }

    void line(const string& s) {
        pad();
        out += s;
        out += '\n';
    }

    int tmp() { return temps++; }

    bool produces_opt(Node* n);
    string wrap_opt(Type* t, const string& e);
    string none_opt(Type* t) { return "((" + c_type(t) + "){ .present = false })"; }

    bool fn_fallible() {
        return current_fn != nullptr && (current_fn->flags & FlagFallible) != 0;
    }

    string wrap_ok(const string& e);
    string wrap_err(const string& code, const string& msg);
    void run_defers(const vector<Node*>& d, bool failing);
    void unwind_scope(const Scope& sc, bool failing = false);
    void run_defers_from(int from, bool failing = false);
    string snapshot_defers(bool failing = false);
    bool is_error_call(Node* n);
    string emit_enum_value(Node* n);
    string emit_try(Node* n);
    string emit_else(Node* n);
    string emit_catch(Node* n);
    string emit_expr(Node* n);
    string emit_expr_inner(Node* n);
    string emit_literal(Node* n);
    string emit_unary(Node* n);
    string emit_helper(const char* name, Type* t, const string& L, const string& R);
    string emit_binary(Node* n);
    string emit_enum_check(Type* dest, const string& e);
    string emit_conv(Node* src, Type* dest, bool checked);
    string emit_member(Node* n);
    string emit_index(Node* n);
    string emit_slice(Node* n);
    string emit_array_lit(Node* n);
    string emit_span_make(Node* n);
    string emit_direct_call(Node* fn, Node* owner, Node* args);
    string emit_addr(Node* n);
    string vt_type_name(Type* iface);
    string vt_instance_name(Node* st, Node* iface);
    string emit_display_buf(const string& b, Node* v);
    string emit_print_formatted(Node* n);
    string emit_format_call(Node* n);
    string emit_hash_of(Type* t, const string& e);
    string emit_hash(Node* n);
    string emit_str_conv(Node* src, bool checked);
    void emit_iface_typedef(Node* iface);
    void emit_vtable(Node* st, Node* iface_type_node);
    void emit_ifaces(Node* mod);
    string emit_as_cspan(Node* n);
    string emit_args(Node* args);
    string emit_extern_args(Node* n);
    string emit_call(Node* n);
    string emit_ctor(Node* n, Node* st);
    string emit_exhausted_lit(Type* payload);
    string emit_allocator(Node* n);
    string emit_new(Node* n);
    string emit_alloc(Node* n);
    void emit_free(Node* n);
    void emit_with(Node* n);
    void emit_block(Node* n);
    void emit_stmt(Node* n);
    bool any_defers();
    int loop_scope(string_view label);
    void emit_return(Node* n);
    void emit_jump(Node* n);
    void emit_while(Node* n);
    void emit_for_range(Node* n);
    void emit_match(Node* n, const string& dest = {});
    string emit_match_expr(Node* n);
    void emit_if(Node* n);
    void emit_sig(Node* fn, Node* owner, bool define);
    string type_attrs(Node* n);
    void emit_struct(Node* st);
    void emit_union(Node* un);
    void emit_enum(Node* en);
    void emit_global(Node* g);
    void note_opt(Type* t);
    void note_fail(Type* payload);
    void note_tup(Type* t);
    void note_fn(Type* t);
    void note_type(Type* t);
    void walk_types(Node* n);
    void emit_type_forwards(Node* mod);
    void emit_array_typedefs(bool funcs);
    void emit_tup_typedefs();
    void emit_fn_typedefs();
    void emit_opt_typedefs();
    void note_fail_fn(Node* fn);
    void collect_from(Node* mod);
    void emit_types(Node* mod);
    void emit_decls(Node* mod);
    void emit_defs(Node* mod);
    void emit_test_sig(Node* t, bool define);
    Node* find_main(Node* mod);
    Node* find_answer(Node* mod);
    void emit_answer_unwrap(Node* mod);
    void emit_c_main(Node* fn);
    void emit_module(Node* mod);
    void emit_many(const vector<Node*>& modules, Node* entry);
    void emit_header_mod(Node* mod);
};

} // namespace lucb
