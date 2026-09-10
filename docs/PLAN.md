# Plan

Nothing is planned for this tree beyond parity with the specification. The seed exists to
build `luce-base`, which is pinned to a tag of it (`bootstrap/SEED` there): a change to
[`language/base.md`](language/base.md) that `luce-base` needs lands here first, with its
test, a `VERSION` bump, and a tag `luce-seed-N`; then `luce-base` pins the tag. What each
section of the specification has here is [`FEATURES.md`](FEATURES.md).

Out of scope for the seed, and staying out: the standard library's `Arena` and
`PageAllocator`, a thread's `stack` and `name`, `asm` in the interpreter, Unicode
`for character in text`, extra `luce.toml` roots, the status form of a fallible export in
the C, a native backend, a formatter, and a linter. `luce-base` has them.
