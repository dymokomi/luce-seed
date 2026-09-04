# What exists

Only committed, gate-green behavior. The plan lives in [`PLAN.md`](PLAN.md).

## M0 — Skeleton

CMake, the `lucb` driver (`--help`, `--version`, `check`, `lex`), arena,
diagnostics with stable codes, source loading and encoding checks, `./test.sh`
with ASAN+UBSAN.

## M1 — Lexer

The lexer of `base.md` §3, §4, and the tokens of §21. Evidence: `tests/lex_test.cpp`
and `tests/source_test.cpp`, run by `./test.sh`.
