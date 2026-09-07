# What exists

Only committed, gate-green behavior. The plan lives in [`PLAN.md`](PLAN.md).
This tree is **luce-seed-0.20**, the seed `luce-base` is written against.

## 0.20: what chapters 6 and 7 of the conformance suite found

- a top-level initialiser and a module-level `assert` need constant expressions
  (`is_constant_expr`); `luce.file` is the path the compiler was given, and
  `luce.function` names `main` too;
- the escape rule covers stores into globals and through pointers, and an error
  message built from a local buffer;
- `&5` is refused; unary `-` on an untyped group takes the context's type; `1 +? 2`
  under a `u8?` adds bytes; checked arithmetic is refused on atomics; unions have
  no equality; `x == .case` takes the enum from the other operand;
- `(Kind)n` and `(func(i64) -> i64)p` parse as casts;
- the interpreter sign-extends implicit widenings, compares arrays element by
  element, and models `memory.read[T]` and `memory.write[T]` over a byte buffer
  by spreading the bytes across its cells.

## 0.19: what chapter 5 of the conformance suite found

- `span.first()`, `span.last()`, and `for (i, x) in items.indexed()` (§5.4), on spans
  and arrays, in the interpreter and the C;
- `sizeof(n.next)` measures a member, `sizeof(c.long)` and `sizeof(m.Type)` name
  types; `alignof`, a top-level constant, and arithmetic fold in an array length;
- `(func(...) -> R)?` is a nullable function everywhere a nullable pointer is:
  `none`, assignment, `if let`, `else`;
- `sizeof` of a tuple, an optional, and a fallible result includes C's tail padding;
- `(usize)p` is the address, so two objects compare unequal, and `(T*)n` brings it back;
- a struct has at least one field; an `@bool` and a forwarded `fmt` display as their
  values.

`testdata/programs/values/{span_ends,nullable_function,aggregate_sizes}` pin them.

## 0.18: what the conformance suite found in chapters 3 and 4

luce-base grew a conformance suite, one positive and one negative program per
point of the specification, and its first two chapters found these gaps here:

- a standard module's name binds only where it is imported (§3.5): `io`,
  `memory`, `files`, `process`, `thread`, `sync`, and `atomic` are bound by
  `import`, so a local named `io` is ordinary elsewhere (`builtin_module`);
- a loop label may not take a core name (`enter_loop`);
- `for character in text` walks Unicode scalars, not bytes (§5.5), in the
  interpreter and the C (`lb_utf8_scalar`);
- byte literals `b"..."` exist: `u8[N]` static data with `\xNN` escapes (§4.4);
- a `char` displays as itself in UTF-8 (§14.4), in every print path
  (`lb_utf8_encode`, `lb_fmtbuf_char`).

`testdata/programs/text/{iterate_scalars,byte_literal,char_display}` pin them,
and the unit tests now import what they use.

## 0.17: `f16`, and a parse that forgets the last one

The parser's O(1) list append cached each list's tail in a process-wide table keyed
by the list's address and head, both of which repeat once an arena is freed and
reused, so a later parse could splice a dead tree into a live one (missing nodes,
or a cycle that spun forever). The cache is now the parser's own (`ListTails`),
living exactly as long as one parse; the shared `append_node` walks.
`tests/parse_test.cpp` parses in a loop over reused arenas to pin it.


`f16` is a real binary16 (§5.1): two bytes in memory, `_Float16` in the C,
and every result rounded to half precision in the interpreter (`v_float`),
so the oracle and the binary agree on `2048.0 + 1.0`. It converts to and from
the other floats and the integers, hashes by its bits, and punned through a
union it is two bytes. `testdata/programs/values/half_floats.lucb` and
`tests/agree_test.cpp` pin it.

## 0.16: the manifest reaches the build

- `[package] symbol_prefix` starts every exported symbol (§17.6), and
  `[native] sources`, `libraries`, `link_search`, `frameworks`, and
  `pkg_config` (§17.4) reach the C compiler and the link step (`NativeInputs`
  in `emit/host`); `lucb header` spells the prefix too.
