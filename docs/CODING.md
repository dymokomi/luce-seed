# Coding conventions

Write code a tired reader can understand a week later. Prefer plain code over
clever modern ceremony. The north star for module shape is the Zig stage-0
coding guide; this file is the C++ form.

## Language

- C++20, `-Wall -Wextra -Wpedantic -Werror`, `-fno-exceptions -fno-rtti`.
- `clang-format` (see `.clang-format`) before a commit.
- `namespace lucb`. Files are `snake_case.{h,cpp}`.
- Tagged unions and `enum class`, not virtual class trees.
- Diagnostics are values collected in a `DiagnosticBag`. No C++ exceptions
  for user errors. `abort` only on an internal compiler error.
- Allocation that outlives a function is explicit: an `Arena&`, or a
  container the caller owns. Arena-backed objects do not run destructors;
  do not put `std::string` inside them.

## Modules

A file has one job. Split at a real API or privacy boundary, not at a line
count. The header says what the file is for. Public types say who owns what.

The language library never opens files. The driver hands it source buffers.

## Naming

- Types: `PascalCase`. Functions, methods, fields, files: `snake_case`.
- Enumerators: `PascalCase` (`TokenKind::KwFunc`, `TokenKind::PlusPercent`).
- Diagnostic codes: `lucb.<stage>.<name>` (`lucb.lex.tab`). Tests pin codes,
  never wording.
- Cite the spec where a rule is implemented: `// base.md §3.2`.

## Tests

- Unit tests live in `tests/` and use `src/support/test.h`.
- Language programs live in `testdata/`.
- `./test.sh` is the gate. A slice is not done until it is green.
- A FEATURES.md row names its tests. Adding a capability without a ledger
  row is incomplete.
