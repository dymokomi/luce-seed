//==============================================================================================
//
//   tests/eval_test - The oracle computes the right values
//
//   DESCRIPTION:
//       Direct tests of the interpreter: values, traps, output, and the standard modules.
//
//==============================================================================================

#include "check/check.h"
#include "interp/interp.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"
#include "support/test.h"

#include <string>

using lucb::Arena;
using lucb::check_module;
using lucb::DiagnosticBag;
using lucb::eval_module;
using lucb::EvalResult;
using lucb::parse;
using lucb::Source;
using lucb::Token;
using lucb::tokenize;

static EvalResult run(const char* text) {
    DiagnosticBag diagnostics;
    Source source = Source::from_bytes("t.lucb", text, diagnostics);
    EvalResult bad;
    if (!source.ok()) {
        return bad;
    }
    Arena arena;
    std::vector<Token> tokens = tokenize(source, diagnostics);
    if (!diagnostics.empty()) {
        return bad;
    }
    lucb::ParseResult parsed = parse(source, tokens, arena, diagnostics);
    if (!diagnostics.empty() || parsed.module == nullptr) {
        return bad;
    }
    if (!check_module(parsed.module, arena, diagnostics, "t.lucb")) {
        return bad;
    }
    return eval_module(parsed.module);
}

TEST(eval_hello_returns_40) {
    EvalResult r = run("pub func answer() -> i64:\n    var counter = 40\n    return counter\n");
    CHECK(r.ok);
    CHECK(r.has_answer);
    CHECK_EQ(r.answer, 40);
}

TEST(eval_arithmetic) {
    EvalResult r = run("pub func answer() -> i64:\n    return (1 + 2) * 3 - 4 // 2\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 7);
}

TEST(eval_if_else) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    if 1 < 0:\n"
                       "        return 1\n"
                       "    elif 2 > 1:\n"
                       "        return 2\n"
                       "    else:\n"
                       "        return 3\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_while) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var n = 0\n"
                       "    var i = 0\n"
                       "    while i < 5:\n"
                       "        n += i\n"
                       "        i += 1\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 10);
}

TEST(eval_bool_and_or) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    if true and false:\n"
                       "        return 1\n"
                       "    if false or true:\n"
                       "        return 2\n"
                       "    return 0\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_struct_method) {
    EvalResult r = run("struct Point:\n"
                       "    var x: i64\n"
                       "    var y: i64\n"
                       "    mutating func bump(by: i64):\n"
                       "        self.x += by\n"
                       "pub func answer() -> i64:\n"
                       "    var p = Point(x = 1, y = 2)\n"
                       "    p.bump(by = 3)\n"
                       "    return p.x\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_nested_call) {
    EvalResult r = run("func add(a: i64, b: i64) -> i64:\n"
                       "    return a + b\n"
                       "pub func answer() -> i64:\n"
                       "    return add(20, add(10, 12))\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 42);
}

TEST(eval_print) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    print(7)\n"
                       "    print(true)\n"
                       "    return 0\n");
    CHECK(r.ok);
    CHECK_STREQ(r.output, "7\ntrue\n");
}

TEST(eval_overflow_traps) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    return 9223372036854775807 + 1\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("overflow") != std::string::npos);
}