- Every `ErrorCode.package(n)` carries its package's identity in its high
  half (§11.3): sixteen bits of the manifest's name (`package_identity`),
  computed once by the checker and read by the interpreter and the emitter,
  so two packages' codes never collide.
  `testdata/programs/modules/c_inputs/` pins a C source and a prefixed export;
  `tests/pkg_test.cpp` pins the manifest fields and the identity.

## 0.15: `reg` operands and `{name}` in assembly text

A `reg` operand of an `asm` block leaves the register to the compiler, and
`{name}` in the text stands for the register chosen for the `reg` operand
whose expression is the name `name` (§8.9). The C emitter makes such an
operand a named one, `[name] "r"(...)`, and spells `{name}` as `%[name]`;
the checker refuses a `{name}` that names no `reg` operand
(`check_asm_references`). `testdata/programs/values/asm_reg_operands.lucb`
is proven by the C build; `tests/check_test.cpp` pins the rule.

## 0.14: an extern's `out` parameters are extra results

`extern func frexp(value: f64, out exponent: c.int) -> f64` is called as
`let (mantissa, exponent) = frexp(x)` (§17.1): an `out` parameter takes no
argument, the C emitter passes the address of a local for it, and the call
answers the declared result followed by every `out` value, as a tuple when
there is more than one (`extern_result`, `emit_extern_out_call`). The
interpreter cannot call C, so `testdata/programs/values/out_parameters.lucb`
is proven by the C build alone; `tests/check_test.cpp` pins the typing.

## 0.13: `for` consumes the Iterable protocol; generic interfaces

- `Iterator[T]`, `Iterable[T, I: Iterator[T]]`, and `Display` are declared
  in the `luce` module (§14.4), as Base text the checker parses into the
  builtin module (`append_builtin_text`), so `from luce import Iterator` and
  `struct Countdown: Iterator[u32]:` are ordinary declarations.
- A generic interface is checked with its parameters as opaque types, named
  with arguments as one interned instance (`intern_iface_instance`), and a
  conformance matches each requirement with the arguments substituted
  (`requirement_type`).
- `for x in source: body` over a struct with `iterator()` is rewritten by the
  checker into a block holding `var __iterN = source.iterator()` and
  `while let x = __iterN.next(): body` (§8.3): the iterator is a hidden local
  of its concrete type, and the interpreter and the emitter see a loop they
  already know. `testdata/programs/control/for_over_iterable.lucb` pins it
  in all three executions; `tests/check_test.cpp` pins the conformance rules.
- A struct that is `Display` shows itself in a formatted string (§14.4): the
  checker turns the field into `value.display(__sink)`, where `__sink` stands
  for the string's own sink (`FlagFormatSink`), the interpreter offers a sink
  that appends to the string it is building, and the C is a `Writer` over the
  `lb_fmtbuf` being filled (`lb_vt_fmtsink`) or over standard output from
  `print`. `testdata/programs/text/display_protocol.lucb` and the two test
  files pin it. Aliases in a builtin module's Base text are kept in the arena
  (`keep_texts`), since the tree outlives the checker.

## 0.12: a conditional with a `none` branch is the optional itself

`return x if c else none` in a function answering `T?` was wrapped twice in
the C: the conditional's branches are each typed as the optional and emitted
as one, so the conditional yields it (`produces_opt`). `tests/agree_test.cpp`
pins the returned, bound, and nested forms.

## 0.11: a module's aliases and extern declarations reach other modules

A `pub type Ints = Box[i64]` used to be invisible as `boxes.Ints` from another
module, as were a module's `pub extern` declarations: `pub_member` skipped
those kinds. Aliases are now resolved when their own module is checked, in
its scope, so another module's `mod.Alias` finds the type ready; the
recursion guard moved with it (`resolve_alias`).
`testdata/programs/modules/alias_across_modules/` pins it. `VERSION` is the
one place the version lives: CMake reads it and `lucb --version` reports it.

