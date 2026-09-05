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

#include <fstream>
#include <sstream>
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

static std::string slurp(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
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

static bool agrees_file(const char* path) {
    std::string src = slurp(path);
    if (src.empty()) {
        std::fprintf(stderr, "    missing %s\n", path);
        return false;
    }
    return agrees(src.c_str());
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

TEST(agree_extern_abs) {
    CHECK(agrees("extern func abs(n: i32) -> i32\n"
                 "pub func answer() -> i64:\n"
                 "    return i64(abs(-40))\n"));
}

TEST(agree_extern_strlen) {
    CHECK(agrees("extern func strlen(s: cstr) -> usize\n"
                 "pub func answer() -> i64:\n"
                 "    return i64(strlen(\"hello\"))\n"));
}

TEST(agree_variadic_printf) {
    CHECK(agrees("extern func printf(format: cstr, ...) -> i32\n"
                 "pub func answer() -> i64:\n"
                 "    return i64(printf(\"%d\", 40))\n"));
}

TEST(agree_c_int) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: c.int = 40\n"
                 "    return i64(n)\n"));
}

TEST(agree_export_twice) {
    CHECK(agrees("export func twice(n: i32) -> i32:\n"
                 "    return n + n\n"
                 "pub func answer() -> i64:\n"
                 "    return i64(twice(20))\n"));
}

TEST(agree_null_foreign) {
    CHECK(agrees("extern func lb_null_probe() -> i64*\n"
                 "pub func answer() -> i64:\n"
                 "    let p = lb_null_probe()\n"
                 "    return *p\n"));
}

TEST(agree_extern_as_name) {
    CHECK(agrees("extern func labs_of as \"abs\"(n: i32) -> i32\n"
                 "pub func answer() -> i64:\n"
                 "    return i64(labs_of(-7))\n"));
}

TEST(agree_atomic_add) {
    CHECK(agrees("var hits: @u64\n"
                 "pub func answer() -> i64:\n"
                 "    hits += 1\n"
                 "    hits += 1\n"
                 "    return i64(hits)\n"));
}

TEST(agree_atomic_method) {
    CHECK(agrees("var hits: @u64\n"
                 "pub func answer() -> i64:\n"
                 "    let prev = hits.add(3, .relaxed)\n"
                 "    return i64(hits) + i64(prev)\n"));
}

TEST(agree_volatile_store) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: i64 = 40\n"
                 "    let p: volatile i64* = &n\n"
                 "    *p = 41\n"
                 "    return *p\n"));
}

TEST(agree_thread_spawn) {
    CHECK(agrees("func run(context: void*):\n"
                 "    let p = (i64*)context\n"
                 "    *p = 40\n"
                 "pub func answer() -> i64!:\n"
                 "    var n: i64 = 0\n"
                 "    let h = try thread.spawn(run, (void*)(&n))\n"
                 "    try h.join()\n"
                 "    return n\n"));
}

TEST(agree_atomic_cas) {
    CHECK(agrees("var hits: @u64\n"
                 "pub func answer() -> i64:\n"
                 "    let (ok, old) = hits.cas(0, 7)\n"
                 "    if ok:\n"
                 "        return i64(old) + i64(hits)\n"
                 "    return 0\n"));
}

TEST(agree_atomic_wait) {
    CHECK(agrees("var hits: @u64\n"
                 "pub func answer() -> i64:\n"
                 "    hits = 1\n"
                 "    hits.wait(0)\n"
                 "    hits.wake(0)\n"
                 "    return i64(hits)\n"));
}

TEST(agree_thread_current) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    thread.pause()\n"
                 "    thread.yield()\n"
                 "    thread.sleep(0)\n"
                 "    let h = thread.current()\n"
                 "    if h.id == 0:\n"
                 "        return 0\n"
                 "    return 1\n"));
}

TEST(agree_sync_mutex) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var lock: sync.Mutex\n"
                 "    if not lock.try():\n"
                 "        return 0\n"
                 "    if lock.try():\n"
                 "        return 0\n"
                 "    lock.unlock()\n"
                 "    lock.lock()\n"
                 "    lock.unlock()\n"
                 "    return 1\n"));
}

TEST(agree_sync_once) {
    CHECK(agrees("var n: i64 = 0\n"
                 "func bump():\n"
                 "    n += 1\n"
                 "pub func answer() -> i64:\n"
                 "    var once: sync.Once\n"
                 "    once.run(bump)\n"
                 "    once.run(bump)\n"
                 "    return n\n"));
}

TEST(agree_sync_cond) {
    CHECK(agrees("struct Ctx:\n"
                 "    var mu: sync.Mutex\n"
                 "    var cv: sync.Condition\n"
                 "    var done: i64\n"
                 "func signaler(context: void*):\n"
                 "    let p = (Ctx*)context\n"
                 "    p.mu.lock()\n"
                 "    p.done = 1\n"
                 "    p.cv.signal()\n"
                 "    p.mu.unlock()\n"
                 "pub func answer() -> i64!:\n"
                 "    var ctx: Ctx\n"
                 "    let h = try thread.spawn(signaler, (void*)(&ctx))\n"
                 "    ctx.mu.lock()\n"
                 "    while ctx.done == 0:\n"
                 "        ctx.cv.wait(&ctx.mu)\n"
                 "    ctx.mu.unlock()\n"
                 "    try h.join()\n"
                 "    return ctx.done\n"));
}

