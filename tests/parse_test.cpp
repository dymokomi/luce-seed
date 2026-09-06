//==============================================================================================
//
//   tests/parse_test - Syntax
//
//   DESCRIPTION:
//       Every declaration and statement form, expression precedence, cast disambiguation, and
//       the forms Base refuses with a stable code.
//
//==============================================================================================

#include "lex/lexer.h"
#include "parse/parser.h"
#include "source/source.h"
#include "support/arena.h"
#include "support/diagnostics.h"
#include "support/test.h"

using lucb::Arena;
using lucb::DiagnosticBag;
using lucb::dump_tree;
using lucb::Node;
using lucb::parse;
using lucb::ParseResult;
using lucb::Source;
using lucb::Token;
using lucb::tokenize;

struct Parsed {
    DiagnosticBag diagnostics;
    Arena arena;
    Source source;
    ParseResult result;

    explicit Parsed(const char* text, const char* path = "t.lucb")
        : source(Source::from_bytes(path, text, diagnostics)) {
        if (!source.ok()) {
            return;
        }
        std::vector<Token> tokens = tokenize(source, diagnostics);
        if (!diagnostics.empty()) {
            return;
        }
        result = parse(source, tokens, arena, diagnostics);
    }

    std::string dump() const {
        if (result.module == nullptr) {
            return "";
        }
        return dump_tree(result.module);
    }

    bool has(const char* code) const {
        return diagnostics.has_code(code);
    }
};

TEST(parse_asm_header_spans_lines) {
    Parsed p("func w(fd: i32) -> i64:\n    var result: i64 = 0\n    asm arm64 (in(\"x0\") fd,\n               out(\"x0\") result):\n        mov x0, x0\n    return result\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(asm") != std::string::npos);
}

TEST(parse_top_level_binding_ends_with_suite) {
    Parsed p("let c: i64 = 5\nlet kind = match c:\n    1 => 1\n    _ => 42\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(match") != std::string::npos);
}

TEST(parse_conformance_list) {
    Parsed p("interface A:\n    func a() -> i64\ninterface B:\n    func b() -> i64\nstruct S: A, B:\n    var x: i64\n    func a() -> i64:\n        return 1\n    func b() -> i64:\n        return 2\nstruct Plain:\n    var y: i64\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(struct \"S\"") != std::string::npos);
}

TEST(parse_implements_is_no_longer_a_keyword) {
    Parsed p("struct S implements A:\n    var x: i64\n");
    CHECK(p.has("lucb.parse.expect"));
}

TEST(parse_hello_function) {
    Parsed p("pub func answer() -> i64:\n    var counter = 40\n    return counter\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(func pub \"answer\"") != std::string::npos);
    CHECK(p.dump().find("(var \"counter\"") != std::string::npos);
    CHECK(p.dump().find("(return") != std::string::npos);
}

TEST(parse_same_line_suite) {
    Parsed p("pub func f() -> i64:\n    if cached: return 1\n    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(if") != std::string::npos);
}

TEST(parse_struct_and_method) {
    Parsed p("struct Point:\n"
             "    var x: i64\n"
             "    var y: i64\n"
             "\n"
             "    mutating func bump(by: i64):\n"
             "        self.x += by\n"
             "\n"
             "pub func origin() -> i64:\n"
             "    var p = Point(x = 1, y = 2)\n"
             "    p.bump(by = 3)\n"
             "    return p.x\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(struct \"Point\"") != std::string::npos);
    CHECK(p.dump().find("mutating") != std::string::npos);
    CHECK(p.dump().find("(member") != std::string::npos);
}

TEST(parse_if_elif_else) {
    Parsed p("func f(x: i64) -> i64:\n"
             "    if x > 0:\n"
             "        return 1\n"
             "    elif x < 0:\n"
             "        return -1\n"
             "    else:\n"
             "        return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(if") != std::string::npos);
    CHECK(p.dump().find("(unary -") != std::string::npos);
}

