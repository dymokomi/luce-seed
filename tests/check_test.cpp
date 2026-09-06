//==============================================================================================
//
//   tests/check_test - The checker accepts and rejects the right programs
//
//   DESCRIPTION:
//       Positive programs check cleanly; negative programs are refused with the pinned
//       diagnostic code.
//
//==============================================================================================

#include "check/check.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"
#include "support/test.h"

using lucb::Arena;
using lucb::check_module;
using lucb::DiagnosticBag;
using lucb::parse;
using lucb::Source;
using lucb::Token;
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

// The checker warns and prunes (base.md §19.6): the program still checks, the warning is
// in the bag, and the tree no longer holds what was warned about.
static bool check_warns(const char* text, const char* code) {
    DiagnosticBag diagnostics;
    Source source = Source::from_bytes("t.lucb", text, diagnostics);
    Arena arena;
    std::vector<Token> tokens = tokenize(source, diagnostics);
    lucb::ParseResult parsed = parse(source, tokens, arena, diagnostics);
    if (!diagnostics.empty() || parsed.module == nullptr) {
        return false;
    }
    check_module(parsed.module, arena, diagnostics, "t.lucb");
    return diagnostics.empty() && diagnostics.has_warning(code);
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
    CHECK(
        check_ok("pub func answer() -> i64:\n    var n: i64 = 1\n    let p = &n\n    return *p\n"));
}

TEST(check_optional_ok) {
    CHECK(check_ok("pub func answer() -> i64?:\n    return 0\n"));
}

TEST(check_try_needs_fallible) {
    CHECK(check_has("func f() -> i64!:\n    return 1\n"
                    "pub func answer() -> i64:\n    return try f()\n",
                    "lucb.check.type"));
}

TEST(check_escape_str_of_local) {
    CHECK(check_has("func leak() -> str:\n    var b: u8[16]\n    return (str)b[..<4]\n", "lucb.check.escape"));
}

TEST(check_weak_is_a_base_attribute) {
    CHECK(check_ok("weak func f() -> i64:\n    return 1\nweak var g: i64 = 2\npub func answer() -> i64:\n    return f() + g\n"));
}

TEST(check_naked_body_is_asm_only) {
    CHECK(check_ok("naked func f() -> i64:\n    asm arm64:\n        mov x0, #42\n        ret\npub func answer() -> i64:\n    return f()\n"));
    CHECK(check_has("naked func f() -> i64:\n    return 1\n", "lucb.check.naked"));
}

TEST(check_float_bits) {
    CHECK(check_ok("pub func answer() -> i64:\n    let x: f64 = 1.5\n    let b = x.bits()\n    let y = f64.bits(b)\n    let s: f32 = 2.0\n    let sb: u32 = s.bits()\n    return (i64)b + (i64)sb + (i64)f32.bits(sb)\n"));
    CHECK(check_has("pub func answer() -> i64:\n    let x: f64 = 1.5\n    return (i64)x.bits(1)\n", "lucb.check.call"));
}

TEST(check_labeled_loop_keeps_its_variable) {
    CHECK(check_ok("pub func answer() -> i64:\n    var t: i64 = 0\n    outer: for i in 0..<3:\n        for j in 0..<3:\n            if i == j: continue outer\n            t += i\n    return t\n"));
}

TEST(check_untyped_expression_against_typed_operand) {
    CHECK(check_ok("pub func answer() -> i64:\n    let w: u64 = 7\n    if w == 256 | 7:\n        return 1\n    if w == (256 << 1):\n        return 2\n    return 42\n"));
}

TEST(check_unused_import_is_pruned) {
    CHECK(check_ok("import memory\npub func answer() -> i64:\n    return 42\n"));
}

TEST(warn_unused_local_is_pruned) {
    CHECK(check_warns("pub func answer() -> i64:\n    let unused = 5\n    return 42\n", "lucb.warn.unused"));
    CHECK(check_ok("pub func answer() -> i64:\n    let _scratch = 5\n    return 42\n"));
}

TEST(warn_unused_import) {
    CHECK(check_warns("import memory\npub func answer() -> i64:\n    return 42\n", "lucb.warn.unused"));
}