## 0.10: the `c` module as the specification has it

- C's types live in the `c` module (§5.2) and need `import c`. `c.int`,
  `c.size`, and the other fixed-width ones are the Base types by another
  name; `c.char`, `c.long`, `c.ulong`, and `c.wchar` are distinct types of
  their target width (`Type::c_name` carries the C spelling, and `c.char`
  follows the host's signedness), so `let m: i64 = n` from a `c.long` is an
  error and `i64(n)`, `c.long(m)`, `(c.long)m` are the conversions;
  `c.va_list` is opaque and only passed through to C. The C text type is
  `c.str`; `c.str` is not a name. `testdata/programs/values/c_module_types.lucb`,
  `tests/check_test.cpp`, and `tests/agree_test.cpp` pin it.
- The 0.9 rule that `from io import Writer` also made `io` visible is
  withdrawn: a `from` import brings the named declarations and nothing else
  (§16.3), and a program that also writes `io.stdout()` writes `import io`;
  `testdata/programs/modules/imports/fromonly.lucb` pins the rejection.

## 0.9: names the language keeps, names modules keep apart

- No declaration of any kind takes a core name (§3.5): a binding, a
  parameter, a function, a method, a type, a field, an enum case, or an
  alias spelled `i8`, `unit`, `error`, `pad`, and the rest of the
  dictionary is an error, so an enum with a case `i8` no longer parses as a
  program that happens to work. `tests/check_test.cpp` pins each position.
- A private `helper` or a private `Box` in two modules used to become the
  same C symbol, `lb_helper`, and the C compiler refused the program. The
  top-level declarations of an imported module now carry the module's name
  (`Node::module`), and every C spelling of a function, a type, and a
  typedef of an optional, array, result, or tuple over such a type is
  qualified by it: `lb_a_helper`, `lb_b_Box`, `lb_o_a_Box`. The entry
  module's names stay bare. `testdata/programs/modules/private_names/`
  pins it.

## 0.8: the seed builds luce-base again

`LUCB=../luce-seed/build/lucb ./build.sh` in luce-base starts from this
seed, and luce-base's gate proves the compiler it builds agrees with the
snapshot-built one. What that took: `weak` is a contextual word (§3.6), not
a reserved one; a standard module's builtin type or struct resolves by its
qualified spelling, `memory.Allocator`, `io.Writer`, `memory.FixedBuffer.over`;
an element of an array of optionals, a call through a function value, and a
conditional of optionals produce the optional itself in the C rather than a
second wrapping; and the interpreter's zero value of `T?` is `none`, so an
untouched `(i64?)[4]` element takes its `else`.

## 0.7: warnings, and what the checker removes

The checker has two outputs besides the checked tree: errors, which fail the
check, and warnings, which `-W` on any command prints and which are otherwise
silent (`DiagnosticBag::warnings`). Every warning names something the program
does not use, and the checker removes it from the tree before the interpreter
or the C emitter runs: an unused local (a name beginning with `_` is exempt),
an unused import, a private function nothing references, a statement no path
reaches, and a branch or loop whose literal condition rules it out. A removal
keeps the program's meaning: an unused binding whose initialiser may have an
effect stays as that expression, and a generic template's body is left alone,
since each instance checks it again. `testdata/programs/values/pruned_by_the_checker.lucb`
pins the answers; `tests/check_test.cpp` pins each warning, and the positive
and negative forms of `let` and `var` bindings.

## 0.6: two decisions and a value receiver

- An unused import is pruned by the checker rather than reported, so nothing
  after the checker sees it; a `from` import keeps only the names that were
  used. The specification's §16.3 says so.
- A triple-quoted text drops the newline directly after its opening
  delimiter (§4.4); `testdata/programs/text/triple_quoted_text.lucb` pins it.
- A method may be called on a value receiver, `Flags.a.name()` or a call's
  result, in the interpreter and the C; an integer-backed enum is
  forward-declared so its methods can be.

## 0.5: the oracle audited

An audit of both compilers against the specification found what the seed
got wrong as the reference, and every finding is fixed and pinned:

- Conformance is written `struct Name: Interface:`, with several interfaces
  separated by commas; `implements` is no longer a word of the language.
  The specification's grammar and every program follow.
- `weak func` and `weak var` are the attributes of §9.8; only `weak` on a
  field is full Luce. Attributes reach the C: `noinline`, `cold`, `naked`,
  `used`, `weak`, and `section("...")` become `__attribute__`.
- A `naked func` body is asm blocks only and is emitted as bare asm.
- `value.bits()` and `f64.bits(u)` / `f32.bits(u)` (§7.5) in the checker,
  the interpreter, and the C.
- A labeled loop keeps its loop variable: the label has its own field on
  the node instead of borrowing the variable's. Range loops accept
  `break label` and `continue label` in the C.
- A top-level `let` or `var` may end with a suite (a `match` or `catch`
  block), and an `asm` header may span lines inside its parentheses.
- A union reinterprets through the target's byte layout in the interpreter
  (`interp/punning`): the member last reached through a member place is
  encoded and the requested member decoded, for integers, floats, bools,
  arrays, and nested records. A member written only through a pointer taken
  earlier is not seen; the C backend is exact.
- An untyped arithmetic expression, `(256 << 32) | 7`, takes the width of
  the operand it meets, all the way down; a float literal adapts to an
  `f32` operand; a floating literal is emitted with a point.
- A method may be called on a value receiver, `Flags.a.name()` or a call's
  result, in both executions; an integer-backed enum is forward-declared so
  its methods can be.

## Found by luce-base's first two slices

Writing the lexer and parser in Base found: `x == none` on a tagged
optional; `self.use(self.take())` losing the receiver mutation made by the
argument in the oracle (arguments now evaluate before the receiver is
copied, and only `mutating` methods write it back); a method called through
a pointer-typed local taking `&p` instead of `p` in C; a diamond of imports
checking a module before its dependency (modules are now ordered
dependencies-first); and two `test` declarations colliding on a truncated
pointer in their C names (tests are numbered). Each is pinned under
`testdata/programs/`.

## 0.4: the seed for luce-base

Every compile and run works in a scratch directory that is removed when it
goes out of scope (`ScratchDir` in emit/host.h), in the compiler and in the
tests; earlier builds left one directory under /tmp per invocation.

## Performance pass

The compiler is linear in program size. Before: a 76,000-line file took
0.9 s to check and 4.9 s to emit, growing with the square of the input.
The emitter copied its entire output buffer for every `catch`, `match`
expression, and defer snapshot; it now swaps buffers. The checker scanned
every binding of every enclosing scope on each lookup; it now keeps a hashed
index of the newest binding per name that `pop_scope` unwinds. The parser
walked to the end of a sibling list on every append; a small tail cache makes
that O(1). After: 0.12 s to check and 0.18 s to emit the same file. The
oracle resolves locals by declaration pointer before name and decodes each
integer literal once. `./build.sh` builds the release compiler into `build/`,
and `./test.sh` keeps the sanitized build in `build-test/`, so the binary
`luce-base` develops against is the fast one. Host `cc` at `-O0` compiles the
164,000 lines of C that file produces in 1.5 s; `--release` (`-O2`) takes 17 s.

## Fourth round: examples shaped like compiler work

Five complete programs under `examples/` (a calculator compiler, a lexer, a
symbol table, a bytecode VM, a JSON parser) are proved by both executions
on every run, alongside every program under `testdata/`, by a directory walk
in `tests/programs_test.cpp`. Writing them found and closed: `while true:`
as a terminating loop; an `Allocator` parameter accepting a `FixedBuffer`,
an implementing struct, or a pointer to either, in the checker, the emitter,
and the oracle; `new module.Type(...)` and `module.Enum.case(...)` through
a type path; `module.constant` emitted by its global name and found by the
oracle; the standard modules' types synthesized once per program so a
`Writer` is one type in every module; `return try f()` keeping its failure
in the oracle; omitted array fields zeroed by the oracle; `T[self.count]`
as an allocation count; `if let x = try f()` on a `T?!` result; and an
untyped literal branch of a conditional taking the other branch's type,
found by the first luce-base slice.

The sources were split along their seams (`check/resolve`, `builtins`,
`memory`, `call`, `convert`, `intrinsics`; `interp/call`, `memory`, `ops`;
`emit/types`, `call`, `memory`, `text`), every file opens with a banner
saying what it owns, dead phase-ordered typedef emission is gone, and the
tree is clang-formatted. The tree is dual-licensed MIT and Apache-2.0.

## Third audit: emitter ordering, oracle fidelity, spec §24 complete

The C emitter writes typedefs in dependency order: a record, array,
tuple, optional, result, or function-pointer typedef appears only after
every type it holds by value, so a struct with an array field, an array
of function values, or a tuple of records no longer references a name
declared later. `T[]` and `const T[]` share one C struct; a slice of a
span indexes in elements, not bytes; a cast keeps its written target when
the context widens it to `T?`; exported spans and span literals cast their
data pointer to `void*`; standard-module records (`Handle`, `Mutex`, …)
come from the runtime header and a user struct with the same name keeps
its own definition (`FlagBuiltin`). Checked `+ - *` and bounds checks are
`static inline` in `lucb_rt.h` so `--release` keeps them in registers.

The oracle: an ASCII character literal adapts to a `u8` operand for every
operator, not only `==`; `defer call catch failure:` parses as the spec
writes it; a reinterpreted pointer (`(u8*)link - offsetof(...)`) is refused
with a message rather than mis-modelled; a forwarded `fmt` parameter
prints its text; `main` returning an error reports `error N: message` like
the native shim; `lucb eval FILE args…` passes arguments and prints the
program's stderr.

The spec's §24.15 example gained the `field` and `number` helpers it
called but never defined, and `load` returns the parsed default instead of
recovering a `const u8[]` where a `u8[]` was expected.

Evidence: `agree_probe_*` (37 programs), `main_*` (8 programs),
`examples`, `eval_tests_probe_t1`, `spec24_ex01`–`ex15`, and
the audit battery in the sibling `luce-seed-review/audit.sh`.

## Remaining audit items

Methods on payload enums, arrays of function values, `+? else return none`,
inline `catch`/`recover` in a call, `ErrorCode.package` as a top-level
constant, `import thread` / `import sync`, and `export` of `const u8[]` as
pointer plus count. `lucb build --release` passes `-O2`. Runtime C is
embedded in the `lucb` binary. Evidence: `agree_program_enum_methods`,
`agree_program_fnptr_table`, `agree_program_checked_else`,
`agree_program_inline_catch`, `agree_program_errorcode`,
`agree_program_thread_mod`, `agree_program_export_span`,
`header_export_span`.

An array of functions is written `(func(A) -> R)[N]`; `func(A) -> R[N]` is
a function that returns an array, per the grammar of §21.

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
the builtin implementations. A user `Arena: Allocator` can be
made current. Evidence: `agree_user_arena`, `testdata/programs/arena.lucb`.

## Memory, text, listing, spawn, hash

`memory.copy` / `move` / `set`, `memory.read[T]` / `write[T]`, and
`memory.grow` (heap realloc; FixedBuffer in-place at the bump tail).
`str(bytes)` and `str(c.str)` validate UTF-8 and yield `str!`; `(str)bytes`
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
one `null_foreign` boundary check, `lucb header`, and `c.str` in foreign
signatures. Interpreter covers a libc subset (`abs`, `strlen`, `printf`)
so it can agree with the C backend. `out` parameters, `luce bind`, and
`[native]` sources wait. Evidence: `tests/agree_test.cpp`,
`tests/eval_test.cpp`, `tests/check_test.cpp`.

## M12 — Interfaces, `fmt`, `Writer`

Nominal conformance, two-word interface views with a vtable, builtin
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
`pub` names cross modules. `pub func main(arguments: str[]|c.str[]) -> i32|i32!`
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
