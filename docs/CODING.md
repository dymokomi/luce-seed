# Coding conventions

Write code a tired reader can understand a week later. Prefer plain, old-school
C++ over clever modern ceremony.

## Language

A small subset:

- C++20 as a better C: structs, enums, functions, `vector`/`string`.
- `-Wall -Wextra -Wpedantic -Werror`, `-fno-exceptions -fno-rtti`.
- Tagged unions (`enum` + a struct of pointers), not virtual class trees.
- Explicit types. `auto` only for iterators nobody wants to spell.
- No lambdas unless a one-line predicate is clearly better than a name.
- No exceptions for user errors. Diagnostics are values. `abort` on ICE.
- `clang-format` before a commit.

Standard-library names live in `namespace lucb` via `support/common.h`, so
headers write `string`, not `std::string`. `.cpp` files may add
`using namespace std;` after the includes. Never `using namespace std` in a
header.

## Files

`namespace lucb`. `snake_case.{h,cpp}`. A file has one job. Split at a real
boundary, not at a line count. The header says what the file is for.

The language library never opens files. The driver hands it source buffers.

Arena-backed objects are not destroyed. Do not put `string` inside them;
borrow `string_view` into the source, or allocate bytes on the arena.

## Naming

- Types: `PascalCase`. Functions, fields, files: `snake_case`.
- Enumerators: `PascalCase` (`TokenKind::KwFunc`, `NodeKind::Func`).
- Diagnostic codes: `lucb.<stage>.<name>` (`lucb.parse.chain`). Tests pin
  codes, never wording.
- Cite the spec where a rule is implemented: `// base.md §3.2`.

## Commits

Author and committer are always `Dy Mokomi <dy@dymokomi.com>`. One short
lowercase subject. No trailers.

## Tests

- Unit tests live in `tests/` and use `src/support/test.h`.
- Language programs live in `testdata/`.
- `./test.sh` is the gate. A slice is not done until it is green.
- A FEATURES.md row names its tests.
