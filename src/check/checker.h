// Checker state. Implementation header for check/*.cpp. base.md §§5–17.

#pragma once

#include "check/type.h"
#include "parse/ast.h"
#include "support/diagnostics.h"
#include "support/literal.h"

namespace lucb {

const uint32_t k_type_flags_unsupported = FlagFuncType;

struct Binding {
    string_view name;
    Type* type = nullptr;
    bool mut = false;
    bool from_local = false;
    Node* decl = nullptr;
    Node* import_src = nullptr;
    int depth = 0;
};

struct Checker {
    Arena* arena = nullptr;
    DiagnosticBag* diag = nullptr;
    string path;
    Type* ty_error = nullptr;
    Type* ty_never = nullptr;
    Type* ty_unit = nullptr;
    Type* ty_bool = nullptr;
    Type* ty_i8 = nullptr;
    Type* ty_i16 = nullptr;
    Type* ty_i32 = nullptr;
    Type* ty_i64 = nullptr;
    Type* ty_isize = nullptr;
    Type* ty_u8 = nullptr;
    Type* ty_u16 = nullptr;
    Type* ty_u32 = nullptr;
    Type* ty_u64 = nullptr;
    Type* ty_usize = nullptr;
    Type* ty_f32 = nullptr;
    Type* ty_f64 = nullptr;
    Type* ty_char = nullptr;
    Type* ty_str = nullptr;
    Type* ty_untyped = nullptr;
    Type* ty_void = nullptr;
    Type* ty_err = nullptr;
    Type* ty_cstr = nullptr;
    Type* ty_alloc = nullptr;
    Type* ty_fixed = nullptr;
    Type* ty_calloc = nullptr;
    Type* ty_fmt = nullptr;
    Type* ty_writer = nullptr;
    Type* ty_location = nullptr;
    Node* memory_mod = nullptr;
    Node* fixed_decl = nullptr;
    Node* current_module = nullptr;
    bool checking_generic_template = false;
    int inst_depth = 0;
    struct Inst {
        Node* generic = nullptr;
        Node* clone = nullptr;
        Type* type = nullptr;
        vector<Type*> args;
    };
    vector<Inst> insts;
    vector<Node*> pending_clones;
    vector<Type*> interned;
    vector<Binding> scope;
    int depth = 0;
    Node* current_fn = nullptr;
    Node* current_struct = nullptr;
    Type* return_type = nullptr;
    bool fallible_fn = false;
    bool in_catch = false;
    Type* catch_type = nullptr;
    vector<string_view> loop_labels;

    
    
    
    
    
    
    
    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    

    Type* t_error() { return ty_error; }

    Type* t_never() { return ty_never; }

    Type* t_unit() { return ty_unit; }

    Type* t_bool() { return ty_bool; }

    Type* t_i64() { return ty_i64; }

    Type* t_str() { return ty_str; }

    Type* t_usize() { return ty_usize; }

    Type* t_untyped() { return ty_untyped; }

    Type* named_scalar(string_view name);
    Type* c_alias(string_view name);
    Type* make_type(TypeKind kind, string_view name);
    string_view keep(const string& s);
    Type* intern_ptr(Type* elem, bool is_const, bool is_vol, bool nullable);
    Type* intern_arr(Type* elem, uint64_t n);
    Type* intern_iface(Node* decl, bool nullable);
    Type* intern_opt(Type* elem);
    Type* intern_fail(Type* elem);
    Type* intern_sp(Type* elem, bool is_const);
    Type* intern_atomic(Type* elem);
    Type* intern_tup(Type** elems, int n);
    bool atomic_ok(Type* t);
    void check_asm(Node* n);
    Node* syn_node(NodeKind k, const char* name);
    Node* syn_method(const char* name, Type* result, bool mutating);
    bool is_fixed(Type* t);
    Type* check_in_allocator(Node* n);
    Type* check_new(Node* n);
    Type* check_alloc(Node* n);
    void check_free(Node* n);
    void check_with(Node* n);
    void bind_memory();
    bool is_generic_decl(Node* n);
    int count_generics(Node* n);
    string sanitize_ty(const string& s);
    string mangle_inst(string_view base, const vector<Type*>& args);
    int index_of_param(Node* generic, Type* p);
    void apply_bounds(Node* g, Type* t);
    void bind_generic_params(Node* gen);
    Node* clone_chain(Node* n);
    Node* clone_node(Node* n);
    bool args_eq(const vector<Type*>& a, const vector<Type*>& b);
    bool is_identity_args(Node* generic, const vector<Type*>& args);
    Inst* find_inst(Node* generic, const vector<Type*>& args);
    bool comparable_type(Type* t);
    bool struct_implements(Node* st, Type* iface);
    bool iface_has_mutating(Type* iface);
    bool satisfies_bounds(Type* t, Node* g, Node* at);
    void unify_into(Type* pat, Type* got, Node* generic, vector<Type*>& inf, Node* at);
    bool finish_inferred(Node* generic, vector<Type*>& inf, Node* at);
    Type* subst_type(Type* t, Node* generic, const vector<Type*>& args);
    Node* instantiate_func(Node* fn, const vector<Type*>& args, Node* owner);
    Type* instantiate_struct(Node* st, const vector<Type*>& args, Node* at);
    Type* read_explicit_targs(Node* n, Node* generic, vector<Type*>& inf);
    Type* check_generic_call(Node* n, Node* fn, Node* recv);
    Type* check_generic_ctor(Node* n, Node* st);
    void mark_local(Node* n);
    bool is_local(Node* n);
    void fail(Span span, const char* code, const string& message);
    void fail_n(Node* n, const char* code, const string& message);
    void push_scope() { depth++; }

