# What exists

Only committed, gate-green behavior. The plan lives in [`PLAN.md`](PLAN.md).
This tree is **luce-seed-0.68**, the seed `luce-base` is written against.

## 0.68: a tuple is not a pattern

- `match p: (0, 0) => ...` (§8.4): the parser refuses a tuple literal where a pattern is
  expected, and the checker now refuses one too should a tree carry it; a pattern is a
  literal, a case, a range, a name or `_`. Evidence: `tests/parse_test.cpp`.

## 0.67: a chain may be three hundred links

- Calls and operators chained, `a + a + ...`, deepen the tree one level per link and are
  bounded at three hundred, where nesting proper is bounded at a hundred: luce-base's
  `long_assert` conformance program is a real one. Evidence: `tests/parse_test.cpp`.

## 0.66: a cast chain is bounded like any nesting

- `(u8)(u8)(u8)...x`: casts and prefix operators recurse without the expression entry
  that counts nesting, so a long chain was a stack fault; the bound, 100 levels, is
  kept in `parse_unary` too. Found by luce-base's mutation fuzzer. Evidence:
  `tests/parse_test.cpp`.

## 0.65: a constant's value stands in for its name in a file-scope initialiser

- `var g: S = S(f = flag)` and `var h = consts.base * 2` (§6.4): C reads no global in a
  file-scope initialiser, so the `let`'s initialiser is written in its place, qualified or
  not; the interpreter binds the globals in passes, so one may name a constant of a module
  loaded after its own. Found by luce's immortal text literals. Evidence:
  `tests/agree_test.cpp`, luce-base's `16_modules/constant_initialisers`.

## 0.64: a tuple is a type argument

- `Box[(i64, str)]`, in an expression and in a type (§13): the bracket lookahead read the
  parenthesis as an index or an array length. The spec's list of what a type argument may
  be now names tuples. Wanted by luce's lists of tuples. Evidence: `tests/agree_test.cpp`.

## 0.63: a `const` span of pointers iterates

- `for item in items` over a `const T[]` with `T` a pointer: the loop's element type came
  from the loop variable and dropped the pointer, and the qualifier sat on the pointee; C
  wants `T* const` (§5.3). Found by luce's runtime. Evidence: `tests/agree_test.cpp`.

## 0.62: an instance over another generic's parameter is not emitted

- `Box[T]*` written inside `first[T]`, as a cast or a binding's type: the instance over
  `first`'s parameter types the template's body and is substituted per real instantiation;
  0.61 emitted it as a C struct with a `void` field. Found by luce's runtime. Evidence:
  `tests/agree_test.cpp`.

## 0.61: a caught optional keeps its own presence

- `let empty = lookup(0) catch: recover 1` with `lookup` returning `i64?!`: the
  interpreter of 0.60 marked the good value present because the expression is an
  optional; only a `T` becoming the expected `T?` is. Found by luce-base's conformance
  suite. Evidence: `tests/agree_test.cpp`.

## 0.60: a `catch` takes the optional expected of it

- `let n: i64? = parse(text) catch: recover none` (§11.4): where a `T?` is expected and the
  handled value is a `T`, the expression is the `T?`, the handler recovers with it, and the
  value on the good path is wrapped. Checker, C emitter and interpreter. Evidence:
  `tests/agree_test.cpp`.

## 0.59: an interface method is called through a pointer to the view

- `out.write(...)` where `out: Writer*` auto-dereferences (§7.3): the C emitter copied the
  pointer where the fat pointer belonged and the C compiler refused it. Found building luce's
  compiler through C. Evidence: `tests/agree_test.cpp`.

## 0.58: a doubly parenthesised guard is not a lambda either

- `_ if ((a > b)) => x`: a `(` inside a would-be parameter list opens a tuple type only after
  `:`, `(p: (i64, i64)) => p.0 + p.1`; anywhere else the parentheses hold an expression.
  Evidence: `tests/agree_test.cpp`.

## 0.57: a parenthesised guard is not a lambda

- `_ if (n < 0) => x` in a match expression was read as a lambda `(n < 0) => x`; a lambda's
  parameter list holds names with optional types, so anything else before `=>` is a
  parenthesised expression. Found by luce's emitter. Evidence: `tests/agree_test.cpp`.

## 0.56: a tuple member is read by position

- `pair.0`, `pair.1` (§5.7), in the parser, the checker, the interpreter and the C emitter,
  which names the member `a0`, `a1` as it always has. Luce reads tuples this way.
  Evidence: `tests/agree_test.cpp`.

## 0.55: an array literal passed as a span is an array of the function

