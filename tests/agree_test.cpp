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

TEST(agree_int_enum) {
    CHECK(agrees("enum Access as u32:\n"
                 "    empty = 0\n"
                 "    read = 1\n"
                 "    write = 2\n"
                 "pub func answer() -> i64:\n"
                 "    let mode = Access.read | Access.write\n"
                 "    return i64((u32)mode)\n"));
}

TEST(agree_payload_enum) {
    CHECK(agrees("enum Cmd:\n"
                 "    quit\n"
                 "    go(n: i64)\n"
                 "pub func answer() -> i64:\n"
                 "    let c = Cmd.go(7)\n"
                 "    match c:\n"
                 "        .quit:\n"
                 "            return 0\n"
                 "        .go(n):\n"
                 "            return n\n"));
}

TEST(agree_union) {
    CHECK(agrees("union Value:\n"
                 "    integer: i64\n"
                 "    real: f64\n"
                 "pub func answer() -> i64:\n"
                 "    var v: Value\n"
                 "    v.integer = 11\n"
                 "    return v.integer\n"));
}

TEST(agree_zero_struct) {
    CHECK(agrees("struct Point:\n"
                 "    var x: i64\n"
                 "    var y: i64\n"
                 "pub func answer() -> i64:\n"
                 "    var p: Point\n"
                 "    return p.x + p.y\n"));
}

TEST(agree_uninit) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: i64 = ---\n"
                 "    n = 4\n"
                 "    return n\n"));
}

TEST(agree_global) {
    CHECK(agrees("var hits: i64\n"
                 "func bump():\n"
                 "    hits += 1\n"
                 "pub func answer() -> i64:\n"
                 "    bump()\n"
                 "    bump()\n"
                 "    return hits\n"));
}

TEST(agree_offsetof_packed) {
    CHECK(agrees("packed struct H:\n"
                 "    var a: u8\n"
                 "    var b: u32\n"
                 "pub func answer() -> i64:\n"
                 "    return i64(offsetof(H, b))\n"));
}

TEST(agree_enum_checked_conv_traps) {
    CHECK(agrees("enum Access as u32:\n"
                 "    empty = 0\n"
                 "    read = 1\n"
                 "pub func answer() -> i64:\n"
                 "    return i64((u32)Access(3))\n"));
}

TEST(agree_thread_local) {
    CHECK(agrees("thread_local var n: i64 = 5\n"
                 "pub func answer() -> i64:\n"
                 "    return n\n"));
}

TEST(agree_writer_view) {
    CHECK(agrees("struct Sink implements Writer:\n"
                 "    var n: usize\n"
                 "    mutating func write(bytes: const u8[]) -> usize!:\n"
                 "        self.n += bytes.length\n"
                 "        return bytes.length\n"
                 "pub func answer() -> i64!:\n"
                 "    var s = Sink(n = 0)\n"
                 "    let w: Writer = &s\n"
                 "    let n = try w.write(\"ab\".bytes)\n"
                 "    return i64(n)\n"));
}

TEST(agree_interface_view) {
    CHECK(agrees("interface Counter:\n"
                 "    mutating func bump() -> i64\n"
                 "struct Box implements Counter:\n"
                 "    var n: i64\n"
                 "    mutating func bump() -> i64:\n"
                 "        self.n += 1\n"
                 "        return self.n\n"
                 "pub func answer() -> i64:\n"
                 "    var b = Box(n = 10)\n"
                 "    let c: Counter = &b\n"
                 "    return c.bump()\n"));
}

TEST(agree_print_formatted) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    print(f\"n={40}\")\n"
                 "    return 0\n"));
}

TEST(agree_format) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    var buf: u8[32]\n"
                 "    let s = try format(buf, f\"{7}\")\n"
                 "    return i64(s.length)\n"));
}

TEST(agree_location) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let loc = location()\n"
                 "    return i64(loc.line)\n"));
}

TEST(agree_generic_id) {
    CHECK(agrees("func id[T](x: T) -> T:\n"
                 "    return x\n"
                 "pub func answer() -> i64:\n"
                 "    return id(40)\n"));
}