    void pop_scope();
    Binding* lookup(string_view name);
    bool bind(string_view name, Type* type, bool mut, Node* decl, Node* import_src = nullptr);
    void mark_import(Binding* b);
    Node* pub_member(Node* mod, string_view name);
    Type* decl_type(Node* d);
    string last_component(string_view path);
    void set_from_local(string_view name, bool from_local);
    bool is_core_name(string_view name);
    bool const_u64(Node* n, uint64_t* out);
    Type* resolve_type(Node* n);
    Node* struct_member(Node* st, string_view name, NodeKind kind);
    Node* enum_case(Node* en, string_view name);
    int enum_tag(Node* en, Node* cse);
    Type* check_case_payload(Node* n, Node* cse, Type* et);
    Type* check_case_value(Node* n, Type* expected);
    Type* check_expr(Node* n, Type* expected = nullptr);
    bool int_fits(uint64_t mag, bool neg, Type* dest);
    Type* coerce(Node* n, Type* got, Type* expected);
    bool same_pointee(const Type* a, const Type* b);
    bool can_ptr_convert(Type* from, Type* to, Node* n);
    Type* unify_int(Type* a, Type* b);
    Type* check_literal(Node* n, Type* expected);
    Type* check_name(Node* n);
    Type* check_self(Node* n);
    Type* check_unary(Node* n, Type* expected);
    Type* as_index_type(Node* n);
    Type* check_index(Node* n);
    Type* check_slice(Node* n);
    Type* check_array_lit(Node* n, Type* expected);
    Type* check_span_make(Node* n);
    bool is_arith(TokenKind op);
    bool is_bit(TokenKind op);
    Type* check_binary(Node* n, Type* expected);
    int count_args(Node* args);
    Type* check_call(Node* n, Type* expected);
    Type* type_from_expr_or_name(Node* a);
    Type* check_sizeof(Node* n);
    Type* check_assert(Node* n);
    Type* check_offsetof(Node* n);
    Type* check_alignof(Node* n);
    Type* check_checked_conv(Node* n, Type* dest);
    Type* check_cast(Node* n, bool checked);
    bool convert_ok(Node* n, Type* src, Type* dest, bool checked);
    bool is_display(Type* t);
    Type* check_formatted(Node* n);
    Type* check_print(Node* n);
    Type* check_format(Node* n);
    Type* check_location(Node* n);
    Type* check_error(Node* n);
    Type* check_else(Node* n, Type* expected);
    Type* check_catch(Node* n, Type* expected);
    bool pattern_covers_rest(Node* pat);
    Type* check_match(Node* n, Type* expected);
    Type* check_trap(Node* n);
    Type* check_ctor(Node* n, Node* st);
    Type* check_func_call(Node* n, Node* fn, Node* recv);
    bool is_cstr(Type* t);
    bool extern_arg_ok(Type* at, Type* pt, Node* arg);
    Type* check_variadic_arg(Node* a);
    bool is_c_repr(Type* t);
    void check_foreign_sig(Node* fn, bool exported);
    Type* check_extern_call(Node* n, Node* fn);
    Type* check_method_call(Node* n);
    Type* check_member(Node* n, bool as_call);
    bool is_mut_place(Node* n);
    bool place_is_local(Node* n);
    bool imported_owner(Node* st);
    void check_stmt(Node* n);
    bool always_returns(Node* n);
    void check_params(Node* fn);
    void check_func(Node* fn, Node* owner);
    void check_struct(Node* st);
    bool sig_matches(Node* impl, Node* req);
    void check_implements(Node* st);
    void check_interface(Node* iface);
    void collect_type_decl(Node* d, TypeKind kind);
    void collect_module(Node* mod);
    void check_enum(Node* en);
    void check_union(Node* un);
    void resolve_sig(Node* fn);
    void bind_imports(Node* mod);
    void check_unused_imports(Node* mod);
    void check_test(Node* t);
    void check_main(Node* fn);
    void check_module(Node* mod);
};

} // namespace lucb
