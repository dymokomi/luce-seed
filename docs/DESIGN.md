# Design of luce-base-c

A C++ bootstrap compiler for Luce Base. The language specification is
[`language/base.md`](language/base.md). This file is the architecture of the
compiler, not of the language.

## Why this compiler exists

The previous stack started at the wrong end: a Zig compiler for full Luce,
then a Luce compiler that treated Base as a *profile* of the same pipeline.
One lexer, one parser, one IR, two languages. That is the wrong bootstrap.

The intended chain is Zig's, with Luce names:

```text
luce-base-c  (C++, this repo)     compiles Luce Base to C, then a binary
     ↓
luce-base    (written in Base)    compiles itself; C first, then native
     ↓
luce-full    (written in Base)    compiles full Luce
```

This compiler never parses, types, or emits full Luce. `.luc` and `.lucn` are
rejected. There is no profile flag.

## How Zig actually bootstraps

Zig did not put a native backend in the C++ compiler and keep it. They tried
the heavy path, paid for it, and replaced it. We copy the path they kept.

### What they threw away (2015–2022)

The C++ compiler (`stage1`, ~80k lines) emitted LLVM IR, then object, then a
binary. The self-hosted compiler was written in Zig beside it. Every language
change had to land twice. Nobody wanted the C++. Peak RSS to build from
source was ~11 GiB. Andrew Kelley, *Goodbye to the C++ Implementation of
Zig*, 2022-12-07:

> new Zig language features had to be implemented twice

