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
The freeze gate is `examples/` and `testdata/`: every program there is
proved by both executions on every test run (see below).

## Build

C++20, CMake 3.20, the host C/C++ compiler.

```text
./build.sh                     # the release compiler: build/lucb
./build/lucb --version
./build/lucb check path/to/file.lucb
./test.sh                      # the gate: a sanitized build in build-test/, every test
```

`./build.sh` makes the binary to use for real work; it is about thirty times
faster than the sanitized test build, which `./test.sh` keeps in its own
directory. A 76,000-line Base file checks and emits in under half a second.

## Commands

```text
lucb --version
lucb --help
lucb check <file.lucb>     # lex, parse, typecheck
lucb lex   <file.lucb>     # print tokens
lucb dump  <file.lucb>     # print the parse tree
lucb eval  <file.lucb> [args]  # run `answer()` or `main` in the interpreter
lucb build <file.lucb> -o <exe>
lucb build <file.lucb> --release -o <exe>
lucb build <file.lucb> --emit=c -o <file.c>
```

`run` and `test` arrive with later slices.

## Layout

```text
src/        the compiler: support, source, lex, parse, check, interp, emit, runtime, pkg
tests/      unit tests per stage, plus programs_test.cpp which proves every program below
testdata/
  programs/ one small program per language feature, grouped by topic
  spec/     the fifteen programs of base.md §24, kept identical to the document
  lex/ check/  fixtures for the lexer and checker tests
examples/   complete programs shaped like compiler work: a calculator compiler,
            a lexer, a symbol table, a bytecode VM, a JSON parser
docs/       the language, this compiler's design, plan, ledger, and conventions
tools/      scripts used to maintain the tree
cmake/      the runtime-embedding step
```

Every `.lucb` under `testdata/programs`, `testdata/spec`, and `examples` is a
test: it must check, its C must compile under `-Wall -Werror`, and the
interpreter and the binary must agree. A file says what it proves with
`# answer: N`, `# args: ...`, or a sibling `.expect`; see
`tests/programs_test.cpp`.

## Status

**luce-seed-0.6.** The oracle audited against the specification (`docs/DONE.md`); M0–M14 landed and three earlier audits closed: every one of
the fifteen `base.md` §24 programs checks, emits C that compiles under
`-Wall -Werror`, and runs; the interpreter and the binary agree on the
audit's sixty-two programs, including a three-module mini compiler
(`examples/calc/`). This repo is the seed: enough Base to write
`luce-base`. See `docs/PLAN.md` and `docs/FEATURES.md` for what stays out
of seed, and `docs/DESIGN.md` for what the oracle does not model.

## License

luce-seed is dual-licensed under the Apache License 2.0 and the MIT license,
at your option. See [LICENSE](LICENSE), [LICENSE-APACHE](LICENSE-APACHE),
and [LICENSE-MIT](LICENSE-MIT).