// `from io import Writer` brings `Writer` alone (§16.3): a program that uses both forms
// needs both imports, with nothing to warn about. (This seed keeps the standard modules as
// builtins, so `io.stdout()` without `import io` is refused by luce-base, not here;
// `testdata/programs/errors/from_import_brings_only_the_name` pins it for a user module.)
TEST(check_from_import_brings_only_the_name) {
    CHECK(!check_warns("import io\nfrom io import Writer\npub func answer() -> i64:\n    var w: Writer = io.stdout()\n    discard(w)\n    return 42\n",
                       "lucb.warn.unused"));
}

TEST(warn_unused_private_function) {
    CHECK(check_warns("func spare() -> i64:\n    return 1\npub func answer() -> i64:\n    return 42\n", "lucb.warn.unused"));
    CHECK(!check_warns("func used() -> i64:\n    return 1\npub func answer() -> i64:\n    return 41 + used()\n", "lucb.warn.unused"));
    CHECK(!check_warns("pub func exported() -> i64:\n    return 1\npub func answer() -> i64:\n    return 42\n", "lucb.warn.unused"));
}

TEST(warn_unreachable_code) {
    CHECK(check_warns("pub func answer() -> i64:\n    return 42\n    let x = 1\n", "lucb.warn.dead"));
}

TEST(warn_constant_branch) {
    CHECK(check_warns("pub func answer() -> i64:\n    if true:\n        return 42\n    else:\n        return 0\n", "lucb.warn.dead"));
    CHECK(check_warns("pub func answer() -> i64:\n    while false:\n        return 0\n    return 42\n", "lucb.warn.dead"));
}

TEST(check_binding_forms_positive) {
    const char* pre = "struct P:\n    var a: i64\n    var b: i64\nfunc give() -> i64:\n    return 42\nfunc two() -> (i64, i64):\n    return (40, 2)\n";
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    let x = 42\n    return x\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    var x: i64\n    x += 42\n    return x\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    var x: i64 = 41\n    x += 1\n    return x\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    let b: u8 = 42\n    return (i64)b\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    let x = give()\n    return x\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    var p = P(a = 42, b = 0)\n    return p.a\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    let xs = [40, 2]\n    return xs[0] + xs[1]\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    let (a, b) = two()\n    return a + b\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    var buf: u8[8] = ---\n    buf[0] = 42\n    return (i64)buf[0]\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    var q: i64*? = none\n    if q == none:\n        return 42\n    return 0\n").c_str()));
    CHECK(check_ok((std::string(pre) + "pub func answer() -> i64:\n    var p: P\n    p.a = 42\n    return p.a + p.b\n").c_str()));
}