TEST(parse_while_and_for) {
    Parsed p("func f(n: i64) -> i64:\n"
             "    var i = 0\n"
             "    while i < n:\n"
             "        i += 1\n"
             "    for x in 0..<n:\n"
             "        i += x\n"
             "    return i\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(while") != std::string::npos);
    CHECK(p.dump().find("(for") != std::string::npos);
    CHECK(p.dump().find("..<") != std::string::npos);
}

TEST(parse_match) {
    Parsed p("enum Color:\n"
             "    red\n"
             "    blue\n"
             "\n"
             "func f(c: Color) -> i64:\n"
             "    match c:\n"
             "        .red: return 1\n"
             "        .blue: return 2\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(enum \"Color\"") != std::string::npos);
    CHECK(p.dump().find("(match") != std::string::npos);
    CHECK(p.dump().find("(pat \"red\"") != std::string::npos);
}

TEST(parse_pointer_and_span_types) {
    Parsed p("func f(p: i64*, s: const u8[], n: Node*?) -> i64:\n"
             "    return *p + s.length\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("type") != std::string::npos);
}

TEST(parse_wrapping_operators) {
    Parsed p("func f(a: i64, b: i64) -> i64:\n    return a +% b *| 2\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("+%") != std::string::npos);
    CHECK(p.dump().find("*|") != std::string::npos);
}

TEST(parse_chained_comparison_is_an_error) {
    Parsed p("func f(a: i64, b: i64, c: i64) -> bool:\n    return a < b < c\n");
    CHECK(p.has("lucb.parse.chain"));
}

TEST(parse_not_before_comparison_is_an_error) {
    Parsed p("func f(a: i64, b: i64) -> bool:\n    return not a == b\n");
    CHECK(p.has("lucb.parse.precedence"));
}

TEST(parse_class_belongs_to_full_luce) {
    Parsed p("class Foo:\n    var x: i64\n");
    CHECK(p.has("lucb.parse.tier"));
}

TEST(parse_import) {
    Parsed p("import image.color\nfrom image.geometry import Point\n"
             "func f() -> i64:\n    return 1\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(import \"image.color\")") != std::string::npos);
    CHECK(p.dump().find("(from \"image.geometry\"") != std::string::npos);
}

TEST(parse_union) {
    Parsed p("union Value:\n"
             "    integer: i64\n"
             "    real: f64\n"
             "\n"
             "func f(v: Value) -> i64:\n"
             "    return v.integer\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(union \"Value\"") != std::string::npos);
}

TEST(parse_extern_func) {
    Parsed p("extern func printf(format: cstr, ...) -> i32\n"
             "func f() -> i32:\n"
             "    return printf(\"hi\")\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(extern_func \"printf\"") != std::string::npos);
    CHECK(p.dump().find("variadic") != std::string::npos);
}

TEST(parse_lambda) {
    Parsed p("func f() -> i64:\n    let add = (x, y) => x + y\n    return add(1, 2)\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(lambda") != std::string::npos);
}

TEST(parse_type_alias) {
    Parsed p("type Pixel = f32[4]\n"
             "type Callback = func(void*, i32) -> unit\n"
             "func f(p: Pixel) -> i64:\n"
             "    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(alias \"Pixel\"") != std::string::npos);
    CHECK(p.dump().find("(alias \"Callback\"") != std::string::npos);
}

TEST(parse_tuple_expr) {
    Parsed p("func f() -> (i64, i64):\n    return (1, 2)\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(tuple") != std::string::npos);
}

TEST(parse_discard) {
    Parsed p("func f():\n    discard(1)\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("\"discard\"") != std::string::npos);
}

TEST(parse_default_args) {
    Parsed p("func f(n: i64 = 1, at: Location = luce.location) -> i64:\n    return n\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("\"location\"") != std::string::npos);
}

TEST(parse_cast_vs_call) {
    Parsed p("func f(value: i64) -> u8:\n"
             "    let low = (u8)value\n"
             "    let same = (value)\n"
             "    return low\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(cast") != std::string::npos);
    CHECK(p.dump().find("(group") != std::string::npos);
}

