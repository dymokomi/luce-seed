#include "check/check.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"
#include "support/test.h"

using lucb::Arena;
using lucb::DiagnosticBag;
using lucb::Source;
using lucb::Token;
using lucb::check_module;
using lucb::parse;
using lucb::tokenize;

static bool check_ok(const char* text) {
    DiagnosticBag diagnostics;
    Source source = Source::from_bytes("t.lucb", text, diagnostics);
    if (!source.ok()) {
        return false;
    }
    Arena arena;
    std::vector<Token> tokens = tokenize(source, diagnostics);
    if (!diagnostics.empty()) {
        return false;
    }
    lucb::ParseResult parsed = parse(source, tokens, arena, diagnostics);
    if (!diagnostics.empty() || parsed.module == nullptr) {
        return false;
    }
    return check_module(parsed.module, arena, diagnostics, "t.lucb");
}

static bool check_has(const char* text, const char* code) {
    DiagnosticBag diagnostics;
    Source source = Source::from_bytes("t.lucb", text, diagnostics);
    if (!source.ok()) {
        return false;
    }
    Arena arena;
    std::vector<Token> tokens = tokenize(source, diagnostics);
    if (!diagnostics.empty()) {
        return diagnostics.has_code(code);
    }
    lucb::ParseResult parsed = parse(source, tokens, arena, diagnostics);
    if (parsed.module == nullptr) {
        return diagnostics.has_code(code);
    }
    check_module(parsed.module, arena, diagnostics, "t.lucb");
    return diagnostics.has_code(code);
}

TEST(check_hello_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n    var counter = 40\n    return counter\n"));
}

TEST(check_type_mismatch) {
    CHECK(check_has("pub func answer() -> i64:\n    return true\n", "lucb.check.type"));
}

TEST(check_unknown_name) {
    CHECK(check_has("pub func answer() -> i64:\n    return missing\n", "lucb.check.name"));
}

TEST(check_mutating_needs_var) {
    CHECK(check_has("struct Point:\n"
                    "    var x: i64\n"
                    "    mutating func bump():\n"
                    "        self.x += 1\n"
                    "pub func answer() -> i64:\n"
                    "    let p = Point(x = 0)\n"
                    "    p.bump()\n"
                    "    return p.x\n",
                    "lucb.check.mut"));
}

TEST(check_explicit_self_rejected) {
    CHECK(check_has("struct Point:\n"
                    "    var x: i64\n"
                    "    mutating func bump(self: Point):\n"
                    "        self.x += 1\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.self"));
}

TEST(check_condition_must_be_bool) {
    CHECK(check_has("pub func answer() -> i64:\n    if 1:\n        return 1\n    return 0\n",
                    "lucb.check.type"));
}

TEST(check_missing_return) {
    CHECK(check_has("pub func answer() -> i64:\n    var x = 1\n", "lucb.check.return"));
}

TEST(check_unknown_type) {
    CHECK(check_has("pub func answer() -> Widget:\n    return 1\n", "lucb.check.type"));
}

TEST(check_pointer_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n    var n: i64 = 1\n    let p = &n\n    return *p\n"));
}

TEST(check_optional_ok) {
    CHECK(check_ok("pub func answer() -> i64?:\n    return 0\n"));
}

TEST(check_try_needs_fallible) {
    CHECK(check_has("func f() -> i64!:\n    return 1\n"
                    "pub func answer() -> i64:\n    return try f()\n",
                    "lucb.check.type"));
}

TEST(check_escape_local) {
    CHECK(check_has("pub func answer() -> i64*:\n    var n: i64 = 1\n    return &n\n",
                    "lucb.check.escape"));
}

TEST(check_no_shadow) {
    CHECK(check_has("pub func answer() -> i64:\n    let x = 1\n    let x = 2\n    return x\n",
                    "lucb.check.shadow"));
}

TEST(check_enum_ok) {
    CHECK(check_ok("enum Dir:\n    north\n    south\n"
                   "pub func answer() -> i64:\n    let d = Dir.north\n    return 1\n"));
}

TEST(check_enum_match_missing) {
    CHECK(check_has("enum Dir:\n    north\n    south\n"
                    "pub func answer() -> i64:\n"
                    "    match Dir.north:\n"
                    "        .north:\n"
                    "            return 1\n",
                    "lucb.check.match"));
}

TEST(check_int_enum_needs_rest) {
    CHECK(check_has("enum Access as u32:\n    empty = 0\n    read = 1\n"
                    "pub func answer() -> i64:\n"
                    "    match Access.read:\n"
                    "        .empty:\n"
                    "            return 0\n"
                    "        .read:\n"
                    "            return 1\n",
                    "lucb.check.match"));
}