TEST(check_binding_forms_negative) {
    CHECK(check_has("pub func answer() -> i64:\n    let x: i64\n    return 42\n", "lucb.parse.expect"));
    CHECK(check_has("pub func answer() -> i64:\n    var x\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("pub func answer() -> i64:\n    let x = none\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("interface S:\n    func a() -> i64\npub func answer() -> i64:\n    var v: S\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("interface S:\n    func a() -> i64\npub func answer() -> i64:\n    var vs: S[2]\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("pub func answer() -> i64:\n    var p: i64*\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("pub func answer() -> i64:\n    var x = ---\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("pub func answer() -> i64:\n    let x: i64 = \"text\"\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("pub func answer() -> i64:\n    let x = 1\n    let x = 2\n    return 42\n", "lucb.check.shadow"));
    CHECK(check_has("pub func answer() -> i64:\n    let x = 1\n    if true:\n        let x = 2\n    return 42\n", "lucb.check.shadow"));
    CHECK(check_has("func nothing():\n    return\npub func answer() -> i64:\n    let x = nothing()\n    return 42\n", "lucb.check.type"));
    CHECK(check_has("pub func answer() -> i64:\n    let b: u8 = 300\n    return 42\n", "lucb.check.number"));
    CHECK(check_has("pub func answer() -> i64:\n    let x = 1\n    x = 2\n    return 42\n", "lucb.check.mut"));
    CHECK(check_has("pub func answer() -> i64:\n    let y = x + 1\n    let x = 41\n    return y\n", "lucb.check.name"));
}

TEST(check_core_names_are_not_declared) {
    CHECK(check_has("enum Kind as u8:\n    i8 = 1\n    unit = 2\npub func answer() -> i64:\n    return 42\n", "lucb.check.shadow"));
    CHECK(check_has("struct P:\n    var str: i64\npub func answer() -> i64:\n    return 42\n", "lucb.check.shadow"));
    CHECK(check_has("pub func answer() -> i64:\n    let error = 1\n    return 42\n", "lucb.check.shadow"));
    CHECK(check_has("func f(format: i64) -> i64:\n    return format\npub func answer() -> i64:\n    return f(42)\n", "lucb.check.shadow"));
    CHECK(check_has("func trap() -> i64:\n    return 1\npub func answer() -> i64:\n    return 42\n", "lucb.check.shadow"));
    CHECK(check_has("struct S:\n    var n: i64\n    func pad() -> i64:\n        return self.n\npub func answer() -> i64:\n    return 42\n", "lucb.check.shadow"));
    CHECK(check_ok("enum Kind as u8:\n    i8_ = 1\n    unit_ = 2\nstruct P:\n    var text: i64\npub func answer() -> i64:\n    let c = 1\n    return 41 + c\n"));
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
                    "lucb.parse.expect"));
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
    CHECK(check_ok(
        "pub func answer() -> i64:\n    let x: u8 = 3\n    let y: u32 = x\n    return i64(y)\n"));
}

TEST(check_no_implicit_signedness_change) {
    CHECK(check_has(
        "pub func answer() -> i64:\n    let x: u8 = 3\n    let y: i64 = x\n    return y\n",
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
                    "struct Box: Counter:\n"
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
    CHECK(check_ok("import c\npub func answer() -> i64:\n"
                   "    var n: c.int = 40\n"
                   "    return i64(n)\n"));
}

// The `c` module (§5.2): its types need `import c`; `c.int` is `i32` by another name;
// `c.long`, `c.char`, and `c.wchar` are distinct, converted with `T(x)` or a cast; the C
// text type is `c.str`, and `cstr` is no name at all.
TEST(check_c_module_types) {
    CHECK(check_has("pub func answer() -> i64:\n    var n: c.int = 40\n    return i64(n)\n", "lucb.check.import"));
    CHECK(check_ok("import c\npub func answer() -> i64:\n    var n: c.int = 40\n    let m: i32 = n\n    return i64(m)\n"));
    CHECK(check_has("import c\npub func answer() -> i64:\n    var n: c.long = 40\n    let m: i64 = n\n    return m\n", "lucb.check.type"));
    CHECK(check_ok("import c\npub func answer() -> i64:\n    var n: c.long = 40\n    let m = i64(n) + i64((c.long)2) + i64(c.long(-2))\n    return m\n"));
    CHECK(check_ok("import c\npub func answer() -> i64:\n    let ch: c.char = c.char(65)\n    let w: c.wchar = (c.wchar)ch\n    return i64(w) - 25\n"));
    CHECK(check_ok("import c\nextern func vprintf(pattern: c.str, args: c.va_list) -> i32\n"
                   "pub func log(pattern: c.str, args: c.va_list) -> i32:\n    return vprintf(pattern, args)\n"
                   "pub func answer() -> i64:\n    return 40\n"));
    CHECK(check_has("pub func answer() -> i64:\n    var s: cstr = \"x\"\n    return 40\n", "lucb.check.type"));
}

TEST(check_extern_handle_ok) {
    CHECK(check_ok("import c\nextern type Window\n"
                   "extern func SDL_GetError() -> c.str\n"
                   "pub func answer() -> i64:\n"
                   "    return 0\n"));
}

TEST(check_variadic_str_rejected) {
    CHECK(check_has("import c\nextern func printf(format: c.str, ...) -> i32\n"
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

TEST(check_lambda_needs_type) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let add = (x, y) => x + y\n"
                    "    return 0\n",
                    "lucb.check.type"));
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

TEST(check_keyword_enum_rejected) {
    CHECK(check_has("enum TokenKind:\n"
                    "    eof\n"
                    "    func\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.parse.expect"));
}

TEST(check_type_alias_ok) {
    CHECK(check_ok("type Pixel = i64\n"
                   "pub func answer() -> i64:\n"
                   "    let p: Pixel = 7\n"
                   "    return p\n"));
}

TEST(check_func_type_ok) {
    CHECK(check_ok("func add(a: i64, b: i64) -> i64:\n"
                   "    return a + b\n"
                   "pub func answer() -> i64:\n"
                   "    let operation: func(i64, i64) -> i64 = add\n"
                   "    return operation(2, 3)\n"));
}

TEST(check_lambda_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    let add: func(i64, i64) -> i64 = (x, y) => x + y\n"
                   "    return add(2, 3)\n"));
}

TEST(check_lambda_capture_rejected) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let n = 1\n"
                    "    let f: func(i64) -> i64 = (x) => x + n\n"
                    "    return f(1)\n",
                    "lucb.check.type"));
}

TEST(check_discard_ok) {
    CHECK(check_ok("func bump(n: i64) -> i64:\n"
                   "    return n + 1\n"
                   "pub func answer() -> i64:\n"
                   "    discard(bump(3))\n"
                   "    return 1\n"));
}

TEST(check_discard_fallible) {
    CHECK(check_has("func boom() -> i64!:\n"
                    "    return 1\n"
                    "pub func answer() -> i64:\n"
                    "    discard(boom())\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_default_args_ok) {
    CHECK(check_ok("func add(a: i64, b: i64 = 1) -> i64:\n"
                   "    return a + b\n"
                   "pub func answer() -> i64:\n"
                   "    return add(3)\n"));
}

TEST(check_tuple_ok) {
    CHECK(check_ok("func pair() -> (i64, i64):\n"
                   "    return (1, 2)\n"
                   "pub func answer() -> i64:\n"
                   "    let (a, b) = pair()\n"
                   "    return a + b\n"));
}

TEST(check_alias_recursive) {
    CHECK(check_has("type A = B\n"
                    "type B = A\n"
                    "pub func answer() -> i64:\n"
                    "    let x: A = 1\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_func_no_zero) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    var f: func() -> i64\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_location_call_rejected) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let loc = location()\n"
                    "    return i64(loc.line)\n",
                    "lucb.check.name"));
}

TEST(check_luce_location_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    let loc = luce.location\n"
                   "    return i64(loc.line)\n"));
}

TEST(check_arena_implements_ok) {
    CHECK(check_ok("pub struct Arena: Allocator:\n"
                   "    var parent: Allocator\n"
                   "    var block: u8[]\n"
                   "    var used: usize\n"
                   "    pub mutating func allocate(size: usize, alignment: usize) -> u8[]?:\n"
                   "        return none\n"
                   "    pub mutating func resize(block: u8[], size: usize) -> bool:\n"
                   "        return false\n"
                   "    pub mutating func release(block: u8[]):\n"
                   "        discard(block.length)\n"
                   "pub func answer() -> i64:\n"
                   "    return 0\n"));
}

TEST(check_memory_copy_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    var dst: u8[4] = [0, 0, 0, 0]\n"
                   "    var src: u8[4] = [1, 2, 3, 4]\n"
                   "    memory.copy(dst, src, 4)\n"
                   "    return 0\n"));
}