TEST(agree_generic_first) {
    CHECK(agrees("func first[T](values: const T[]) -> T?:\n"
                 "    if values.length == 0:\n"
                 "        return none\n"
                 "    return values[0]\n"
                 "pub func answer() -> i64:\n"
                 "    let xs: i64[3] = [10, 20, 30]\n"
                 "    return first(xs) else 0\n"));
}

TEST(agree_generic_pair) {
    CHECK(agrees("struct Pair[A, B]:\n"
                 "    var first: A\n"
                 "    var second: B\n"
                 "pub func answer() -> i64:\n"
                 "    let p = Pair[i64, i64](first = 3, second = 4)\n"
                 "    return p.first + p.second\n"));
}

TEST(agree_generic_pair_infer) {
    CHECK(agrees("struct Pair[A, B]:\n"
                 "    var first: A\n"
                 "    var second: B\n"
                 "pub func answer() -> i64:\n"
                 "    let p = Pair(first = 3, second = 7)\n"
                 "    return p.first + p.second\n"));
}

TEST(agree_generic_comparable) {
    CHECK(agrees("func largest[T: Comparable](left: T, right: T) -> T:\n"
                 "    if left.compare(right) >= 0:\n"
                 "        return left\n"
                 "    return right\n"
                 "pub func answer() -> i64:\n"
                 "    return largest(3, 9)\n"));
}

TEST(agree_new_i64) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    let p = try new i64\n"
                 "    *p = 42\n"
                 "    let n = *p\n"
                 "    free(p)\n"
                 "    return n\n"));
}

TEST(agree_new_span) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    var items = try new i64[3]\n"
                 "    items[0] = 10\n"
                 "    items[1] = 20\n"
                 "    items[2] = 30\n"
                 "    let n = items[0] + items[1] + items[2]\n"
                 "    free(items)\n"
                 "    return n\n"));
}

TEST(agree_new_array_ptr) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    let p = try new (i64[2])\n"
                 "    let n = (*p)[0] + (*p)[1]\n"
                 "    free(p)\n"
                 "    return n\n"));
}

TEST(agree_new_struct) {
    CHECK(agrees("struct Node:\n"
                 "    var value: i64\n"
                 "    var next: i64\n"
                 "pub func answer() -> i64!:\n"
                 "    let p = try new Node(value = 3, next = 4)\n"
                 "    let n = p.value + p.next\n"
                 "    free(p)\n"
                 "    return n\n"));
}

TEST(agree_alloc_span) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    var items = try alloc i64[2]\n"
                 "    items[0] = 5\n"
                 "    items[1] = 6\n"
                 "    let n = items[0] + items[1]\n"
                 "    free(items)\n"
                 "    return n\n"));
}

TEST(agree_alloc_raw) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    var raw = try alloc(16, 8)\n"
                 "    let n = i64(raw.length)\n"
                 "    free(raw)\n"
                 "    return n\n"));
}

TEST(agree_callocator) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    var a = CAllocator()\n"
                 "    let p = try new i64 in a\n"
                 "    *p = 9\n"
                 "    let n = *p\n"
                 "    free(p) in a\n"
                 "    return n\n"));
}

TEST(agree_fixed_buffer) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    var buf: u8[64]\n"
                 "    var fb = FixedBuffer.over(buf)\n"
                 "    with fb:\n"
                 "        let p = try new i64\n"
                 "        *p = 11\n"
                 "        let n = *p\n"
                 "        free(p)\n"
                 "        return n\n"));
}

TEST(agree_fixed_exhausted) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    var buf: u8[4]\n"
                 "    var fb = FixedBuffer.over(buf)\n"
                 "    with fb:\n"
                 "        let p = new i64 catch:\n"
                 "            return 1\n"
                 "        return 0\n"));
}

TEST(agree_memory_heap) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    memory.allocator = memory.heap\n"
                 "    let p = try new i64\n"
                 "    *p = i64(memory.exhausted)\n"
                 "    let n = *p\n"
                 "    free(p)\n"
                 "    return n\n"));
}
