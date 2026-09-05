# Examples

Complete programs shaped like the work `luce-base` will do, each a package
with its own `luce.toml`. Every one is proved on every test run: it must
check, its C must compile under `-Wall -Werror`, and the interpreter and the
binary must agree on stdout, stderr, and exit status. `main.expect` pins the
output; a `# args:` line on the first line supplies arguments.

| Example | What it exercises |
| --- | --- |
| `calc/` | A three-module expression compiler: a byte lexer with an integer-backed `enum` kind, a recursive-descent parser building a payload `enum` tree in the current allocator, an evaluator, and an emitter writing C through a `Writer`. |
| `lexer/` | A lexer for a Base subset written in Base: keyword tables as `str[N]` constants, layout with a fixed indent stack, `ErrorCode` constants shared across modules, `files.read` and `cstr` arguments, counts in an array indexed by an enum. |
| `symbols/` | A string interner over an `Arena` passed as an `Allocator` view, open addressing with `hash`, growth by reallocation and `memory.copy`, and a block-structured symbol table with duplicate detection through `catch`. |
| `vm/` | A stack machine: a payload `enum` instruction set, `match` with bindings, fixed-size value and frame stacks inside a struct, recursion through explicit frames, and faults reported as errors rather than traps. |
| `json/` | A JSON parser and printer: a recursive `enum` tree of arena-allocated nodes linked through `T*?` fields, `while let` walks, error messages formatted into a caller's scratch buffer with the byte offset, and `with` scopes. |

Run one by hand:

```sh
build/lucb build examples/json/main.lucb --release -o /tmp/json && /tmp/json
build/lucb eval examples/lexer/main.lucb examples/lexer/sample.lucb
```

When an example needs something the seed lacks, the seed grows just enough
to compile it, and the shape is pinned as a small program under
`testdata/programs/`. That is the loop that keeps the seed honest before
`luce-base` starts.
