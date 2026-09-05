# What exists

Only committed, gate-green behavior. The plan lives in [`PLAN.md`](PLAN.md).

## M0 — Skeleton

CMake, the `lucb` driver (`--help`, `--version`, `check`, `lex`), arena,
diagnostics with stable codes, source loading and encoding checks, `./test.sh`
with ASAN+UBSAN.

## M1 — Lexer

The lexer of `base.md` §3, §4, and the tokens of §21. `asm` suites emit `raw`
lines, not tokens. Evidence: `tests/lex_test.cpp` and `tests/source_test.cpp`.

## M6 — Pointers, arrays, spans, `str`

`T*` never-null pointers, `&` / `*`, auto-deref, `T[N]` values, `T[]` spans
with checked index/slice, `str.length` / `str.bytes`, `for` over arrays and
spans, and the local-escape rule on return. `T*?` is a nullable pointer;
other optionals wait for M7. Evidence: `tests/agree_test.cpp`,
`tests/eval_test.cpp`, `tests/check_test.cpp`.

## M5 — Numbers, casts, `usize`

Integer widths, `usize`/`isize` as pointer width, wrapping (`+%`) and
saturating (`+|`) arithmetic, shifts, implicit same-signedness widening,
checked `T(x)` vs C `(T)x`, `sizeof`. `f32`/`f64` convert. Not `f16`, not
`+?` (needs optionals). Evidence: `tests/agree_test.cpp`, `tests/eval_test.cpp`,
`tests/check_test.cpp`.

## M4 — C backend

Scalar-core programs compile to C, then to a host executable with
`lucb build`. Checked arithmetic and traps live in `src/runtime`.
Interpreter and native binary agree on stdout and trap reasons.
Evidence: `tests/agree_test.cpp`.

## M3 — Check and interpreter

Scalar core: `i64`, `bool`, `unit`, structs with implicit `self`, checked
`+ - * // %`, `print`, `trap`. Entry is `pub func answer() -> i64`.
`lucb check` typechecks; `lucb eval` runs the interpreter. Evidence:
`tests/check_test.cpp`, `tests/eval_test.cpp`, `testdata/programs/hello.lucb`.

## M2 — Parser

Recursive-descent parser for `base.md` §21, with layered expression
precedence. Arena AST, sibling lists, s-expression dump (`lucb dump`).
Chained comparisons, `not a == b`, `class`/`spawn`, and `goto` are refused
with stable codes. Evidence: `tests/parse_test.cpp`, `./test.sh`.