- `total(["a", "b"])` was written as a brace list cast to a span, which C refuses; the literal
  is now an array declared at the function's top, as format buffers are, and the span views
  it. Found compiling luce's runtime through luce-base, whose C backend had the sister
  defect (a block-scoped temporary). Evidence: `tests/agree_test.cpp`.

## 0.54: a C string literal escapes what C could misread

- `??)` in a Base text reached C as a trigraph and clang refused it under `-Werror`; a NUL,
  a `\r`, a control or a non-ASCII byte were written raw. Every such byte is escaped now,
  `\?` and three-digit octal. Found by luce-base compiling luce. Evidence:
  `tests/agree_test.cpp`.

## 0.53: a cast inside an index is an index

- `ap[(usize)i].x` was read as a generic instantiation because a bracket beginning with `(`
  counted as a type argument list; only `(func(...) -> R)` begins one. From luce-base's
  fuzzer, which had worked around it. Evidence: `tests/check_test.cpp`.

## 0.52: an `else` fallback is the payload; a struct with a default or an `init` has no zero value

- `o = (o else 3)`: the fallback was checked under the optional the result is stored into,
  so it became `i64?` and was refused as not matching the payload. It is checked as the
  payload now. A struct that declares a field default or a custom `init` is no longer
  zeroable (§6.1). Both from luce-base's widened fuzzer. Evidence: `tests/check_test.cpp`.

## 0.51: an identifier is at most 128 bytes

- The lexer refuses a longer identifier (`lucb.lex.identifier`, §3.1), and a module's file
  name without `.lucb` must be an identifier (§16.1), so a diagnostic quoting a name has
  a bound and a file name never reaches C as a bad symbol. Both rules came from luce-base's
  fuzzer. Evidence: `tests/lex_test.cpp`, luce-base's conformance rejections.

## 0.50: a cast gives its operand no context

- `(u8)(200 + 100)` was checked in `u8` and trapped; the operand of a plain cast now has no
  context, computes as `i64`, and is converted after, so it is 44 and `(u32)(1 << 40)` is 0
  (§7.5). A bare literal is still read in the cast's type: `(u64)18446744073709551615`.
- Every literal in an untyped expression must fit the type the expression takes:
  `18446744073709551615 - 1` as an `i64` and `-(9223372036854775808)` are refused, where
  before they were read as negative values. Only `-literal` is a negative literal (§4.2).
  Found by luce-base's fuzzer. Evidence: `tests/agree_test.cpp`, `tests/check_test.cpp`.

## 0.49: an untyped integer computes at sixty-four bits

- `(i64)((104 >> 2) >> 0)`: an untyped integer expression under a cast had no width in the
  interpreter, so every shift count was "out of range", and the C emitter spelled its
  type `void`. Both now take `i64`'s width and spelling, the default of §4.2. Found by
  luce-base's fuzzer, which compares the interpreter with the compiled program.

## 0.48: a `c` type as a generic argument

- `List[c.str]` reads as a type application: the parser's lookahead took `module.Name`
  as a type only when the name was capitalised, and the `c` module's types are lowercase
  (§5.2). `tests/check_test`.

## 0.47: the exported surface of §17.6

- A fallible function may be exported: the specification's status form is what
  `luce-base` writes, and the checker no longer refuses it as "not in this slice".
- A span inside a function pointer's signature is not C-representable; a span is only a
  function's own parameter, which the wrapper takes as a pointer and a length.

## 0.46: a formatted text outlives its expression

- The C emitter declares every format buffer at the top of the function, not inside the
  statement expression that fills it: a `fmt` argument is read by the callee after that
  expression has ended, which `-O2` showed by reusing the stack. The program tests take a
  `# release: true` line to run the `-O2` build beside the `-O0` one and compare.
- The specification's §3.5 says what both compilers do: a local or a parameter may not
  shadow a declaration of its module either.

## 0.45: float literals rounded once, and finite

- A decimal float literal is converted exactly (`support/decimal`): big-integer arithmetic
  and one rounding, ties to even, to the width the literal has, so an `f32` or `f16`
  literal is never rounded through a double (§4.3). The emitted C spells the literal as
  the hexadecimal float of those bits, which the C compiler reads exactly.
- A literal that would round to an infinity is refused; the infinities are `math`'s
  constants. The `f16` suffix is accepted as `f32`'s is.
- The generated C is compiled with `-fno-strict-aliasing`: Base has no type-based
  aliasing rule (§12.6), and the C must not be optimised as if it had one.

## 0.44: a discarded asm output on a register the block reads

