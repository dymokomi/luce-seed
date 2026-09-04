# Design of luce-base-c

A C++ bootstrap compiler for Luce Base. The language specification is
[`language/base.md`](language/base.md). This file is the architecture of the
compiler, not of the language.

## Why this compiler exists

The previous stack started at the wrong end: a Zig compiler for full Luce,
then a Luce compiler that treated Base as a *profile* of the same pipeline.
One lexer, one parser, one IR, two languages. That is the wrong bootstrap.

The intended chain is:

```text
luce-base-c  (C++, this repo)     compiles Luce Base
     ↓
luce-base    (written in Base)    compiles itself
     ↓
luce-full    (written in Base)    compiles full Luce
```

This compiler never parses, types, or emits full Luce. `.luc` and `.lucn` are
rejected. There is no profile flag.

## Pipeline

```text
.lucb → source → lex → parse (AST) → check (HIR) ┬→ interpret
                                                └→ emit C → host cc → exe
```

There is no MIR. MIR exists so several backends can share a machine form. A
bootstrap with one backend (C) does not need it. The self-hosted Base
compiler may introduce MIR later.

HIR is typed, desugared, and the last word on meaning. The interpreter
executes HIR. The C backend lowers HIR. Semantics live in the checker plus a
small C runtime for traps and checked arithmetic. The interpreter must not
grow a second type system.

## Product backend is C

Base is C with a different organisation and a C ABI. Emitting C needs no
vendored QBE or LLVM, is the natural debug form, and matches export headers
(`base.md` §17.6). QBE is the right backend for a later self-hosted
multi-target compiler. Not this one.

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