In 2020 they already knew the C++ compiler should emit C, not LLVM
([ziglang/zig#5246](https://github.com/ziglang/zig/issues/5246)):

> Use system C compiler to compile .c source files into zig1
> Use zig1 to compile .zig source files into .c
> Use system C compiler to compile those .c files into zig2
> Use zig2 to compile .zig source files into zig

They never finished that plan inside C++. They deleted the C++ compiler
instead.

### What they kept (2022–now)

The source of truth is `bootstrap.c` (~200 lines) and `CMakeLists.txt` in
[ziglang/zig](https://codeberg.org/ziglang/zig). From a C compiler only:

```text
cc          stage1/wasm2c.c              →  zig-wasm2c
zig-wasm2c  stage1/zig1.wasm             →  zig1.c
cc          zig1.c + stage1/wasi.c       →  zig1          (C backend only)
zig1        -ofmt=c src/main.zig         →  zig2.c
zig1        -ofmt=c lib/compiler_rt.zig  →  compiler_rt.c
cc          zig2.c + compiler_rt.c       →  zig2          (self-hosted, no LLVM)
zig2 build                               →  zig3          (production)
```

`zig3` compiling itself is `zig4`. They are byte-identical, so the name
drops the suffix.

`zig1.wasm` is a frozen *self-hosted* compiler, not the old C++ one. It is
built by `zig build update-zig1`: LLVM backend, `wasm32-wasi`,
`ReleaseSmall`, **every backend disabled except C**. Size is ~2.4 MiB.
Update it only when the compiler needs a new language feature or a bug fix
*to compile itself*. Ordinary bug fixes do not touch the blob. `git bisect`
works on any commit because the seed lives in that commit.

`wasm2c.c` + `wasi.c` are ~4k lines of portable C99. They implement only the
WASI calls `zig1` actually uses when it emits C (open, read, write, args,
clock, …). No sandbox. They tried a real interpreter first; a run that
takes 5 seconds native took hours. Translating wasm to C and letting `cc`
optimise it is the bootstrap JIT.

### Why C, not LLVM, not a native ISA, not checked-in C

They enumerated the options in the same post:

| Idea | Who does it | Why Zig rejected it |
| --- | --- | --- |
| Don't self-host | Lua | They want to write the compiler in Zig. |
| Prior binary as seed | Rust | Breaks `git bisect`; you only bootstrap on targets that already have a binary. |
| Check in generated C | Nim | Zig's C dump of the compiler was ~80 MiB *and target-specific*. |
| Clean up that C and maintain it | their 2020 plan | Two compilers forever. |
| Tiny custom VM | OCaml | Wasm is already a portable VM LLVM can target. |
| LLVM in the seed | their C++ compiler | Inflates the seed; needs a C++ toolchain to even start. |
| Native backend in the seed | — | Not portable enough to be *the* bootstrap. |

C is the bootstrap IR because a system `cc` is the one tool every Unix
already has, and C is the one IR that `cc` consumes. Native backends
(x86, aarch64, wasm, LLVM) live in `src/codegen/` of the *Zig-written*
compiler. `zig2` without LLVM cannot do release opts, some linking, asm,
or compile C; it can still emit C, and that is enough to rebuild itself.

`zig-bootstrap` is a different project: build LLVM from source, then Zig,
for a new target. That is "ship a toolchain", not "bootstrap the language".

### What the C backend actually is

`src/codegen/c.zig` does not lower to machine code. For this backend, MIR
*is* the C text of one function. `lib/zig.h` is the runtime: overflow
helpers (`zig_addo_i64`, …), wrapping/saturating ops, atomics, special
floats, identifier mangling (`zig_e_` for reserved names). Locals are
`t0`, `t1`; args are `a0`. AIR is legalised (`expand_add_safe`, packed
load/store) before emission. The host `cc` is the optimiser and assembler.

That is the whole product of a bootstrap compiler: typed AST → C → `cc` →
binary.

## Luce copies those stages

```text
Zig C++ compiler (2015–22, LLVM)     we skip; they regretted it
Zig #5246 plan (C++ emits C)         THIS REPO
zig1.wasm  (C backend only)          freeze of luce-base, later
zig2       (self-hosted via C+cc)    luce-base, first build
zig3       (native / LLVM)           luce-base with QBE or a machine backend
```

Concretely:

1. **This repo** is Zig's planned C++ stage: compile Luce-Base well enough
   to write `luce-base`. Product backend is C plus host `cc`. `lucb build`
   already produces a native executable. C is IR, the same way QBE IL is
   IR. There is no QBE, LLVM, Wasm, or MIR here.
2. **`luce-base`** (next tree, written in Base) is Zig's self-hosted
   compiler. Its first backend is C, like `zig1`. A machine backend is
   added *there*, like `src/codegen/x86_64.zig`.
3. **Freeze a seed** once `luce-base` compiles itself: a C dump if it is
   small and portable, otherwise wasm + a tiny `wasm2c` as Zig did. Then
   this C++ tree can go away. Update the seed only when Base needs a new
   feature to compile itself.

When that machine backend is written, QBE is the example, not a library
this C++ compiler vendors. QBE is a few thousand lines of C: one IL,
register allocation, a handful of ISAs. That is the size of a backend a
Base compiler can grow itself. We read it, then write our own. The
bootstrap path stays C plus host `cc`.

Do not put a second backend in this compiler to "really compile to
binary". Zig's C++ compiler did that with LLVM. The replacement was C.

## Pipeline

```text
.lucb → source → lex → parse (AST) → check (HIR) ┬→ interpret
                                                └→ emit C → host cc → exe
```

There is no MIR. MIR exists so several backends can share a machine form. A
bootstrap with one backend (C) does not need it. `luce-base` may introduce
MIR when it grows a second backend.

HIR is typed, desugared, and the last word on meaning. The interpreter
executes HIR. The C backend lowers HIR. Semantics live in the checker plus a
small C runtime for traps and checked arithmetic. The interpreter must not
grow a second type system.

The C we emit is the bootstrap IR: prefix `lb_`, structs as C structs,
methods as `lb_Type_method(Type* self, ...)`, checked ops and traps in
`src/runtime` (`lucb_rt.h` is our `zig.h`). Host `cc` finishes the binary.

## Two executions

Every executable language feature is proven by two independent executions
that must agree:

1. the HIR interpreter (the oracle)
2. a compiled C artifact (the product)

When they disagree, the stage between them is wrong. Frontend tests pin
diagnostic **codes**, not wording. Language tests pin trap reason, stdout
bytes, and exit status.

## Layers

The language library (`lex` through `emit`) never opens files. The driver
hands it source buffers. Same seam as the Zig stage-0 compiler.

| Layer | Owns |
| --- | --- |
| `support/` | arena, diagnostics, the test harness |
| `source/` | UTF-8, BOM, positions, file identity |
| `lex/` | tokens and layout (`INDENT`/`DEDENT`) |
| `parse/` | untyped arena AST, full grammar of §21 |
| `check/` | names, types, effects; records HIR |
| `hir/` | typed nodes |
| `interp/` | HIR interpreter |
| `emit/` | HIR → C |
| `runtime/` | C startup shim, trap reporter, checked helpers |

Parse the whole grammar once. Typecheck, interpret, and emit grow by slices.

## Runtime

A Base executable links a startup shim, a trap reporter, and checked-arithmetic
helpers. It links the host C library unless `--freestanding`. There is no Luce
runtime, no reference counting, and no built-in collections (`base.md` §1.3).

## Packages

`luce.toml` as in spec §16.4. Until a program needs two files, `lucb check
file.lucb` is a one-file package named after the file.

## Binary

`lucb`, so it coexists with stage-0 `luce` and the Luce compiler.