- `out("eax") _` beside `in("eax") 1u32` (`cpuid`) is a dummy output operand in the C, not
  a clobber: GCC refuses a clobber of a register an input variable is bound to, clang
  does not, which is how the Linux gate found it; the seed's gate is green on Linux.

## 0.43, 0.42: instruction-set levels and vectors

- recorded in the commit log: vectors (§5.12) with eleven tests, and the `platform`
  module carrying the instruction-set level (§19.5).

## 0.41: the seed on Linux x86_64

- the tree builds and its gate is green on x86_64 Linux with GCC 15 and clang 21 as well
  as on arm64 macOS; what it took is what a second host always finds:
- arguments are evaluated left to right (§7.1) whatever the C compiler does: a call whose
  arguments may have an effect computes them into temporaries in order
  (`testdata/programs/values/evaluation_order.lucb`); GCC evaluates C arguments right to
  left, clang left to right, so the bug was invisible on the first host;
- `f64.bits(N)` with constant bits is a hexadecimal float literal (`0x1.5555555555555p-2`),
  or `__builtin_inf`/`__builtin_nan` with the payload, which every C compiler folds; GCC
  has no `__builtin_bit_cast` in C;
- a label's `unused` attribute follows the colon, the form both compilers accept;
  `fread`'s result is not ignored; the fixed allocator's overflow check compares sizes,
  not pointers;
- the `platform` standard module (§19.5) is decided when the seed is compiled for its host,
  so `import platform` and `os`-style constants read the host's target;
- a section name is the language's spelling, `section(".custom")`; a Mach-O target places
  it in `__DATA` or `__TEXT` (§9.8); the assembly test programs carry an `asm x86_64` arm
  beside the arm64 one, and a module-level symbol is defined under both the C-underscore and
  the bare ELF spelling;
- a top-level `let` or `var` and a generic instance are qualified by their module in the C,
  so two modules may declare `failed`, `count`, or a `Home` used as a type argument
  (`testdata/programs/modules/same_names`); the interpreter assigns through
  `module.variable`;
