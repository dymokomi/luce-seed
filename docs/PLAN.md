# Plan

Nothing is planned for this tree beyond parity with the specification. The seed exists to
build `luce-base`, whose gate builds this tree's main beside it: a change to
[`base.md`](https://github.com/dymokomi/luce-base/blob/main/docs/language/base.md) that `luce-base` needs lands here first, with its
test and a `VERSION` bump; then `luce-base` uses it. What each
section of the specification has here is [`FEATURES.md`](FEATURES.md).

Out of scope for the seed, and staying out: the standard library's `Arena` and
`PageAllocator`, a thread's `stack` and `name`, `asm` in the interpreter, Unicode
`for character in text`, extra `luce.toml` roots, the status form of a fallible export in
the C, a native backend, a formatter, and a linter. `luce-base` has them.