TEST(parse_array_literal) {
    Parsed p("func f() -> i64:\n    let xs = [1, 2, 3]\n    return xs[0]\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(array") != std::string::npos);
    CHECK(p.dump().find("(index") != std::string::npos);
}

TEST(parse_try_and_error_call) {
    Parsed p("func f() -> i64!:\n"
             "    let x = try g()\n"
             "    return x\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(unary try") != std::string::npos);
}

TEST(parse_formatted_string) {
    Parsed p("func f(n: i64):\n    print(f\"n={n}\")\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(fmt") != std::string::npos);
}

TEST(parse_defer) {
    Parsed p("func f():\n    defer close()\n    open()\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(defer") != std::string::npos);
}

TEST(parse_labeled_loop) {
    Parsed p("func f() -> i64:\n"
             "    rows: for y in 0..<10:\n"
             "        break rows\n"
             "    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(for \"y\" label=\"rows\"") != std::string::npos);
    CHECK(p.dump().find("(break \"rows\")") != std::string::npos);
}

TEST(parse_generic_func) {
    Parsed p("func first[T](values: const T[]) -> T?:\n"
             "    return values[0]\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(generics") != std::string::npos);
    CHECK(p.dump().find("(generic \"T\")") != std::string::npos);
}

TEST(parse_generic_struct) {
    Parsed p("struct Pair[A, B]:\n"
             "    var first: A\n"
             "    var second: B\n"
             "func f() -> i64:\n"
             "    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(generics") != std::string::npos);
}

TEST(parse_new_alloc) {
    Parsed p("func f() -> i64:\n"
             "    let n = try new Node(value = 1)\n"
             "    let buf = try new u8[16]\n"
             "    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(new") != std::string::npos);
}

TEST(parse_test_declaration) {
    Parsed p("test \"adds one\":\n    assert(1 + 1 == 2)\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(test") != std::string::npos);
}

TEST(parse_defer_free) {
    Parsed p("pub func answer() -> i64!:\n"
             "    let bytes = try alloc u8[4]\n"
             "    defer free(bytes)\n"
             "    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(defer") != std::string::npos);
    CHECK(p.dump().find("(free") != std::string::npos);
}

TEST(parse_interface) {
    Parsed p("interface Writer:\n"
             "    mutating func write(bytes: const u8[]) -> usize!\n"
             "\n"
             "func f():\n"
             "    return\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(interface \"Writer\"") != std::string::npos);
}

TEST(parse_enum_as_integer) {
    Parsed p("enum Access as u32:\n"
             "    empty = 0\n"
             "    read = 1\n"
             "\n"
             "func f() -> Access:\n"
             "    return Access.read | Access.empty\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(enum \"Access\"") != std::string::npos);
}

TEST(parse_keyword_is_not_a_case_name) {
    Parsed p("enum Kind:\n    func\n    struct_\n");
    CHECK(p.has("lucb.parse.expect"));
}

TEST(parse_keyword_is_not_a_field_name) {
    Parsed p("struct Node:\n    var type: i64\n");
    CHECK(p.has("lucb.parse.expect"));
}

TEST(parse_goto_is_reserved) {
    Parsed p("func f():\n    goto done\n");
    CHECK(p.has("lucb.parse.reserved"));
}

TEST(parse_try_as_member) {
    Parsed p("func f(lock: sync.Mutex) -> bool:\n    return lock.try()\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(member \"try\"") != std::string::npos);
}

TEST(parse_slice_from_zero) {
    Parsed p("func f(xs: i64[]) -> i64:\n    return xs[..<2][0]\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(slice") != std::string::npos);
}

TEST(parse_new_count_name) {
    Parsed p("func f(n: usize) -> i64:\n    let p = try new i64[n]\n    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(new") != std::string::npos);
}

TEST(parse_new_enum_case) {
    Parsed p("enum Expr:\n    number(value: i64)\n"
             "func f() -> i64:\n    let p = try new Expr.number(value = 1)\n    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(case_value \"number\"") != std::string::npos ||
          p.dump().find("number") != std::string::npos);
}

