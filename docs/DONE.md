# What exists

Only committed, gate-green behavior. The plan lives in [`PLAN.md`](PLAN.md).

## M0 — Skeleton

CMake, the `lucb` driver (`--help`, `--version`, `check`, `lex`), arena,
diagnostics with stable codes, source loading and encoding checks, `./test.sh`
with ASAN+UBSAN.

## M1 — Lexer

The lexer of `base.md` §3, §4, and the tokens of §21. `asm` suites emit `raw`
lines, not tokens. Evidence: `tests/lex_test.cpp` and `tests/source_test.cpp`.

## M14 — Atomics, volatile, threads, `asm`

`@T` atomic integers and pointers with wrapping `+=`/`-=`/`|=`/`&=`/`^=`,
`load`/`store`/`add` and friends, `Ordering`, and `atomic.fence`. `volatile T*`
loads and stores. `thread.spawn` / `Handle.join` over pthreads. `asm` is
rejected in the interpreter and emitted as GNU `asm volatile` for the host
architecture. Evidence: `tests/agree_test.cpp`, `tests/eval_test.cpp`,
`tests/check_test.cpp`.

## M13 — Calling C

`extern func` / `type` / `var` / `struct` / `union`, `as "name"`, `export`
with unprefixed C symbols, the `c` module aliases, variadic C calls, the
one `null_foreign` boundary check, `lucb header`, and `cstr` in foreign
signatures. Interpreter covers a libc subset (`abs`, `strlen`, `printf`)
so it can agree with the C backend. `out` parameters, `luce bind`, and
`[native]` sources wait. Evidence: `tests/agree_test.cpp`,
`tests/eval_test.cpp`, `tests/check_test.cpp`.

## M12 — Interfaces, `fmt`, `Writer`

Nominal `implements`, two-word interface views with a vtable, builtin
`Writer` and `Location`, `print(f"...")`, `format(buffer, fmt) -> str!`,
`fmt`, and `location()`. Compiler Display covers scalars and `str`.
Evidence: `tests/agree_test.cpp`, `tests/eval_test.cpp`, `tests/check_test.cpp`.

## M11 — Generics

Generic functions and structs with declaration-time checking of an
opaque type parameter. Calls infer type arguments or take them in
`name[T](...)`. Instantiations are monomorphised. `T: Comparable` is
the one constraint in this slice (`compare` on integers, floats, `char`,
`str`). User interfaces wait for M12. Evidence: `tests/agree_test.cpp`,
`tests/eval_test.cpp`, `tests/check_test.cpp`.

## M10 — Allocation and `memory`

`new` / `alloc` / `free` / `with` / `in`. Builtin `Allocator` view,
`FixedBuffer.over(u8[])`, `CAllocator()`, `memory.allocator` /
`memory.heap` / `memory.exhausted`. Interpreter and C backend agree.
Evidence: `tests/agree_test.cpp`, `tests/eval_test.cpp`, `tests/check_test.cpp`.

## M9 — Modules, packages, `main`, `test`

`import path` and `from path import Name` resolve `.lucb` files from a
`luce.toml` package root. Unused and duplicate imports are errors; only
`pub` names cross modules. `pub func main(arguments: str[]|cstr[]) -> i32|i32!`
is the process entry. `test "name":` runs under `lucb test` with `assert`.
Evidence: `tests/pkg_test.cpp`, `testdata/m9/`.

## M8 — Enums, unions, zeros, globals, layout

Payload enums with exhaustive `match`, integer-backed `enum as u32` with
`|` `&` `^` `~` and checked `T(n)`, C unions, zero values for structs and
unions, `---` uninitialised locals, module `var` / `thread_local var`,
`packed` / `align(N)`, `offsetof`. Interpreter and C backend agree.
Evidence: `tests/agree_test.cpp`, `tests/eval_test.cpp`, `tests/check_test.cpp`.

## M7 — Optionals, errors, match, ranges, defer

`T?` with `none` / `else` / `if let` / `while let`. `T!` with `try`,
`error`, `catch`, `recover`. Exhaustive `match` on integers, bools, and
optionals. `for` over `0..<n` / `1..=n`. Labeled `break`/`continue`.
`defer` LIFO on scope exit. `+?` overflow yields `none`. Interpreter and
C backend agree. `errdefer` runs in the interpreter. Evidence:
`tests/agree_test.cpp`, `tests/eval_test.cpp`, `tests/check_test.cpp`.

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