TEST(check_memory_read_needs_type) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    var n: i64 = 1\n"
                    "    return memory.read((void*)(&n))\n",
                    "lucb.check.type"));
}

TEST(check_memory_read_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    var n: i64 = 1\n"
                   "    return memory.read[i64]((void*)(&n))\n"));
}

TEST(check_str_bytes_ok) {
    CHECK(check_ok("pub func answer() -> i64!:\n"
                   "    let s = try str(\"hi\".bytes)\n"
                   "    return i64(s.length)\n"));
}

TEST(check_str_bytes_needs_try) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let s = str(\"hi\".bytes)\n"
                    "    return i64(s.length)\n",
                    "lucb.check.type"));
}

TEST(check_files_list_ok) {
    CHECK(check_ok("pub func answer() -> i64!:\n"
                   "    let names = try files.list(\".\")\n"
                   "    return i64(names.length)\n"));
}

TEST(check_process_run_ok) {
    CHECK(check_ok("import c\npub func answer() -> i64!:\n"
                   "    var args: c.str[1] = [\"\"]\n"
                   "    let (code, out, err) = try process.run(\"/bin/true\", args[0..<0])\n"
                   "    discard(out)\n"
                   "    discard(err)\n"
                   "    return i64(code)\n"));
}

TEST(check_char_u8_eq_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    let b: u8 = 32\n"
                   "    if b == ' ':\n"
                   "        return 1\n"
                   "    return 0\n"));
}