TEST(eval_div_by_zero_traps) {
    EvalResult r = run("pub func answer() -> i64:\n    return 1 // 0\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("division") != std::string::npos);
}

TEST(eval_neg_div_truncates_toward_zero) {
    EvalResult r = run("pub func answer() -> i64:\n    return -7 // 2\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, -3);
}

TEST(eval_remainder_sign_of_dividend) {
    EvalResult r = run("pub func answer() -> i64:\n    return -7 % 2\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, -1);
}

TEST(eval_zero_var) {
    EvalResult r = run("pub func answer() -> i64:\n    var n: i64\n    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 0);
}

TEST(eval_u8_wrap) {
    EvalResult r = run("pub func answer() -> i64:\n    let x: u8 = 250\n    return i64(x +% 10)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_u8_overflow_traps) {
    EvalResult r = run("pub func answer() -> i64:\n    let x: u8 = 250\n    return i64(x + 10)\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("overflow") != std::string::npos);
}

TEST(eval_u8_saturating) {
    EvalResult r = run("pub func answer() -> i64:\n    let x: u8 = 250\n    return i64(x +| 10)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 255);
}

TEST(eval_c_cast_truncates) {
    EvalResult r = run("pub func answer() -> i64:\n    return i64((u8)300)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 44);
}

TEST(eval_checked_conv_traps) {
    EvalResult r = run("pub func answer() -> i64:\n    let n: i64 = 300\n    return i64(u8(n))\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("conversion") != std::string::npos);
}

TEST(eval_widen) {
    EvalResult r =
        run("pub func answer() -> i64:\n    let x: i32 = 40\n    let y: i64 = x\n    return y\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 40);
}

TEST(eval_sizeof_i64) {
    EvalResult r = run("pub func answer() -> i64:\n    return i64(sizeof(i64))\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 8);
}

TEST(eval_sizeof_usize) {
    EvalResult r = run("pub func answer() -> i64:\n    return i64(sizeof(usize))\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, static_cast<int64_t>(sizeof(void*)));
}

TEST(eval_shift) {
    EvalResult r = run("pub func answer() -> i64:\n    let x: u8 = 1\n    return i64(x << 3)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 8);
}

TEST(eval_shift_too_wide_traps) {
    EvalResult r = run("pub func answer() -> i64:\n    let x: u8 = 1\n    return i64(x << 8)\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("shift") != std::string::npos);
}

TEST(eval_wrapping_neg) {
    EvalResult r = run("pub func answer() -> i64:\n    let x: u8 = 1\n    return i64(-%x)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 255);
}

TEST(eval_i64_min) {
    EvalResult r = run("pub func answer() -> i64:\n    return -9223372036854775808\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, static_cast<int64_t>(INT64_MIN));
}

TEST(eval_pointer_deref) {
    EvalResult r =
        run("pub func answer() -> i64:\n    var n: i64 = 41\n    let p = &n\n    return *p\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 41);
}

TEST(eval_array_index) {
    EvalResult r =
        run("pub func answer() -> i64:\n    var xs: i64[3] = [10, 20, 30]\n    return xs[1]\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 20);
}

TEST(eval_span_from_array) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var xs: i64[3] = [1, 2, 3]\n"
                       "    let s: i64[] = xs\n"
                       "    return s[0] + s[2]\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_span_length) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var xs: i64[4]\n"
                       "    let s: i64[] = xs\n"
                       "    return i64(s.length)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_index_oob_traps) {
    EvalResult r =
        run("pub func answer() -> i64:\n    var xs: i64[2] = [1, 2]\n    return xs[2]\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("index") != std::string::npos);
}

TEST(eval_slice) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var xs: i64[4] = [1, 2, 3, 4]\n"
                       "    let s = xs[1..<3]\n"
                       "    return s[0] + s[1]\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 5);
}

TEST(eval_for_span) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var xs: i64[3] = [10, 20, 30]\n"
                       "    var n: i64 = 0\n"
                       "    for x in xs:\n"
                       "        n += x\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 60);
}

TEST(eval_str_length) {
    EvalResult r = run("pub func answer() -> i64:\n    let t = \"hi\"\n    return i64(t.length)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_assign_through_pointer) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var n: i64 = 1\n"
                       "    let p = &n\n"
                       "    *p = 9\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 9);
}

TEST(eval_optional_else) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let x: i64? = none\n"
                       "    return x else 7\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 7);
}

TEST(eval_if_let) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let x: i64? = 3\n"
                       "    if let n = x:\n"
                       "        return n\n"
                       "    return 0\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 3);
}

TEST(eval_overflow_optional) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let x: u8 = 250\n"
                       "    return i64(x +? 10 else 0)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 0);
}

TEST(eval_try_catch) {
    EvalResult r = run("func boom() -> i64!:\n"
                       "    error(1, \"nope\")\n"
                       "    return 0\n"
                       "pub func answer() -> i64:\n"
                       "    return boom() catch e:\n"
                       "        recover 9\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 9);
}

TEST(eval_try_ok) {
    EvalResult r = run("func id(n: i64) -> i64!:\n"
                       "    return n\n"
                       "pub func answer() -> i64!:\n"
                       "    return try id(4)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_for_range) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var n: i64 = 0\n"
                       "    for i in 0..<5:\n"
                       "        n += i\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 10);
}

TEST(eval_match_int) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let x = 2\n"
                       "    match x:\n"
                       "        1:\n"
                       "            return 10\n"
                       "        2:\n"
                       "            return 20\n"
                       "        _:\n"
                       "            return 0\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 20);
}

TEST(eval_break) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var n: i64 = 0\n"
                       "    while true:\n"
                       "        n += 1\n"
                       "        if n == 3:\n"
                       "            break\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 3);
}

TEST(eval_defer) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    defer print(7)\n"
                       "    return 1\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
    CHECK_STREQ(r.output, "7\n");
}

TEST(eval_errdefer) {
    EvalResult r = run("func boom() -> i64!:\n"
                       "    errdefer print(3)\n"
                       "    error(1, \"nope\")\n"
                       "    return 0\n"
                       "pub func answer() -> i64:\n"
                       "    return boom() catch e:\n"
                       "        recover 2\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
    CHECK_STREQ(r.output, "3\n");
}

TEST(eval_labeled_break) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var n: i64 = 0\n"
                       "    outer: while true:\n"
                       "        n += 1\n"
                       "        if n == 2:\n"
                       "            break outer\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_int_enum) {
    EvalResult r = run("enum Access as u32:\n"
                       "    empty = 0\n"
                       "    read = 1\n"
                       "    write = 2\n"
                       "pub func answer() -> i64:\n"
                       "    let mode = Access.read | Access.write\n"
                       "    return i64((u32)mode)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 3);
}

TEST(eval_payload_enum) {
    EvalResult r = run("enum Cmd:\n"
                       "    quit\n"
                       "    go(n: i64)\n"
                       "pub func answer() -> i64:\n"
                       "    let c = Cmd.go(7)\n"
                       "    match c:\n"
                       "        .quit:\n"
                       "            return 0\n"
                       "        .go(n):\n"
                       "            return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 7);
}

TEST(eval_union) {
    EvalResult r = run("union Value:\n"
                       "    integer: i64\n"
                       "    real: f64\n"
                       "pub func answer() -> i64:\n"
                       "    var v: Value\n"
                       "    v.integer = 11\n"
                       "    return v.integer\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 11);
}

TEST(eval_zero_struct) {
    EvalResult r = run("struct Point:\n"
                       "    var x: i64\n"
                       "    var y: i64\n"
                       "pub func answer() -> i64:\n"
                       "    var p: Point\n"
                       "    return p.x + p.y\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 0);
}

TEST(eval_uninit) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var n: i64 = ---\n"
                       "    n = 4\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_global) {
    EvalResult r = run("var hits: i64\n"
                       "func bump():\n"
                       "    hits += 1\n"
                       "pub func answer() -> i64:\n"
                       "    bump()\n"
                       "    bump()\n"
                       "    return hits\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_offsetof_packed) {
    EvalResult r = run("packed struct H:\n"
                       "    var a: u8\n"
                       "    var b: u32\n"
                       "pub func answer() -> i64:\n"
                       "    return i64(offsetof(H, b))\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
}

TEST(eval_enum_checked_conv_traps) {
    EvalResult r = run("enum Access as u32:\n"
                       "    empty = 0\n"
                       "    read = 1\n"
                       "pub func answer() -> i64:\n"
                       "    return i64((u32)Access(3))\n");
    CHECK(r.trapped);
}

TEST(eval_interface_view) {
    EvalResult r = run("interface Counter:\n"
                       "    mutating func bump() -> i64\n"
                       "struct Box implements Counter:\n"
                       "    var n: i64\n"
                       "    mutating func bump() -> i64:\n"
                       "        self.n += 1\n"
                       "        return self.n\n"
                       "pub func answer() -> i64:\n"
                       "    var b = Box(n = 10)\n"
                       "    let c: Counter = &b\n"
                       "    return c.bump()\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 11);
}

TEST(eval_generic_id) {
    EvalResult r = run("func id[T](x: T) -> T:\n"
                       "    return x\n"
                       "pub func answer() -> i64:\n"
                       "    return id(40)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 40);
}

TEST(eval_new_i64) {
    EvalResult r = run("pub func answer() -> i64!:\n"
                       "    let p = try new i64\n"
                       "    *p = 42\n"
                       "    let n = *p\n"
                       "    free(p)\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 42);
}

TEST(eval_fixed_exhausted) {
    EvalResult r = run("pub func answer() -> i64!:\n"
                       "    var buf: u8[4]\n"
                       "    var fb = FixedBuffer.over(buf)\n"
                       "    with fb:\n"
                       "        let p = new i64 catch:\n"
                       "            return 1\n"
                       "        return 0\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
}

TEST(eval_extern_abs) {
    EvalResult r = run("extern func abs(n: i32) -> i32\n"
                       "pub func answer() -> i64:\n"
                       "    return i64(abs(-40))\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 40);
}

TEST(eval_asm_rejected) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    asm arm64:\n"
                       "        nop\n"
                       "    return 40\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("asm") != std::string::npos);
}

TEST(eval_match_expr) {
    EvalResult r = run("enum Color:\n"
                       "    red\n"
                       "    blue\n"
                       "pub func answer() -> i64:\n"
                       "    let n = match Color.red:\n"
                       "        .red => 1\n"
                       "        .blue => 2\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
}

TEST(eval_array_infer) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let xs = [1, 2, 3]\n"
                       "    return xs[0] + xs[2]\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_str_bytes) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let s = \"hi\"\n"
                       "    return i64(s.bytes[0])\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 104);
}

TEST(eval_slice_from_zero) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let xs: i64[4] = [1, 2, 3, 4]\n"
                       "    let s = xs[..<2]\n"
                       "    return s[0] + s[1]\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 3);
}

TEST(eval_field_default) {
    EvalResult r = run("struct Style:\n"
                       "    var width: i64 = 1\n"
                       "    var color: i64 = 2\n"
                       "pub func answer() -> i64:\n"
                       "    let s = Style(width = 3)\n"
                       "    return s.color\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_sync_once) {
    EvalResult r = run("var n: i64 = 0\n"
                       "func bump():\n"
                       "    n += 1\n"
                       "pub func answer() -> i64:\n"
                       "    var once: sync.Once\n"
                       "    once.run(bump)\n"
                       "    once.run(bump)\n"
                       "    return n\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
}

TEST(eval_null_foreign) {
    EvalResult r = run("extern func lb_null_probe() -> i64*\n"
                       "pub func answer() -> i64:\n"
                       "    let p = lb_null_probe()\n"
                       "    return *p\n");
    CHECK(r.trapped);
    CHECK(r.trap.find("null_foreign") != std::string::npos);
}

TEST(eval_type_alias) {
    EvalResult r = run("type Pixel = i64\n"
                       "pub func answer() -> i64:\n"
                       "    let p: Pixel = 7\n"
                       "    return p\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 7);
}

TEST(eval_func_value) {
    EvalResult r = run("func add(a: i64, b: i64) -> i64:\n"
                       "    return a + b\n"
                       "pub func answer() -> i64:\n"
                       "    let operation: func(i64, i64) -> i64 = add\n"
                       "    return operation(2, 3)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 5);
}

TEST(eval_lambda) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let add: func(i64, i64) -> i64 = (x, y) => x + y\n"
                       "    return add(2, 3)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 5);
}

TEST(eval_discard) {
    EvalResult r = run("func bump(n: i64) -> i64:\n"
                       "    print(n)\n"
                       "    return n + 1\n"
                       "pub func answer() -> i64:\n"
                       "    discard(bump(3))\n"
                       "    return 1\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
    CHECK_STREQ(r.output, "3\n");
}

TEST(eval_default_args) {
    EvalResult r = run("func add(a: i64, b: i64 = 1) -> i64:\n"
                       "    return a + b\n"
                       "pub func answer() -> i64:\n"
                       "    return add(3)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 4);
}

TEST(eval_named_default) {
    EvalResult r = run("func f(a: i64, b: i64 = 1, c: i64 = 2) -> i64:\n"
                       "    return a + b + c\n"
                       "pub func answer() -> i64:\n"
                       "    return f(10, c = 5)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 16);
}

TEST(eval_tuple) {
    EvalResult r = run("func divide(value: i64, divisor: i64) -> (i64, i64):\n"
                       "    return (value // divisor, value % divisor)\n"
                       "pub func answer() -> i64:\n"
                       "    let (q, r) = divide(17, 5)\n"
                       "    return q * 10 + r\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 32);
}

TEST(eval_match_expr_int) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let n: i64 = 2\n"
                       "    let k = match n:\n"
                       "        1 => 10\n"
                       "        2 => 20\n"
                       "        _ => 0\n"
                       "    return k\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 20);
}

TEST(eval_luce_location) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    let loc = luce.location\n"
                       "    return i64(loc.line)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_method_value) {
    EvalResult r = run("struct Point:\n"
                       "    pub let x: i64\n"
                       "    pub let y: i64\n"
                       "    func sum() -> i64:\n"
                       "        return self.x + self.y\n"
                       "pub func answer() -> i64:\n"
                       "    let p = Point(x = 2, y = 3)\n"
                       "    let f: func(const Point*) -> i64 = Point.sum\n"
                       "    return f(&p)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 5);
}

TEST(eval_memory_copy) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var dst: u8[4] = [0, 0, 0, 0]\n"
                       "    var src: u8[4] = [1, 2, 3, 4]\n"
                       "    memory.copy(dst, src, 4)\n"
                       "    return i64(dst[0]) + i64(dst[3])\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 5);
}

TEST(eval_memory_read_write) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    var n: i64 = 0\n"
                       "    memory.write[i64]((void*)(&n), 42)\n"
                       "    return memory.read[i64]((void*)(&n))\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 42);
}

TEST(eval_memory_grow) {
    EvalResult r = run("pub func answer() -> i64!:\n"
                       "    let b = try alloc u8[2]\n"
                       "    b[0] = 9\n"
                       "    let g = try memory.grow(b, 8)\n"
                       "    return i64(g[0]) + i64(g.length)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 17);
}

TEST(eval_str_from_bytes) {
    EvalResult r = run("pub func answer() -> i64!:\n"
                       "    let s = try str(\"hi\".bytes)\n"
                       "    return i64(s.length)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 2);
}

TEST(eval_str_invalid_utf8) {
    EvalResult r = run("pub func answer() -> i64!:\n"
                       "    var b: u8[1] = [255]\n"
                       "    let s = str(b) catch:\n"
                       "        return 1\n"
                       "    return i64(s.length)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
}

TEST(eval_files_list) {
    EvalResult r = run("pub func answer() -> i64!:\n"
                       "    let names = try files.list(\"testdata/programs/values\")\n"
                       "    var found = 0\n"
                       "    for name in names:\n"
                       "        if name == \"hello.lucb\":\n"
                       "            found = 1\n"
                       "    return i64(found)\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
}

TEST(eval_process_run) {
    EvalResult r = run("pub func answer() -> i64!:\n"
                       "    var args: cstr[2] = [\"-c\", \"printf out; printf err >&2; exit 7\"]\n"
                       "    let (code, out, err) = try process.run(\"/bin/sh\", args)\n"
                       "    if out == \"out\" and err == \"err\":\n"
                       "        return i64(code)\n"
                       "    return 0\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 7);
}

TEST(eval_hash_int) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    if hash(7) == hash(7) and hash(7) != hash(8):\n"
                       "        return 1\n"
                       "    return 0\n");
    CHECK(r.ok);
    CHECK_EQ(r.answer, 1);
}

TEST(eval_hex) {
    EvalResult r = run("pub func answer() -> i64:\n"
                       "    print(hex(255))\n"
                       "    return 0\n");
    CHECK(r.ok);
    CHECK(r.output.find("ff") != std::string::npos);
}