TEST(check_packed_addr_rejected) {
    CHECK(check_has("packed struct H:\n    var a: u8\n    var b: i64\n"
                    "pub func answer() -> i64:\n"
                    "    var h: H\n"
                    "    let p = &h.b\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_none_case_rejected) {
    CHECK(check_has("enum E:\n    none\n"
                    "pub func answer() -> i64:\n    return 0\n",
                    "lucb.check.type"));
}

TEST(check_u8_literal_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n    let x: u8 = 200\n    return i64(x)\n"));
}

TEST(check_u8_literal_too_big) {
    CHECK(check_has("pub func answer() -> i64:\n    let x: u8 = 300\n    return 0\n",
                    "lucb.check.number"));
}

TEST(check_unary_minus_unsigned_rejected) {
    CHECK(check_has("pub func answer() -> i64:\n    let x: u8 = 1\n    return i64(-x)\n",
                    "lucb.check.type"));
}

TEST(check_checked_conv_literal_impossible) {
    CHECK(check_has("pub func answer() -> i64:\n    return i64(u8(300))\n", "lucb.check.number"));
}

TEST(check_c_cast_truncates) {
    CHECK(check_ok("pub func answer() -> i64:\n    return i64((u8)300)\n"));
}

TEST(check_widen_u8_to_u32) {
    CHECK(check_ok("pub func answer() -> i64:\n    let x: u8 = 3\n    let y: u32 = x\n    return i64(y)\n"));
}

TEST(check_no_implicit_signedness_change) {
    CHECK(check_has("pub func answer() -> i64:\n    let x: u8 = 3\n    let y: i64 = x\n    return y\n",
                    "lucb.check.type"));
}

TEST(check_new_needs_try) {
    CHECK(check_has("pub func answer() -> i64:\n    let p = new i64\n    return 0\n",
                    "lucb.check.type"));
}

TEST(check_free_needs_pointer) {
    CHECK(check_has("pub func answer() -> i64:\n    free(1)\n    return 0\n", "lucb.check.type"));
}

TEST(check_new_zeroable) {
    CHECK(check_has("pub func answer() -> i64!:\n    let p = try new i64*\n    return 0\n",
                    "lucb.check.type"));
}

TEST(check_interface_missing_method) {
    CHECK(check_has("interface Counter:\n"
                    "    mutating func bump() -> i64\n"
                    "struct Box implements Counter:\n"
                    "    var n: i64\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_extern_str_rejected) {
    CHECK(check_has("extern func puts(s: str) -> i32\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_extern_out_unsupported) {
    CHECK(check_has("extern func get(out n: i32) -> bool\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.unsupported"));
}

TEST(check_atomic_ok) {
    CHECK(check_ok("var hits: @u64\n"
                   "pub func answer() -> i64:\n"
                   "    hits += 1\n"
                   "    return i64(hits)\n"));
}

TEST(check_atomic_bad_type) {
    CHECK(check_has("var hits: @str\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_c_int_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    var n: c.int = 40\n"
                   "    return i64(n)\n"));
}

TEST(check_extern_handle_ok) {
    CHECK(check_ok("extern type Window\n"
                   "extern func SDL_GetError() -> cstr\n"
                   "pub func answer() -> i64:\n"
                   "    return 0\n"));
}

TEST(check_variadic_str_rejected) {
    CHECK(check_has("extern func printf(format: cstr, ...) -> i32\n"
                    "pub func answer() -> i64:\n"
                    "    var s = \"hi\"\n"
                    "    return i64(printf(\"%s\", s))\n",
                    "lucb.check.type"));
}

TEST(check_generic_id_ok) {
    CHECK(check_ok("func id[T](x: T) -> T:\n"
                   "    return x\n"
                   "pub func answer() -> i64:\n"
                   "    return id(40)\n"));
}

TEST(check_generic_plus_rejected) {
    CHECK(check_has("func add[T](a: T, b: T) -> T:\n"
                    "    return a + b\n"
                    "pub func answer() -> i64:\n"
                    "    return add(1, 2)\n",
                    "lucb.check.type"));
}

TEST(check_generic_needs_targ) {
    CHECK(check_has("func decode[T]() -> i64:\n"
                    "    return 1\n"
                    "pub func answer() -> i64:\n"
                    "    return decode()\n",
                    "lucb.check.type"));
}

TEST(check_new_ok) {
    CHECK(check_ok("pub func answer() -> i64!:\n"
                   "    let p = try new i64\n"
                   "    *p = 1\n"
                   "    free(p)\n"
                   "    return 0\n"));
}

TEST(check_sync_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    var lock: sync.Mutex\n"
                   "    lock.lock()\n"
                   "    lock.unlock()\n"
                   "    return 1\n"));
}

TEST(check_sync_mut) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let lock: sync.Mutex\n"
                    "    lock.lock()\n"
                    "    return 0\n",
                    "lucb.check.mut"));
}

TEST(check_assign_let) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let n = 1\n"
                    "    n = 2\n"
                    "    return n\n",
                    "lucb.check.mut"));
}

TEST(check_arity) {
    CHECK(check_has("func f(a: i64) -> i64:\n"
                    "    return a\n"
                    "pub func answer() -> i64:\n"
                    "    return f(1, 2)\n",
                    "lucb.check.call"));
}

