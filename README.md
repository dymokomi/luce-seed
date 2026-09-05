# luce-seed

A C++ seed compiler for **Luce Base**. This tree compiles `.lucb` well enough
to write `luce-base` in Base. It is not a compiler that grows into
`luce-base`. It compiles `.lucb` and nothing else: not full Luce, not `.luc`,
not `.lucn`.

```text
luce-seed  (C++, this repo)      compiles Luce Base to C, then a binary
     ↓
luce-base  (written in Base)     compiles itself; C first, then native
     ↓
luce-full  (written in Base)     compiles full Luce
```

The language is specified in [`docs/language/base.md`](docs/language/base.md).
That document is the law. Architecture of *this* compiler is
[`docs/DESIGN.md`](docs/DESIGN.md). What to implement next is
[`docs/PLAN.md`](docs/PLAN.md). What exists is [`docs/DONE.md`](docs/DONE.md).
The freeze gate is `testdata/programs/compile/`: a tiny Base package that
emits C, shells out to `cc`, and runs the result.

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
lucb check <file.lucb>     # lex, parse, typecheck
lucb lex   <file.lucb>     # print tokens
lucb dump  <file.lucb>     # print the parse tree
lucb eval  <file.lucb>     # run pub func answer() -> i64
lucb build <file.lucb> -o <exe>
lucb build <file.lucb> --release -o <exe>
lucb build <file.lucb> --emit=c -o <file.c>
```

`run` and `test` arrive with later slices.

## Status

**luce-seed-0.1.** M0–M14 landed. This repo is the seed: enough Base to
write `luce-base`. See `docs/PLAN.md` and `docs/FEATURES.md` for what
stays out of seed.
