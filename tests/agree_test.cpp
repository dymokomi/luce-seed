#include "check/check.h"
#include "emit/emit.h"
#include "emit/host.h"
#include "interp/interp.h"
#include "lex/lexer.h"
#include "parse/parser.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"
#include "support/test.h"

#include <string>
#include <unistd.h>

using lucb::Arena;
using lucb::DiagnosticBag;
using lucb::EvalResult;
using lucb::RunResult;
using lucb::Source;
using lucb::Token;
using lucb::check_module;
using lucb::compile_c;
using lucb::emit_c;
using lucb::eval_module;
using lucb::parse;
using lucb::run_exe;
using lucb::tokenize;

struct Both {
    EvalResult interp;
    RunResult native;
    bool compiled = false;
    std::string compile_error;
};

static bool compile_source(const char* text, Both* both) {
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
    if (!check_module(parsed.module, arena, diagnostics, "t.lucb")) {
        return false;
    }
    both->interp = eval_module(parsed.module);
    std::string c = emit_c(parsed.module);
    char tmpl[] = "/tmp/lucbXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (dir == nullptr) {
        both->compile_error = "mkdtemp failed";
        return false;
    }
    std::string exe = std::string(dir) + "/prog";
    if (!compile_c(c, exe, &both->compile_error)) {
        return false;
    }
    both->compiled = true;
    both->native = run_exe(exe);
    return true;
}

static std::string interp_stdout(const EvalResult& r) {
    std::string s = r.output;
    if (r.has_answer) {
        s += std::to_string(r.answer);
        s += '\n';
    }
    return s;
}

static bool agrees(const char* text) {
    Both both;
    if (!compile_source(text, &both)) {
        std::fprintf(stderr, "    compile failed: %s\n", both.compile_error.c_str());
        return false;
    }
    if (both.interp.trapped) {
        if (both.native.exit_code == 0) {
            std::fprintf(stderr, "    interp trapped, native exited 0\n");
            return false;
        }
        if (both.native.err.find(both.interp.trap) == std::string::npos) {
            std::fprintf(stderr, "    trap mismatch interp=%s native_err=%s\n",
                         both.interp.trap.c_str(), both.native.err.c_str());
            return false;
        }
        return true;
    }
    if (!both.interp.ok || both.native.exit_code != 0) {
        std::fprintf(stderr, "    native exit %d err=%s\n", both.native.exit_code,
                     both.native.err.c_str());
        return false;
    }
    std::string want = interp_stdout(both.interp);
    if (want != both.native.out) {
        std::fprintf(stderr, "    stdout mismatch\n    interp: %s    native: %s", want.c_str(),
                     both.native.out.c_str());
        return false;
    }
    return true;
}

TEST(agree_hello) {
    CHECK(agrees("pub func answer() -> i64:\n    var counter = 40\n    return counter\n"));
}

TEST(agree_arithmetic) {
    CHECK(agrees("pub func answer() -> i64:\n    return (1 + 2) * 3 - 4 // 2\n"));
}

TEST(agree_if_else) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    if 1 < 0:\n"
                 "        return 1\n"
                 "    elif 2 > 1:\n"
                 "        return 2\n"
                 "    else:\n"
                 "        return 3\n"));
}

TEST(agree_while) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n = 0\n"
                 "    var i = 0\n"
                 "    while i < 5:\n"
                 "        n += i\n"
                 "        i += 1\n"
                 "    return n\n"));
}

TEST(agree_struct_method) {
    CHECK(agrees("struct Point:\n"
                 "    var x: i64\n"
                 "    var y: i64\n"
                 "    mutating func bump(by: i64):\n"
                 "        self.x += by\n"
                 "pub func answer() -> i64:\n"
                 "    var p = Point(x = 1, y = 2)\n"
                 "    p.bump(by = 3)\n"
                 "    return p.x\n"));
}

TEST(agree_nested_call) {
    CHECK(agrees("func add(a: i64, b: i64) -> i64:\n"
                 "    return a + b\n"
                 "pub func answer() -> i64:\n"
                 "    return add(20, add(10, 12))\n"));
}

TEST(agree_print) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    print(7)\n"
                 "    print(true)\n"
                 "    return 0\n"));
}

TEST(agree_overflow_traps) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    return 9223372036854775807 + 1\n"));
}

TEST(agree_div_by_zero_traps) {
    CHECK(agrees("pub func answer() -> i64:\n    return 1 // 0\n"));
}

TEST(agree_neg_div) {
    CHECK(agrees("pub func answer() -> i64:\n    return -7 // 2\n"));
}

TEST(agree_remainder) {
    CHECK(agrees("pub func answer() -> i64:\n    return -7 % 2\n"));
}

TEST(agree_zero_var) {
    CHECK(agrees("pub func answer() -> i64:\n    var n: i64\n    return n\n"));
}

TEST(agree_u8_wrap) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: u8 = 250\n    return i64(x +% 10)\n"));
}

TEST(agree_u8_overflow_traps) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: u8 = 250\n    return i64(x + 10)\n"));
}

