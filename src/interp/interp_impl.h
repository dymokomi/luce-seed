//==============================================================================================
//
//   interp/interp_impl - Interpreter state shared by interp/*.cpp
//
//   DESCRIPTION:
//       The `Interp` struct: frames and slots, storage for allocated values, the current
//       allocator, output and error streams, control-flow flags, and the declarations of
//       every evaluation routine grouped by unit. Implementation header.
//
//==============================================================================================

#pragma once

#include "interp/interp.h"
#include "interp/value.h"

#include <deque>
#include <unordered_map>

namespace lucb {

struct Interp {
    Node* module = nullptr;
    vector<Node*> all_modules;
    vector<Frame> frames;
    std::deque<vector<Value>> storage;
    // String literals decoded once; every `Value.str` holds decoded text.
    std::unordered_map<const Node*, string> literal_text;
    string output;
    string err;
    bool trapped = false;
    string trap;
    bool returning = false;
    bool breaking = false;
    bool continuing = false;
    string_view jump_label;
    bool in_catch = false;
    bool recovered = false;
    Value recover_val;
    Node* current_fn = nullptr;
    Value ret;
    string err_storage;
    Frame globals;
    Value current_alloc;
    Value stdio_dummy;
    std::deque<string> strings;
    uint64_t hash_seed = 0;
    Value* bump_fb = nullptr;
    Value* bump_ptr = nullptr;
    size_t bump_len = 0;
    struct Deferred {
        Node* n = nullptr;
        bool err_only = false;
    };
    vector<vector<Deferred>> defers;

    void init_memory() {
        current_alloc.kind = TypeKind::Allocator;
        current_alloc.ptr = nullptr;
        current_alloc.u = 0;
    }

    void fail(const string& message) {
        trapped = true;
        trap = message;
    }

    Value make_array(Type* t, vector<Value> elems);
    Value eval_float_bits(Node* callee, Node* n);
    Value copy_value(const Value& v);
    Value zero_of(Type* t);
    Slot* find_slot(string_view name, Node* decl = nullptr);
    uint64_t enum_case_int(Node* en, Node* cse);
    int enum_tag_of(Node* en, Node* cse);
    Value v_enum_case(Node* cse, Type* t);
    bool values_equal(const Value& a, const Value& b, Type* t);
    Value* lvalue(Node* n);
    string decode_string(string_view tok);
    string show(const Value& v);
    Value eval(Node* n);
    Value eval_uncast(Node* n);
    Value eval_case_value(Node* n);
    Value eval_member(Node* n);
    Value eval_index(Node* n);
    Value eval_slice(Node* n);
    Value eval_array_lit(Node* n);
    Value eval_formatted(Node* n);
    Value eval_format(Node* n);
    Value eval_span_make(Node* n);
    Value heap_alloc_value();
    Value fail_exhausted(Type* fail_ty);
    Value ok_payload(Value payload, Type* fail_ty);
    Value as_alloc(Node* n);
    bool bump_fixed(Value* fb, size_t size, size_t align);
    bool take_bytes(const Value& a, size_t size, size_t align);
    Value invoke_method(Value* self, Node* st, string_view name, const vector<Value>& args);
    Value eval_new(Node* n);
    Value eval_alloc(Node* n);
    Value eval_unary(Node* n);
    bool cmp_num(const Value& L, const Value& R, Type* t, TokenKind op);
    Value arith(Type* t, const Value& L, const Value& R, TokenKind op);
    Value eval_binary(Node* n);
    Value eval_else(Node* n);
    void run_catch_handler(Node* n, const Value& errv);
    Value eval_catch(Node* n);
    bool match_pat(Node* pat, const Value& scrut, Type* st);
    Value eval_match(Node* n);
    Value eval_conv(Node* srcn, Type* dest, bool checked);
    Value eval_str_conv(const Value& x, Type* src, Type* result_ty, bool checked);
    uint64_t hash_value(const Value& v, Type* t);
    Value eval_hash(Node* n);
    Value eval_hex(Node* n);
    Value eval_bin(Node* n);
    Value eval_pad(Node* n);
    Node* find_func(string_view name);
    void load_globals();
    Value as_u8_span(const Value& v);
    Value call_func(Node* fn, Value* self, Node* args);
    Value eval_call(Node* n);
    string extern_symbol(Node* fn);
    string cstr_text(const Value& v);
    string interp_printf(const string& fmt, const vector<Value>& args);
    Value eval_extern(Node* n, Node* fn);
    Value eval_ctor(Node* n, Node* st);
    void exec(Node* n);
};

} // namespace lucb