- the interpreter's frames are a deque: a receiver's address stays valid while calls push
  frames (libstdc++'s vector copied the frames on growth, which ASan caught on Linux).

## 0.40: the escape rule through aggregates

- a tuple or array literal holding a local's address is as local as that address, and a
  result or a stored value whose type holds a view anywhere inside, a tuple, an array, a
  struct, or an optional of one, is checked like the view itself (§6.6): `return (1, &n)`
  is refused.

## 0.39: what the expanded conformance programs found

- `let (_, rest) = pair()`: a discarded tuple element parses in every position, binds no
  name, and the C output names nothing for it;
- a method on a place receives the place by address in the interpreter, so `&self` is the
  object and `c.add(2).add(3)` mutates `c` through the returned pointer;
- an element read from a local array is a copy: a `str` from a local table may be
  returned; only `&a[i]` and a slice view the frame (§6.6).

## 0.38: an interface view's optional everywhere

- `Writer?` in `else`, `let ... else`, and `match .some(name)`: the view's null niche is
  read wherever a pointer's would be (§14.3), in the checker and the interpreter.

## 0.37: constant conditions decide branches

- `if os.arm64:`, `if bits == 64:`: a condition that is a constant, a top-level `let`
  of `bool` or a comparison of constants, is decided by the checker and the branch it
  rules out is pruned, without a warning (§19.6); `if true:` keeps its warning. Only a
  top-level `let` is a constant: a `var` with an initialiser never folds (§6.4).

## 0.36: the math module as source, and three decisions

- `math` is Base source carried in the binary (`std/math.lucb`, the same file as
  luce-base's `src/std/math.lucb`): `import math` and `from math import sqrt` both
  work, and the interpreter reaches the C library's mathematics for its bodies;
  `f64.bits(...)` is a constant expression in the C output;
- `(i32)flag` is 0 or 1 and `(bool)n` is `n != 0`, as in C (§7.5);
- `&a[N]` is the one-past-the-end address (§7.7);
- `(c.str)text` traps unless the byte after the text is NUL (§5.2).

## 0.35: constants across modules, pointers in the interpreter

- an array length names a constant of an imported module, `i32[base.NCmp]`, and that
  constant's initialiser may name constants of its own module (§6.4, §16.3);
- the interpreter indexes a pointer value, `(p + n)[i]`, and addresses the bytes of a
  `str`, `&text.bytes[0]`.

## 0.34: what porting QBE found

- a bracket holding one bare name is read by what the name resolves to: `i32[NIns]`
  is an array and `insb[NIns].f` an index when `NIns` is a constant (§5.4, §7.6);
  brackets holding arithmetic or literals are never type arguments; a run of
  brackets reads inside-out as in C, so `u8[2][4]` is two arrays of four;
- `(bits)1` and `(lib.bits)1` cast through a scalar alias; `lib.bits` is a type
  wherever a type is read (§5.10, §7.5);
- `pub` before a union member (§10.4);
- a string literal fills a `c.str?` slot; an ASCII character literal indexes
  (§4.4, §5.2); `&global[i]` and `&global.field` are constant addresses (§6.4);
- the escape rule stops at a pointer or a view: `row.next` read through a local
  `row` is not local, `text.bytes` of a `str` parameter is not local, and an
  imported module's global is a global (§6.6);
- `assert` reports `file:line: assert failed: condition` (§11.6);
- `-3.0e38` in an `f32` context is an `f32`; an infinity saturates in a float to
  integer cast, only NaN answers 0 (§5);
- an `extern func` is declared under a private C name bound to its symbol by an
  asm label, so bindings of `fputc` and its kind never clash with `<stdio.h>`.

## 0.33: `c.str?` crosses the boundary

- an `extern` or `export` signature admits `c.str?`, a `char*` that may be
  null (§17.1); the callee's `none` becomes C's null and back.

## 0.32: what chapters 20 and 22 of the conformance suite found

- `luce.line` and its siblings bind only where `luce` is imported (§3.5), like
  every standard module; the checker reaches the module's own declarations
  through `builtin_module`;
- a statement is a call, alone or under `try`, `catch`, or parentheses (§7.9):
  `--n` and every other unused expression are refused.

## 0.31: `local var`

- the keyword `thread_local` is `local` (base.md §3.6, §6.3): no reserved word
  carries an underscore; `local` is reserved, so no name may be `local`.

## 0.30: what chapter 15 of the conformance suite found

- an `@T` takes a `T` as its initial value, in a binding or a field; `if let`
  reads an `@T?` through its load;
- `cas` refuses a failure ordering stronger than the success ordering; the
  arithmetic and bit methods need an integer atomic; the interpreter's `cas`
  compares pointers by address;
- `thread.spawn` takes `func(void*) -> unit`; `Mutex.try_lock` (the spec's
  `try` is a keyword); the interpreter gives a spawned thread fresh
  `local` variables;
- `_` in a tuple binding names a value nothing reads.

## 0.29: what chapter 14 of the conformance suite found

- `Writer?` keeps `none` in the null niche: `if let` unwraps it to the view and
  `view == none` tests it;
- a view is formed from a pointer, never from a value; `&` takes a place, so the
  address of a temporary is refused;
- `Comparable`, `Equatable`, and `Hashable` are nameable interfaces of the `luce`
  module (`void*` in a builtin requirement stands for the conforming type); a
  struct's own `compare` serves a `Comparable` bound; Equatable and Hashable
  conformance written by hand is refused;
- a requirement's `mutating` is part of its exact signature, both ways.

## 0.28: what chapter 13 of the conformance suite found

- an instantiation is checked in its module's top-level scope (`enter_module_scope`),
  never among the use site's locals, which shadowed its parameters;
- a generic built inside a template is an instance too (`Pair[B, A]` inside
  `Pair[A, B]`), an instance of a clone is an instance of the clone's generic
  (`generic_origin`), and a generic's fields are typed before any signature names
  an instance of it;
- inference sees through function-typed arguments and matches an instance
  against its generic;
- `compare` under a `Comparable` bound comes before an interface bound's
  methods; a parameter bound by `Display` formats, and a scalar satisfies
  `Display`;
- a type parameter has no zero value: `var v: T` and `new T[n]` are refused;
- an instantiation nested deeper than sixteen is an infinite chain.

## 0.27: what chapter 12 of the conformance suite found

- `free` hands the block back: a user allocator's `release` runs with the block
  in bytes, a `FixedBuffer` takes back its last block, the heap needs nothing;
- `memory.heap` answers `allocate`, `resize`, and `release` as a view;
- `in allocator` binds below `catch`, so `new T in arena catch e:` handles the
  allocation; `alloc (T)[n]` parenthesises a type;
- a grouped place, `(*p)[i] = v`, is assignable and an lvalue;
- `new T[count]` needs a zeroable `T`.

## 0.26

- the C backend wraps a value into an optional once: a group and a conditional
  whose branches are optionals produce the optional themselves, and a unary or
  binary operator under an optional context computes in the payload's type;
- CMake reconfigures when `VERSION` changes, so `lucb --version` is current.

## 0.25

- a unary operator under an optional context computes in the payload's type:
  `return ~v` in a function returning `u64?`.

## 0.24: what chapter 11 of the conformance suite found, continued

- a `break` or `continue` from a `catch` handler steers the loop over an array,
  a span, or text as it does a `while` or a range: every loop consumes the jump
  through one `leave_loop`;
- `error` needs a fallible function, inside a handler or not.

## 0.23: what chapter 11 of the conformance suite found

- a module-level `assert` is decided at compile time (`const_bool`): a false one
  is a compile error naming its message, as C's `static_assert`;
- `format` on a local array yields a local view, and `try` keeps the mark, so a
  message formatted on a local buffer is refused wherever it reaches `error`.

## 0.22: what chapter 10 of the conformance suite found

- a memberwise initialiser is positional or named: an unnamed value takes the
  field at its position, none may follow a named one;
- an integer-backed enum gives every case a constant that fits its
  representation, negative only when it is signed, no two alike; the checker
  folds the value and the backends read it, so `low = -1` in an `i8` enum is
  the byte 0xFF and `L(n)` finds it;
- a custom `init` returns `unit` or `!`, assigns every field exactly once, and
  neither reads a field nor calls a method before every field is assigned
  (`check/init.cpp`); a failing `init` fails the construction in the interpreter;
- a struct cannot contain itself by value, directly or through another aggregate;
- `==` needs equality in every component, so a struct holding a union has none;
  a union has at least one member;
- a `catch` handler for a value must `recover` or leave; a handler for `unit`
  may fall through;
- the interpreter's union keeps its bytes across member changes, so a `u8`
  member's write leaves the rest of a `u16` member as C does.

## 0.21: what chapters 8 and 9 of the conformance suite found

- a deferred call runs whole on `continue`, `break`, and `return`: the jump in
  flight is set aside while it runs;
- a string pattern compares its text, so `"two", "deux" => 2` matches "deux";
- `match` refuses a duplicate pattern, a pattern after an unguarded `_`, and
  alternatives that bind different names;
- a deferred call must produce `unit` and a fallible one must carry `catch`;
- a label already open may not be reopened; a range needs integer bounds;
- a default argument is a constant expression; no function returns `fmt`.

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
unions, `---` uninitialised locals, module `var` / `local var`,
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

## 0.69 — `handle` declarations (§17.7)

`pub handle Name:` with `destroy function` parses to an extern type whose `right` names
the destroyer; the checker requires a `pub` function of the module taking the handle and
returning `unit` without failing; an opaque handle converts to and from `void*` by cast
(§7.5), which the seed always allowed. Evidence: `tests/parse_test.cpp` (`parse_handle`),
`tests/check_test.cpp` (`check_handle_destroy`).

## 0.70 — a cast to a named type before a parenthesised operand

`(Counter)(void*)p` is a cast of a cast: a capitalised name in parentheses before `(` is
a type (§3.4), as luce-base reads it; the parser took it for a call of a value. Evidence:
`tests/parse_test.cpp` (`parse_cast_of_a_cast_to_a_named_type`).

## 0.71 — bracketed values and name patterns

`(NPtr) * n` with `NPtr` a value: the parser's cast (0.70) is rewritten by the checker
into the binary expression, as luce-base does (§7.5). A bare name is not a pattern (§8.4):
the checker rejects it instead of compiling a comparison no reader expects. Evidence:
`tests/check_test.cpp` (`check_bracketed_value_times_operand`, `check_name_is_not_a_pattern`).

## 0.72 — an enum cannot contain itself

An enum whose payload holds the enum by value, directly or through an optional, a tuple,
an array or a struct, is rejected as a struct is (§10.2); the checker accepted it and
emitted C that never compiled. Evidence: `tests/check_test.cpp`
(`check_enum_contains_itself`).

## 0.73 — a `match` expression ends with its arms

`let w = match n: ...` followed by `if w == 1:` on the next line: the parser read the `if`
as a conditional continuing the last arm (§7.8), and the same for an operator, an `else`
or a `catch`. An expression whose last operand ended a suite is complete. Evidence:
`tests/parse_test.cpp` (`parse_match_expression_ends_its_line`).

## 0.74 — a nested tuple is a type argument

`Box[((i64, i64), str)]`: the lookahead deciding between an array length and type
arguments knew a tuple type only by a type word after `(`; a nested `(` opens one too
(§13.2), as luce-base reads it. Evidence: `tests/parse_test.cpp`
(`parse_nested_tuple_type_argument`).