TEST(check_break_outside) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    break\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_never_null_zero) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    var p: i64*\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_missing_field) {
    CHECK(check_has("struct Box:\n"
                    "    var p: i64*\n"
                    "pub func answer() -> i64:\n"
                    "    var n: i64 = 1\n"
                    "    let b = Box()\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_duplicate_field) {
    CHECK(check_has("struct Point:\n"
                    "    var x: i64\n"
                    "    var x: i64\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.shadow"));
}

TEST(check_fmt_stored) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let x = f\"hi\"\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_lambda_unsupported) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let add = (x, y) => x + y\n"
                    "    return 0\n",
                    "lucb.check.unsupported"));
}

TEST(check_type_alias_unsupported) {
    CHECK(check_has("type Count = i64\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.unsupported"));
}

TEST(check_func_type_unsupported) {
    CHECK(check_has("func add(a: i64, b: i64) -> i64:\n"
                    "    return a + b\n"
                    "pub func answer() -> i64:\n"
                    "    let operation: func(i64, i64) -> i64 = add\n"
                    "    return 0\n",
                    "lucb.check.unsupported"));
}

TEST(check_array_infer_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    let xs = [1, 2, 3]\n"
                   "    return xs[0]\n"));
}

TEST(check_match_expr_ok) {
    CHECK(check_ok("enum Color:\n"
                   "    red\n"
                   "    blue\n"
                   "pub func answer() -> i64:\n"
                   "    let n = match Color.red:\n"
                   "        .red => 1\n"
                   "        .blue => 2\n"
                   "    return n\n"));
}

TEST(check_func_as_value) {
    CHECK(check_has("func add(a: i64, b: i64) -> i64:\n"
                    "    return a + b\n"
                    "pub func answer() -> i64:\n"
                    "    return add\n",
                    "lucb.check.type"));
}

TEST(check_escape_global_ok) {
    CHECK(check_ok("var g: i64 = 40\n"
                   "func addr() -> i64*:\n"
                   "    return &g\n"
                   "pub func answer() -> i64:\n"
                   "    return *addr()\n"));
}

TEST(check_let_field_mut) {
    CHECK(check_has("struct Point:\n"
                    "    let x: i64\n"
                    "pub func answer() -> i64:\n"
                    "    var p = Point(x = 1)\n"
                    "    p.x = 2\n"
                    "    return p.x\n",
                    "lucb.check.mut"));
}

TEST(check_nullable_deref) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    var p: i64*? = none\n"
                    "    return *p\n",
                    "lucb.check.type"));
}

TEST(check_match_guard_not_cover) {
    CHECK(check_has("enum E:\n"
                    "    a\n"
                    "    b\n"
                    "pub func answer() -> i64:\n"
                    "    match E.a:\n"
                    "        .a if false:\n"
                    "            return 1\n"
                    "        .b:\n"
                    "            return 2\n",
                    "lucb.check.match"));
}

TEST(check_fallible_field) {
    CHECK(check_has("struct Box:\n"
                    "    var r: i64!\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_ptr_int_cast_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    var n: i64 = 1\n"
                   "    let p = &n\n"
                   "    let a = (usize)p\n"
                   "    let q = (i64*)a\n"
                   "    return *q\n"));
}

TEST(check_match_expr_unify) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let x = match true:\n"
                    "        true => 1\n"
                    "        false => false\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_always_returns_early) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    return 1\n"
                   "    print(2)\n"));
}

TEST(check_atomic_store_mut) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let hits: @u64\n"
                    "    hits.store(1)\n"
                    "    return 0\n",
                    "lucb.check.mut"));
}

TEST(check_alloc_needs_count) {
    CHECK(check_has("pub func answer() -> i64!:\n"
                    "    let s = try alloc i64[]\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_eq_unconstrained) {
    CHECK(check_has("func eq[T](a: T, b: T) -> bool:\n"
                    "    return a == b\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_sizeof_ptr_ok) {
    CHECK(check_ok("struct Node:\n"
                   "    var n: i64\n"
                   "pub func answer() -> i64:\n"
                   "    return i64(sizeof(Node*))\n"));
}

TEST(check_payload_arity) {
    CHECK(check_has("enum Cmd:\n"
                    "    go(n: i64)\n"
                    "pub func answer() -> i64:\n"
                    "    match Cmd.go(1):\n"
                    "        .go:\n"
                    "            return 0\n",
                    "lucb.check.call"));
}

TEST(check_new_count_ok) {
    CHECK(check_ok("pub func answer() -> i64!:\n"
                   "    let n: usize = 2\n"
                   "    let p = try new i64[n]\n"
                   "    free(p)\n"
                   "    return 0\n"));
}

TEST(check_keyword_enum_ok) {
    CHECK(check_ok("enum TokenKind:\n"
                   "    eof\n"
                   "    func\n"
                   "pub func answer() -> i64:\n"
                   "    let t = TokenKind.func\n"
                   "    match t:\n"
                   "        .eof:\n"
                   "            return 0\n"
                   "        .func:\n"
                   "            return 1\n"));
}