TEST(check_writer_fmt_ok) {
    CHECK(check_ok("struct Sink: Writer:\n"
                   "    var n: usize\n"
                   "    mutating func write(bytes: const u8[]) -> usize!:\n"
                   "        self.n += bytes.length\n"
                   "        return bytes.length\n"
                   "pub func answer() -> i64!:\n"
                   "    var s = Sink(n = 0)\n"
                   "    let w: Writer = &s\n"
                   "    let n = try w.write(f\"n={1}\")\n"
                   "    return i64(n)\n"));
}

TEST(check_hashable_bound_ok) {
    CHECK(check_ok("func intern[K: Hashable & Equatable](keys: const K[], key: K) -> usize:\n"
                   "    if keys[0] == key:\n"
                   "        return 0\n"
                   "    return 1\n"
                   "pub func answer() -> i64:\n"
                   "    let table: i64[2] = [1, 2]\n"
                   "    return i64(intern(table, 1))\n"));
}

TEST(check_hashable_bound_rejected) {
    CHECK(check_has("func intern[K: Hashable](key: K) -> u64:\n"
                    "    return hash(key)\n"
                    "interface Sink:\n"
                    "    mutating func write(bytes: const u8[]) -> usize\n"
                    "pub func answer() -> i64:\n"
                    "    var s: Sink? = none\n"
                    "    return i64(intern(s))\n",
                    "lucb.check.type"));
}

TEST(check_hash_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    if hash(7) == hash(7):\n"
                   "        return 1\n"
                   "    return 0\n"));
}

TEST(check_hash_rejected) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    return i64(hash(true, 1))\n",
                    "lucb.check.call"));
}

TEST(check_hex_ok) {
    CHECK(check_ok("pub func answer() -> i64:\n"
                   "    print(hex(255))\n"
                   "    return 0\n"));
}

TEST(check_process_shadow) {
    CHECK(check_has("func process() -> i64:\n"
                    "    return 1\n"
                    "pub func answer() -> i64:\n"
                    "    return process()\n",
                    "lucb.check.shadow"));
}

TEST(check_errorcode_package_ok) {
    CHECK(check_ok("pub let missing: ErrorCode = ErrorCode.package(1)\n"
                   "pub func answer() -> i64:\n"
                   "    return 0\n"));
}

TEST(check_errorcode_package_duplicate) {
    CHECK(check_has("pub let a: ErrorCode = ErrorCode.package(1)\n"
                    "pub let b: ErrorCode = ErrorCode.package(1)\n"
                    "pub func answer() -> i64:\n"
                    "    return 0\n",
                    "lucb.check.shadow"));
}

TEST(check_errorcode_package_not_const) {
    CHECK(check_has("pub func answer() -> i64:\n"
                    "    let c: ErrorCode = ErrorCode.package(1)\n"
                    "    return 0\n",
                    "lucb.check.type"));
}

TEST(check_enum_method) {
    CHECK(check_ok("enum Token:\n"
                   "    number(value: i64)\n"
                   "    eof\n"
                   "    func weight() -> i64:\n"
                   "        match self:\n"
                   "            .number(value): return value\n"
                   "            .eof: return 0\n"
                   "pub func answer() -> i64:\n"
                   "    return Token.eof.weight()\n"));
}

TEST(check_import_thread) {
    CHECK(check_ok("import thread\n"
                   "func run(context: void*):\n"
                   "    discard(context)\n"
                   "pub func answer() -> i64!:\n"
                   "    var n: i64 = 0\n"
                   "    let h = try thread.spawn(run, (void*)(&n))\n"
                   "    try h.join()\n"
                   "    return n\n"));
}

TEST(check_export_span) {
    CHECK(check_ok("export func checksum(data: const u8[]) -> u32:\n"
                   "    return 0\n"
                   "pub func answer() -> i64:\n"
                   "    return 0\n"));
}