TEST(parse_keyword_member) {
    Parsed p("func f(t: Token) -> i64:\n    return t.func\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(member \"func\"") != std::string::npos);
}

TEST(parse_keyword_case) {
    Parsed p("func f(t: TokenKind) -> i64:\n    match t:\n        .func: return 1\n        _: "
             "return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(pat \"func\"") != std::string::npos);
}

TEST(parse_sizeof_ptr) {
    Parsed p("func f() -> usize:\n    return sizeof(Node*)\n");
    CHECK(p.diagnostics.empty());
}

TEST(parse_c_int_ptr_cast) {
    Parsed p("func f(p: void*) -> i64:\n    let q = (c.int*)p\n    return 0\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(cast") != std::string::npos);
}

TEST(parse_same_line_compound) {
    Parsed p("func f() -> i64:\n    if x: if y: return 1\n    return 0\n");
    CHECK(p.has("lucb.parse.suite"));
}

TEST(parse_interface_junk_recovers) {
    Parsed p("interface Writer:\n    var x: i64\nfunc f() -> i64:\n    return 0\n");
    CHECK(p.has("lucb.parse.expect"));
}

TEST(parse_variadic_base_rejected) {
    Parsed p("func log(fmt: cstr, ...) -> i32:\n    return 0\n");
    CHECK(p.has("lucb.parse.expect"));
}

TEST(parse_let_needs_init) {
    Parsed p("func f() -> i64:\n    let x: i64\n    return 0\n");
    CHECK(p.has("lucb.parse.expect"));
}

TEST(parse_weak_field_marker_belongs_to_full_luce) {
    Parsed p("struct S:\n    weak var n: i64\nfunc f() -> i64:\n    return 0\n");
    CHECK(p.has("lucb.parse.tier"));
}

TEST(parse_weak_attribute_is_base) {
    Parsed p("weak var n: i64 = 0\nweak func f() -> i64:\n    return 0\n");
    CHECK(p.diagnostics.empty());
}

TEST(parse_spawn_belongs_to_full_luce) {
    Parsed p("spawn func f() -> i64:\n    return 0\n");
    CHECK(p.has("lucb.parse.tier"));
}

TEST(parse_import_not_top) {
    Parsed p("func f() -> i64:\n    return 1\nimport util\n");
    CHECK(p.has("lucb.parse.import"));
}

TEST(parse_const_without_star) {
    Parsed p("func f(x: const i64) -> i64:\n    return 0\n");
    CHECK(p.has("lucb.parse.type"));
}

TEST(lex_asm_body_is_raw) {
    lucb::DiagnosticBag diagnostics;
    lucb::Source source = lucb::Source::from_bytes(
        "t.lucb", "func f():\n    asm x86_64:\n        syscall\n        # not a comment\n",
        diagnostics);
    CHECK(source.ok());
    std::vector<Token> tokens = tokenize(source, diagnostics);
    CHECK(diagnostics.empty());
    bool saw_raw = false;
    bool saw_hash_as_name = false;
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i].kind == lucb::TokenKind::RawLine) {
            saw_raw = true;
            if (tokens[i].text.find('#') != std::string_view::npos) {
                saw_hash_as_name = true;
            }
        }
    }
    CHECK(saw_raw);
    CHECK(saw_hash_as_name);
}

TEST(parse_asm) {
    Parsed p("func f():\n    asm x86_64:\n        syscall\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(asm \"x86_64\"") != std::string::npos);
}

TEST(parse_else_return) {
    Parsed p("func bump(start: usize, size: usize) -> usize?:\n"
             "    let end = start +? size else return none\n"
             "    return end\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(return") != std::string::npos);
}

TEST(parse_inline_catch_recover) {
    Parsed p("func f() -> i64:\n"
             "    discard(g() catch failure: recover 0)\n"
             "    return 1\n");
    CHECK(p.diagnostics.empty());
    CHECK(p.dump().find("(recover") != std::string::npos);
}
