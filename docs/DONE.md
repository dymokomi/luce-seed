# What exists

Only committed, gate-green behavior. The plan lives in [`PLAN.md`](PLAN.md).
This tree is **luce-seed-0.1**.

## Oracle, recursive structs, and first-day compiler holes

`if let` / `while let` over a function that returns `T?` now agree: the
interpreter wraps a payload as optional, matching C emit. Self-referential
`T*?` fields no longer overflow the emitter. Diagnostics from an imported
module name that file. `defer free(x)` / `errdefer free(x)` parse.
A `char` literal compares with `u8`. `mod.Enum.case` typechecks.
`memory.copy` copies any element type by count, so `List[T]` can grow.
`List[i64].create(n)` parses as a static call on an instantiated struct.
Evidence: `agree_program_while_let`, `agree_program_if_let_call`,
`agree_program_ptr_fields`, `agree_program_defer_free`,
`agree_program_char_u8`, `agree_program_qenum`,
`agree_program_generic_list`, `import_diag_names_the_imported_file`.

## Writer.write consumes fmt, process capture, seed freeze

`Writer.write` takes `const u8[]` and also consumes a formatted string or a
`str`. `process.run` answers `(i32, str, str)!` — status, stdout, stderr —
allocated from the current allocator. The freeze gate is the toy compiler
package under `testdata/programs/compile/`. This tree is luce-seed.
Evidence: `agree_writer_fmt`, `agree_program_builder`, `agree_process_run`,
`agree_program_compile`, `testdata/programs/builder.lucb`,
`testdata/programs/spawn.lucb`, `testdata/programs/compile/`.

## `Hashable` bound

`T: Hashable` and `T: Hashable & Equatable` are derived like `Equatable`.
A compiler intern table can be generic. Evidence: `agree_hashable_intern`,
`testdata/programs/map.lucb`.

## User `Allocator`

`Allocator` is the interface of §12.4. `new` / `alloc` / `free` / `with` /
`in` call `allocate` / `resize` / `release`. Heap and `FixedBuffer` remain
the builtin implementations. A user `Arena implements Allocator` can be
made current. Evidence: `agree_user_arena`, `testdata/programs/arena.lucb`.

## Memory, text, listing, spawn, hash

`memory.copy` / `move` / `set`, `memory.read[T]` / `write[T]`, and
`memory.grow` (heap realloc; FixedBuffer in-place at the bump tail).
`str(bytes)` and `str(cstr)` validate UTF-8 and yield `str!`; `(str)bytes`
stays unchecked. `files.list` names a directory. `process.run` forks,
execs, and waits. `hash` is process-seeded; `hex` / `bin` / `pad` are
Display forms. Evidence: `tests/agree_test.cpp`, `tests/eval_test.cpp`,
`tests/check_test.cpp`, `testdata/programs/memory.lucb`, `text.lucb`,
`list.lucb`, `spawn.lucb`, `hash.lucb`.

## Compile-time `luce` facts, `io`, and `files`

`luce.location`, `luce.file`, `luce.line`, and `luce.function` are
compile-time replacements at the use site (and at the call site when used
as a default). `location()` is refused. `io.stdout()` / `io.stderr()` are
`Writer`s over the C streams. `files.read` / `files.write` load and store
whole files through the current allocator. Evidence: `agree_location`,
`agree_io_stderr`, `agree_files_roundtrip`.

## Base coverage — aliases, func values, tuples

Type aliases, `func(A, B) -> R` types and function values, capture-free
lambdas, `discard`, default parameters (including named skip and
`luce.location` at the call site), tuple expressions and multiple results,
match-expression C emit, and `errdefer` C emit on the failure path.
Interpreter and C backend agree. Evidence: `tests/agree_test.cpp`,
`tests/eval_test.cpp`, `tests/check_test.cpp`, `tests/parse_test.cpp`.

## M0 — Skeleton

CMake, the `lucb` driver (`--help`, `--version`, `check`, `lex`), arena,
diagnostics with stable codes, source loading and encoding checks, `./test.sh`
with ASAN+UBSAN.

## M1 — Lexer

The lexer of `base.md` §3, §4, and the tokens of §21. `asm` suites emit `raw`
lines, not tokens. Evidence: `tests/lex_test.cpp` and `tests/source_test.cpp`.

## M14 — Atomics, volatile, threads, `asm`

`@T` atomic integers and pointers with wrapping `+=`/`-=`/`|=`/`&=`/`^=`,
`load`/`store`/`add`/`cas`/`wait`/`wake` and friends, `Ordering`, and
`atomic.fence`. `volatile T*` loads and stores. `thread.spawn` / `Handle.join`
/ `detach` / `current` / `pause` / `yield` / `sleep` over pthreads. The `sync`
module's `Mutex`, `Condition`, `Once`, and `Semaphore`, all zeroable, over
`@T` wait/wake. `asm` is rejected in the interpreter and emitted as GNU
`asm volatile` with register operands for the host architecture. Evidence:
`tests/agree_test.cpp`, `tests/eval_test.cpp`, `tests/check_test.cpp`.

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
`fmt`, and `luce.location`. Compiler Display covers scalars and `str`.
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
C backend agree. `errdefer` runs on the failure path in both. Evidence:
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
`lucb check` typechecks; `lucb eval` runs the interpreter. Untyped integer
literals infer `i64` in `let`, array literals, conditionals, and `match`
expressions; struct field defaults apply when omitted at a constructor.
Keywords after `.` are members and cases. `new T[n]` takes a count
expression; `new Type.case(...)` allocates a payload enum. Evidence:
`tests/check_test.cpp`, `tests/eval_test.cpp`, `tests/parse_test.cpp`,
`testdata/programs/`.

## M2 — Parser

Recursive-descent parser for `base.md` §21, with layered expression
precedence. Arena AST, sibling lists, s-expression dump (`lucb dump`).
Chained comparisons, `not a == b`, `class`/`spawn`, and `goto` are refused
with stable codes. Evidence: `tests/parse_test.cpp`, `./test.sh`.