TEST(agree_sync_sem) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var sem: sync.Semaphore\n"
                 "    sem.release()\n"
                 "    sem.release()\n"
                 "    sem.acquire()\n"
                 "    sem.acquire()\n"
                 "    return 1\n"));
}

TEST(agree_array_infer) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let xs = [1, 2, 3]\n"
                 "    return xs[0] + xs[2]\n"));
}

TEST(agree_str_bytes) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let s = \"hi\"\n"
                 "    return i64(s.bytes[0])\n"));
}

TEST(agree_field_default) {
    CHECK(agrees("struct Style:\n"
                 "    var width: i64 = 1\n"
                 "    var color: i64 = 2\n"
                 "pub func answer() -> i64:\n"
                 "    let s = Style(width = 3)\n"
                 "    return s.color + s.width\n"));
}

TEST(agree_conditional_let) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let n = 1 if true else 2\n"
                 "    return n\n"));
}

TEST(agree_generic_span) {
    CHECK(agrees("func largest[T: Comparable](values: const T[]) -> T?:\n"
                 "    if values.length == 0:\n"
                 "        return none\n"
                 "    var best = values[0]\n"
                 "    for value in values:\n"
                 "        if value.compare(best) > 0:\n"
                 "            best = value\n"
                 "    return best\n"
                 "pub func answer() -> i64:\n"
                 "    let numbers = [3, 9, 4]\n"
                 "    let top = largest(numbers) else 0\n"
                 "    return top\n"));
}

TEST(agree_program_ast) { CHECK(agrees_file("testdata/programs/ast.lucb")); }

TEST(agree_program_ring) { CHECK(agrees_file("testdata/programs/ring.lucb")); }

TEST(agree_program_scan) { CHECK(agrees_file("testdata/programs/scan.lucb")); }

TEST(agree_program_table) { CHECK(agrees_file("testdata/programs/table.lucb")); }

TEST(agree_slice_from_zero) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    let xs: i64[4] = [1, 2, 3, 4]\n"
                 "    let s = xs[..<2]\n"
                 "    return s[0] + s[1]\n"));
}

TEST(agree_new_count_var) {
    CHECK(agrees("pub func answer() -> i64!:\n"
                 "    let n: usize = 3\n"
                 "    var items = try new i64[n]\n"
                 "    items[0] = 10\n"
                 "    items[1] = 20\n"
                 "    items[2] = 30\n"
                 "    let s = items[0] + items[1] + items[2]\n"
                 "    free(items)\n"
                 "    return s\n"));
}

TEST(agree_new_enum_case) {
    CHECK(agrees("enum Expr:\n"
                 "    number(value: i64)\n"
                 "pub func answer() -> i64!:\n"
                 "    let p = try new Expr.number(value = 7)\n"
                 "    match *p:\n"
                 "        .number(value):\n"
                 "            return value\n"));
}

TEST(agree_ptr_int_cast) {
    CHECK(agrees("pub func answer() -> i64:\n"
                 "    var n: i64 = 41\n"
                 "    let p = &n\n"
                 "    let a = (usize)p\n"
                 "    let q = (i64*)a\n"
                 "    return *q\n"));
}

TEST(agree_escape_global) {
    CHECK(agrees("var g: i64 = 40\n"
                 "func addr() -> i64*:\n"
                 "    return &g\n"
                 "pub func answer() -> i64:\n"
                 "    return *addr()\n"));
}

TEST(agree_keyword_enum) {
    CHECK(agrees("enum TokenKind:\n"
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

TEST(agree_sizeof_ptr) {
    CHECK(agrees("struct Node:\n"
                 "    var n: i64\n"
                 "pub func answer() -> i64:\n"
                 "    let s = sizeof(Node*)\n"
                 "    if s == 0:\n"
                 "        return 0\n"
                 "    return 1\n"));
}

TEST(native_asm_add) {
    Both both;
    const char* src = "pub func answer() -> i64:\n"
                      "    var result: i64 = 0\n"
                      "    asm arm64 (in(\"x0\") 7, out(\"x0\") result):\n"
                      "        add x0, x0, #1\n"
                      "    asm x86_64 (in(\"rax\") 7, out(\"rax\") result):\n"
                      "        addq $1, %rax\n"
                      "    return result\n";
    CHECK(compile_source(src, &both));
    CHECK(both.compiled);
    CHECK_EQ(both.native.exit_code, 0);
    CHECK(both.native.out.find("8") != std::string::npos);
}

TEST(header_export_func) {
    DiagnosticBag diagnostics;
    Source source = Source::from_bytes("t.lucb",
                                       "export func twice(n: i32) -> i32:\n"
                                       "    return n + n\n"
                                       "pub func answer() -> i64:\n"
                                       "    return i64(twice(20))\n",
                                       diagnostics);
    CHECK(source.ok());
    Arena arena;
    std::vector<Token> tokens = tokenize(source, diagnostics);
    CHECK(diagnostics.empty());
    lucb::ParseResult parsed = parse(source, tokens, arena, diagnostics);
    CHECK(diagnostics.empty());
    CHECK(parsed.module != nullptr);
    CHECK(check_module(parsed.module, arena, diagnostics, "t.lucb"));
    std::string h = lucb::emit_header(parsed.module);
    CHECK(h.find("int32_t twice(") != std::string::npos);
    CHECK(h.find("lb_twice") == std::string::npos);
}