TEST(agree_u8_saturating) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: u8 = 250\n    return i64(x +| 10)\n"));
}

TEST(agree_c_cast_truncates) {
    CHECK(agrees("pub func answer() -> i64:\n    return i64((u8)300)\n"));
}

TEST(agree_checked_conv_traps) {
    CHECK(agrees("pub func answer() -> i64:\n    let n: i64 = 300\n    return i64(u8(n))\n"));
}

TEST(agree_widen) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: i32 = 40\n    let y: i64 = x\n    return y\n"));
}

TEST(agree_sizeof_i64) {
    CHECK(agrees("pub func answer() -> i64:\n    return i64(sizeof(i64))\n"));
}

TEST(agree_sizeof_usize) {
    CHECK(agrees("pub func answer() -> i64:\n    return i64(sizeof(usize))\n"));
}

TEST(agree_shift) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: u8 = 1\n    return i64(x << 3)\n"));
}

TEST(agree_shift_too_wide_traps) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: u8 = 1\n    return i64(x << 8)\n"));
}

TEST(agree_wrapping_neg) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: u8 = 1\n    return i64(-%x)\n"));
}

TEST(agree_bits) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: u8 = 0b1100\n    return i64(x & 0b1010)\n"));
}

TEST(agree_i64_min) {
    CHECK(agrees("pub func answer() -> i64:\n    return -9223372036854775808\n"));
}

TEST(agree_f64_to_i64) {
    CHECK(agrees("pub func answer() -> i64:\n    let x: f64 = 40.9\n    return i64(x)\n"));
}

TEST(agree_pointer_deref) {
    CHECK(agrees("pub func answer() -> i64:\n    var n: i64 = 41\n    let p = &n\n    return *p\n"));
}

TEST(agree_array_index) {
    CHECK(agrees("pub func answer() -> i64:\n    var xs: i64[3] = [10, 20, 30]\n    return xs[1]\n"));
}

TEST(agree_span_from_array) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var xs: i64[3] = [1, 2, 3]\n"
                 "    let s: i64[] = xs\n"
                 "    return s[0] + s[2]\n"));
}

TEST(agree_span_length) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var xs: i64[4]\n"
                 "    let s: i64[] = xs\n"
                 "    return i64(s.length)\n"));
}

TEST(agree_index_oob_traps) {
    CHECK(agrees("pub func answer() -> i64:\n    var xs: i64[2] = [1, 2]\n    return xs[2]\n"));
}

TEST(agree_slice) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var xs: i64[4] = [1, 2, 3, 4]\n"
                 "    let s = xs[1..<3]\n"
                 "    return s[0] + s[1]\n"));
}

TEST(agree_for_span) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var xs: i64[3] = [10, 20, 30]\n"
                 "    var n: i64 = 0\n"
                 "    for x in xs:\n"
                 "        n += x\n"
                 "    return n\n"));
}

TEST(agree_str_length) {
    CHECK(agrees("pub func answer() -> i64:\n    let t = \"hi\"\n    return i64(t.length)\n"));
}

TEST(agree_assign_through_pointer) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: i64 = 1\n"
                 "    let p = &n\n"
                 "    *p = 9\n"
                 "    return n\n"));
}

TEST(agree_optional_else) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let x: i64? = none\n"
                 "    return x else 7\n"));
}

TEST(agree_if_let) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let x: i64? = 3\n"
                 "    if let n = x:\n"
                 "        return n\n"
                 "    return 0\n"));
}

TEST(agree_overflow_optional) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let x: u8 = 250\n"
                 "    return i64(x +? 10 else 0)\n"));
}

TEST(agree_try_catch) {
    CHECK(agrees("func boom() -> i64!:\n"
                 "    error(1, \"nope\")\n"
                 "    return 0\n"
                 "pub func answer() -> i64:\n"
                 "    return boom() catch e:\n"
                 "        recover 9\n"));
}

TEST(agree_try_ok) {
    CHECK(agrees("func id(n: i64) -> i64!:\n"
                 "    return n\n"
                 "func inner() -> i64!:\n"
                 "    return try id(4)\n"
                 "pub func answer() -> i64:\n"
                 "    return inner() catch e:\n"
                 "        recover 0\n"));
}

TEST(agree_for_range) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: i64 = 0\n"
                 "    for i in 0..<5:\n"
                 "        n += i\n"
                 "    return n\n"));
}

TEST(agree_match_int) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let x = 2\n"
                 "    match x:\n"
                 "        1:\n"
                 "            return 10\n"
                 "        2:\n"
                 "            return 20\n"
                 "        _:\n"
                 "            return 0\n"));
}

TEST(agree_break) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: i64 = 0\n"
                 "    while true:\n"
                 "        n += 1\n"
                 "        if n == 3:\n"
                 "            break\n"
                 "    return n\n"));
}

TEST(agree_defer) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    defer print(7)\n"
                 "    return 1\n"));
}

TEST(agree_labeled_break) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: i64 = 0\n"
                 "    outer: while true:\n"
                 "        n += 1\n"
                 "        if n == 2:\n"
                 "            break outer\n"
                 "    return n\n"));
}
