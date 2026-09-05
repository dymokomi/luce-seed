# Implementation plan

Each slice is independently reviewable. Do not start N+1 until N’s tests are
green. The spec is [`language/base.md`](language/base.md).

## M0 — Skeleton

CMake, `lucb --help` / `--version`, arena, diagnostics, source positions,
`./test.sh`, this documentation tree, an empty FEATURES ledger.

## M1 — Lexer

`base.md` §3, §4, and the tokens of §21.

- UTF-8, BOM-at-zero ignored, reject NUL / invalid UTF-8 / bidi / tabs / lone CR.
- Confusable punctuation diagnosed with the ASCII to write, in code (not in
  strings or comments: Unicode is fully supported there).
- CRLF accepted; positions keep byte and line mappings.
- 4-space `INDENT`/`DEDENT`; inside `()`, `[]`, `{}` newlines are spacing;
  `:` ending a line inside delimiters reopens layout.
- `#` comments drop out; `##` at the start of a logical line is `doc_comment`.
- Keywords from §3.6. `class` / `spawn` / `weak` are keywords so they cannot
  be identifiers; the parser will refuse them with a tier diagnostic.
- Base operators, longest first: `+%` `-%` `*%` `+|` `-|` `*|` `+?` `-?` `*?`
  and their assignments, `---`, `...`, `@`.
- Literals: ints (hex/oct/bin/underscores/suffixes), floats, char, `"..."`,
  `b"..."`, `r"..."`, triple quotes, `f"..."` as `format_start` / `format_text`
  / nested tokens / `format_end`.

## M2 — Parser (full grammar)

Recursive descent for declarations and statements; layered precedence for
expressions (§7.10). Arena AST. Recovery: one diagnostic per mistake, force
progress if a production consumes nothing. **Landed.**

## M3 — Check + interpreter: the scalar core

`i64`, `bool`, `unit`, `let`/`var`, `if`/`while`/`return`, `func`, `struct`
with implicit `self`, checked arithmetic with truncating `//`, `print`.
Test entry `pub func answer() -> i64` until `main` exists. **Landed.**

## M4 — C backend for the scalar core

Emit C from the checked AST. Link `lucb_rt.c` + `start.c`. Interpreter and
compiled binary must agree. **Landed.**

## M5 — Numbers, casts, `usize`

All scalars of §5.1. Wrapping / saturating / checked operators. Implicit
same-signedness widening. `T(x)` vs `(T)x`. `sizeof`. Host pointer width.
**Landed** (`f16` and `+?` wait; `offsetof` waits on layout tests).

## M6 — Pointers, arrays, spans, `str`

`T*`, `const`/`volatile`, `void*`, null-niche `T*?`, `&`, auto-deref,
pointer arithmetic, `T[N]`, `T[]`, checked index/slice, `str` as a view,
`for` over spans. Escape rule §6.6. **Landed.**

## M7 — Optionals, errors, defer, match, for-range, labels

`T?`, `if let`/`while let`, `T!`, `try`/`error`/`catch`/`recover`/`errdefer`.
Exhaustive `match`. Ranges. Labeled `break`/`continue`. `defer`.
**Landed.** (`errdefer` is interpreted; C emit of `errdefer` waits.)

## M8 — Enums, unions, zeros, globals, layout

Payload enums, integer-backed `enum as u32`, unions, zero values, `---`,
module `var` / `thread_local var`, `packed` / `align(N)`, `offsetof`.
**Landed.**

## M9 — Modules, packages, `main`, `test`

`import`, `luce.toml`, `pub func main(arguments: str[]|cstr[]) -> i32|i32!`,
`test "...":`, `lucb test`.
**Landed.**

## M10 — Allocation and `memory`

`new` / `alloc` / `free` / `with` / `in`. `Allocator`. `FixedBuffer` and
`CAllocator`. `memory.exhausted`.
**Landed.**

## M11 — Generics (monomorphise)

Functions and structs. Constraints. Declaration-time checking.

## M12 — Interfaces, `fmt`, `Display`, `Writer`

Nominal `implements`. Two-word views. `print` / `format` / `fmt` / `location()`.

## M13 — Calling C

`extern` / `export`, `c` module, variadic calls, the one null-boundary check,
generated header. `cstr`.

## M14 — Atomics, volatile, threads, `asm`

Last. Interpreter rejects `asm` (§8.9); C backend only.

## After this compiler

A new tree, `luce-base`, written in Base, compiled by `lucb`. Same shape as
Zig after the C++ compiler could emit C:

1. `lucb` builds `luce-base` to C, host `cc` makes a binary (`zig2`).
2. That binary's first backend is still C, so it can rebuild itself without
   this C++ tree.
3. QBE or a machine backend is added in `luce-base`, not here (`zig3`).
4. Freeze a seed (C dump or wasm) so C++ can go away (`zig1.wasm`).

Out of scope in this repo. See [`DESIGN.md`](DESIGN.md).
