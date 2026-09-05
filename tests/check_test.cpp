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

TEST(check_new_ok) {
    CHECK(check_ok("pub func answer() -> i64!:\n"
                   "    let p = try new i64\n"
                   "    *p = 1\n"
                   "    free(p)\n"
                   "    return 0\n"));
}
