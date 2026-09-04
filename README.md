# luce-base-c

A C++ bootstrap compiler for **Luce Base**. It compiles `.lucb` source and
nothing else: not full Luce, not `.luc`, not `.lucn`.

```text
luce-base-c  (C++, this repo)     compiles Luce Base
     ↓
luce-base    (written in Base)    compiles itself
     ↓
luce-full    (written in Base)    compiles full Luce
```

The language is specified in [`docs/language/base.md`](docs/language/base.md).
That document is the law. Architecture of *this* compiler is
[`docs/DESIGN.md`](docs/DESIGN.md). What to implement next is
[`docs/PLAN.md`](docs/PLAN.md). What exists is [`docs/DONE.md`](docs/DONE.md).

## Build

C++20, CMake 3.20, the host C/C++ compiler.

```text
./test.sh
./build/lucb --version
./build/lucb check path/to/file.lucb
```

`./test.sh` is the gate: it configures with sanitizers, builds, and runs
every test.

## Commands

```text
lucb --version
lucb --help
lucb check <file.lucb>     # lex (and, later, parse and typecheck)
lucb lex   <file.lucb>     # print tokens, for debugging the lexer
```

`build`, `run`, `eval`, and `test` arrive with later slices.

## Status

M0 (skeleton) and M1 (lexer) are the first slices. See `docs/PLAN.md`.
