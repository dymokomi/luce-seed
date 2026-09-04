#include "check/check.h"
#include "interp/interp.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"
#include "support/test.h"

using lucb::Arena;
using lucb::DiagnosticBag;
using lucb::EvalResult;
using lucb::Source;
using lucb::Token;
using lucb::check_module;
using lucb::eval_module;
using lucb::parse;
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
