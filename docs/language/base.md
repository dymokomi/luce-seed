# Luce Base

## The language design

Status: design draft, revision 4, 2026-09-02. Not yet implemented. This document is complete on its own: it states every rule of the language and the reason for it, and it does not require reading the Luce 1.0 specification. Where a rule is shared with full Luce, this document says so once and states the rule anyway. The standard library is specified separately; this document defines what the compiler knows and names each library facility where the language depends on it (§16.6).

## 1. Introduction

Luce Base is C with a different organisation. It keeps the properties that make C the language other languages are implemented in: values with predictable layout, pointers, manual memory, a plain calling convention, and no runtime library (§1.3). It replaces the parts of C that are accidents of its history: header files, the preprocessor, null, integer promotion, `switch` fallthrough, return-code error handling, and `void*` generics.

A Base program is made of structs with functions and initialisers inside them, modules instead of headers, generics instead of macros, tagged unions with exhaustive matching, interfaces, `defer`, optionals instead of null, and a fallible result type instead of return codes. Memory is managed by hand: `new`, `alloc`, and `free` are part of the language and go to an allocator the program chose. There is no reference counting, no garbage collector, and no hidden allocation.

Base is a profile of the Luce language, compiled by the same compiler as full Luce. A Base module is a file ending in `.lucb`. Full Luce, with its reference-counted classes and collections, can import a Base module as an ordinary module; Chapter 18 states that contract. A Base program that never touches full Luce needs nothing from it.

The three sentences that summarise the design:

> **Base gives up nothing that portable C can do.** Where a C capability is unsafe, Base admits it with a restriction that makes it checkable; it never omits it.

> **Values copy. Pointers point and are never null unless they say so. Spans carry their length. Allocation is written. Arithmetic, bounds, and shifts are checked. Failure is visible. Nothing runs that you did not write.**

> **A better C with Luce's syntax, that calls C in both directions without adapters, and is the language Luce's own runtime is written in.**

The rule that decided most spellings: Base is allowed to be safer than C, but never slower and never more convoluted. Where a check costs one predicted branch, Base has it and marks the unchecked form. Where a check would cost more than that, or would change what the hardware does, C's semantics stand.

### 1.1 How to read this document

Each chapter states its rules first, then the reasons under the heading **Why.** The rules are normative. The reasons are there so that the next person to change a rule knows what the rule was for. Chapter 21 is the grammar; Chapter 22 is a one-page translation table from C; Chapter 23 lists the places where Base and full Luce differ and why; Chapter 24 is a set of complete programs.

Code in this document is Base source unless marked otherwise. A fenced block marked `c` is C, shown for comparison.

Builtin names are single words. The language's own operations are keywords (`new`, `alloc`, `free`, `with`), operators (`+%`, `+?`), C's words (`sizeof`, `offsetof`), or one-word core functions (`format`, `assert`). Compile-time source facts live on the `luce` module (`luce.location`, `luce.file`, `luce.line`, `luce.function`) and are replaced at the use site, not called. A compound name such as `format_into` never appears in the builtin surface; user code may name things as it likes.

### 1.2 What it looks like

```luce
struct Cursor:
    var data: const u8[]
    var offset: usize

    mutating func advance(count: usize) -> !:
        if self.offset + count > self.data.length:
            error(past_end, "advance past the end of the input")
        self.offset += count

    func remaining() -> const u8[]:
        return self.data[self.offset..]

func first_line(text: str) -> str:
    var cursor = Cursor(data = text.bytes, offset = 0)
    while let byte = cursor.remaining().first():
        if byte == '\n': break
        cursor.advance(1) catch failure:
            recover ()
    return (str)text.bytes[0..<cursor.offset]
```

Everything in that example has a C counterpart and costs what the C would cost on the native backends. The span `const u8[]` is a pointer and a length. `offset + count` traps on overflow instead of wrapping. `error(...)` returns a two-word error value; `catch` handles it. The final cast reinterprets bytes as text without a check, because the bytes came from a `str`. Nothing allocates.

### 1.3 The runtime, and why Base has none

Every language links some code into every program it compiles, code the programmer did not write but the language's features depend on. C's is small: the startup code that calls `main`, and `malloc`, `printf`, and the rest of the C library. Full Luce's is larger, because full Luce has features that need code to exist at execution time. This document calls that code **the Luce runtime**. It is a library, written in Luce, that provides:

- **Reference counting.** A `class` value in full Luce is a heap object with a count of the references to it. Every copy of the reference increments the count, every release decrements it, and the object is freed when the count reaches zero. The code that allocates the object, adjusts the count, tracks weak references, and runs `deinit` is the runtime.
- **Collection storage.** `list`, `map`, `set`, and the owned `str` and `bytes` are growable heap structures. The code that grows them, copies them on write, hashes keys, and keeps their iteration guards is the runtime.
- **Closures.** A closure that captures a local needs an environment that outlives the frame; the runtime allocates and reference-counts it.
- **Workers.** `spawn` copies a value graph into an isolated worker; the runtime does the copying, scheduling, and joining.
- **Interface values.** A full Luce interface value boxes its payload; the runtime allocates the box and detaches it before mutation.
- **The heap.** Underneath all of the above, an allocator over a memory arena the backend provides.
- **Trap reporting.** The code that prints a trap's message and source trace and stops the program.

Full Luce needs all of this because its central promise, values copy and references share identity with automatic lifetime, is implemented by it. A full Luce program cannot run without the runtime any more than a C program can run without its C library.

Base has none of those features. There are no classes, so nothing is reference-counted. There are no built-in collections; a Base list is a library type that allocates with `new` from the allocator it was given (§12.3). There are no closures, no workers, and no boxed interface values. There is no hidden heap; every allocation is written as `new` or `alloc`. What remains from the list above is trap reporting, and that is a few hundred bytes.

So a Base executable links what a C executable links: a **startup shim** that builds `main`'s arguments, sets the initial allocator, and calls `main`; a **trap reporter**; and, unless the program is built `--freestanding`, the host C library for process start and output. There is no runtime library. This is not a restriction placed on Base; it is a consequence of the features Base does not have, and the compiler checks it: a Base artifact that reaches any function needing the Luce runtime fails to build, naming the function (§16.2).

The relationship runs the other way too. The Luce runtime is itself written in Luce, today in the audited native tier (§16.2), and the plan is for it to become a Base package (§18.13): the code that implements reference counting and collection storage for full Luce is code of the kind Base is designed for.

Two uses of the word are kept apart in this document. **The Luce runtime** is the library above. **At runtime** means at execution time, as in "checked at runtime", and says nothing about the library.

### 1.4 Glossary

Terms this document uses with a fixed meaning, each defined again where it first matters.

- **Tier**, **kind**: one of the three module kinds, safe Luce, native Luce, or Base, told apart by file suffix (§16.2).
- **Profile**: Base as a mode of the Luce compiler, selected by the `.lucb` suffix. Not to be confused with a **build profile**, the choice between the default and diagnostic builds (§19.4).
- **Span**: a pointer and a length, `T[]` (§5.4).
- **View**: a value that refers to storage it does not own: a span, a `str`, an interface view.
- **Niche**: a bit pattern that is never a valid value of a type, reused to represent `none` so that the optional costs no extra word. Address zero is the niche of every pointer type (§5.3).
- **Zeroable**: a type whose all-zero bit pattern is a valid value (§6.1).
- **Plain**: a type whose representation is copied data with no pointer and no reference identity (§18.3).
- **Lent**: passed for the duration of one call, to be read and not retained (§18.4).
- **Witness table**: the static table of function pointers through which an interface view dispatches (§14.3).
- **Current allocator**: the thread-local allocator that `new`, `alloc`, and `free` use unless told otherwise (§12.3).
- **Slot**: a parameter or result position in a signature that crosses the C boundary (§17.1).
- **Sealed**: usable only by the runtime package, under its own identity (§18.13).
- **Recipe**: the file `luce bind` reads for the facts a C header does not state (§17.5).

### 1.5 Failure in four sentences

Failure appears from the first example on and is specified in Chapter 11. The short version: a function whose result type ends in `!` returns either its value or an `Error`. `try e` unwraps the value of a fallible `e` or returns its error from the current function, which must itself be fallible. `error(code, message)` returns an error. `e catch name:` handles the error of `e` in place, and the handler must `recover` a value, return, or terminate.

## 2. Design principles

These are the tests every rule in the rest of the document had to pass.

**A feature enters the language only when it removes more total complexity than it adds.** Total complexity counts syntax, semantics, the compiler, the runtime, diagnostics, tooling, documentation, and the number of distinctions a programmer must remember. This is Luce's governing test, and Base inherits it unchanged.

**Base gives up nothing that portable C can do.** A C programmer must never find that the thing they need is absent. Where a capability is unsafe, it is admitted with a restriction that makes it checkable, or with an explicit spelling that can be searched for. Chapter 20 lists the exclusions and the reason each one is not a loss.

**Safer than C, never slower, never more convoluted.** A check that costs one predicted branch is on by default and the unchecked form is marked. A rule that would cost more than that, or would change what the hardware does, follows C. Signed division truncates as the instruction does; an atomic add is the instruction; a bounds check is one compare.

**C's spelling wins where C's spelling is good.** `T*`, `&x`, `*p`, `(T*)p`, `T[N]`, `sizeof`, `offsetof`, `const`, `volatile`, and `union` mean in Base what they mean in C. Luce's spelling wins where C's is bad or absent: `name: type`, `let`/`var`, `T?`, `T!`, `match`, `defer`, indentation.

**Builtins are one word, an operator, or syntax.** No compound names in the language's own surface.

**Undefined behaviour is named, not assumed.** Chapter 12 has two exhaustive lists: what is defined and checked in every build, and what remains undefined. A rule that would add to the second list needs a reason a C programmer would accept.

**Costs are explicit.** No operation allocates, copies a large value, or calls through a table unless the source says so. Where the compiler must insert something at a boundary, it is one comparison, and this document says where.

**One compiler, two spellings.** Base and full Luce share one lexer, one parser, one typed intermediate representation, and the same backends, the way C and C++ share Clang. They do not promise the same spelling for every type. What they promise is that a value with one representation is one type, and that a value with two representations crosses through a named adapter.

**The safe path is the ordinary one.** In a mixed program, a plain `.luc` module is safe Luce, and the audited and Base tiers carry the marked suffixes. Inside Base, the checked form is the ordinary spelling and the unchecked form is the marked one: `+` traps, `+%` wraps; `u8(x)` traps, `(u8)x` truncates; `var buffer: u8[N]` is zeroed, `= ---` is not.

## 3. Source text

### 3.1 Encoding

Source is UTF-8. A byte-order mark is accepted and ignored only at byte zero. NUL bytes, invalid UTF-8, the Unicode bidirectional format characters (U+202A to U+202E and U+2066 to U+2069), and characters confusable with ASCII punctuation are rejected. CRLF is normalised for parsing while source positions keep correct byte and line mappings.

Identifiers are ASCII: a letter or `_`, followed by letters, digits, or `_`. The standalone `_` is the pattern wildcard; `_unused` is an ordinary name. Unicode is fully supported inside text and comments.

### 3.2 Layout

A statement suite after `:` is either one simple statement on the same physical line or a newline followed by a block indented four spaces. Type, enum, union, and interface bodies always use the indented form.

```luce
if cached: return result

if image.width > maximum_width:
    let scale = maximum_width / f64(image.width)
    image.resize(scale)
```

- A same-line suite cannot contain a compound statement (`if`, `while`, `for`, `match`, `with`, `asm`), a nested suite, or a second statement.
- Tabs are errors with a machine-applicable replacement. There are no semicolons and no line continuations.
- Newlines terminate statements except inside `()`, `[]`, or `{}`, where they are spacing. Inside delimiters, a `:` that ends its line opens an indented suite, so a `match` expression or a `catch` handler can be written as an argument.
- A trailing comma is accepted in multi-line parameter, argument, tuple, array, and payload lists.
- Blank lines do not affect meaning. The formatter, `luce fmt`, is the single authority on line breaking and indentation.
- An `asm` suite (§8.9) is captured raw: its lines are not tokenised.

**Why.** Base keeps Luce's layout rather than adopting braces because a brace dialect would mean two parsers, two formatters, and a permanent question of which style a file is in. Programmers who know Python already read this layout. The one-line suite form covers the short `if x: return` cases that are most awkward to write in indented form.

### 3.3 Comments and documentation

```luce
# An ordinary comment.

## A public summary used by `luce doc`.
## Further paragraphs are Markdown.
pub func area(width: f64, height: f64) -> f64:
    return width * height
```

`#` begins a comment outside a string. Consecutive `##` comments immediately before a declaration are its documentation. A `##` block at the top of a file, separated from what follows by a blank line, documents the module.

### 3.4 Naming

Types, interfaces, and unions are `PascalCase`; functions, methods, bindings, fields, cases, modules, and packages are `snake_case`; acronyms are words, `HttpClient`. Violations are formatter and linter diagnostics, never a change of meaning.

### 3.5 Scope

Names resolve lexically. A module's declarations share one namespace and are order-independent. Members of a type have their own namespace. Locals are sequential; use before declaration is rejected. A local may not shadow another visible local, parameter, or imported name; renaming is the repair. A loop, `catch`, `if let`, or `match` binding owns its nested scope. A loop label (§8.5) lives in its own namespace.

The compiler-known core namespace cannot be redeclared: no declaration of any kind, a binding, a parameter, a function, a type, a field, an enum case, a label, or an alias, may take a core name, and a compiler carries exactly this dictionary:

```text
assert discard error trap hash print format sizeof alignof offsetof hex bin pad
bool i8 i16 i32 i64 isize u8 u16 u32 u64 usize f16 f32 f64 char str cstr
unit never void fmt Error ErrorCode
```

The reserved words of §3.6 are excluded the same way, by the lexer. A standard module's name, `io` or `c`, binds only where it is imported, so a local named `c` in a module that does not import `c` is ordinary. The standard modules themselves may declare core names, since they are what those names mean. Calls such as `sizeof` parse as ordinary calls; only their checked types and semantics are special. `luce.location` and its pieces are not calls: they are compile-time replacements.

**Why.** No shadowing removes a refactoring hazard and keeps every diagnostic that names a binding unambiguous. Making `sizeof` a core name rather than a keyword keeps the grammar small: it is a call whose argument may be a type.

### 3.6 Reserved words

```text
alloc and as asm break catch const continue defer elif else enum errdefer
export extern false for free from func goto if import in interface
let match mutating new none not or pub recover return self static struct test
thread_local true try type union var volatile while with
```

Contextual words, meaningful only in the positions stated: `void` before `*`; `packed`, `align`, `naked`, `weak`, `used`, `noinline`, `cold`, `section`, and `inline` before a declaration (§9.8); `noalias` before a parameter type; `blocking` and `out` in `extern` declarations; `reg` and `options` in an `asm` operand list. `goto` is reserved and unused (§8.6). `class`, `weak` as a field marker, and `spawn` belong to full Luce and are rejected in Base with a diagnostic that names the tier they belong to. `none` is a literal (§4.1) and is never a case name.

## 4. Literals

### 4.1 Boolean and absence

`true`, `false`, and `none`. `none` takes its optional type from context; it is never a universal null.

### 4.2 Integers

```luce
42        1_000_000     0xff     0o755     0b1010_1100     255u8     -20i32
```

Underscores may separate digits. Based prefixes are lowercase. Context chooses the integer type; absent context the default is `i64`, except in a variadic C argument position, where it is `c.int` (§17.2), and as the bound of a `for` range, where it is `usize` (§8.3). A suffix names an exact type. A literal outside the contextual type's range is a compile error. A negative literal is unary minus applied to a positive literal; `-9223372036854775808` is accepted as `i64` although its positive part alone is out of range.

### 4.3 Floats

```luce
1.0     6.022e23     0.5f32     1_000.25
```

Context chooses the float type; absent context the default is `f64`, or `c.double` in a variadic position. Conversion from decimal is correctly rounded. NaN and the infinities are constants in the `math` module, not literals.

### 4.4 Characters and text

```luce
'A'                          # one Unicode scalar: char; adapts to u8 when ASCII
"hello"                      # UTF-8 text: str, static, NUL-terminated
b"\x89PNG\r\n"               # bytes: const u8[6] static data
r"C:\studio\shots"           # raw: no escapes, no interpolation
f"frame {frame}: {status}"   # formatted; see §5.5
"""multiline
text"""
```

- A character literal is one Unicode scalar after escapes. In a `u8` context an ASCII character literal is that byte, so `byte == '\n'` needs no conversion.
- A string literal is valid UTF-8, stored once in static data, and followed by a NUL byte that is not part of its length. Its type is `str`, and it converts implicitly to `cstr` (§5.5) because the NUL is guaranteed.
- A byte literal `b"..."` is static data of type `const u8[N]`, with `\xNN` escapes and ASCII text; it is not NUL-terminated.
- Triple-quoted strings drop a newline that directly follows the opening delimiter, strip indentation by the closing delimiter's column, and normalise CRLF to `\n` before escapes are decoded.
- Escapes in text are `\\`, `\"`, `\'`, `\n`, `\r`, `\t`, `\0`, and `\u{HEX}` with one to six hex digits. There is no `\x` in text; it exists in byte literals.
- A formatted string is not a value. It is consumed by `print`, by a `Writer`, by `format`, or by a parameter of type `fmt` (§5.5). `{{` and `}}` are literal braces. Each field is `{expression}`, evaluated once, left to right; there is no format specification inside the braces, and radix and padding are one-word functions applied in the field, `{hex(value)}`.

**Why.** The one extra byte per literal means a literal can be passed to any C function that takes a `char*` without a copy, and most of them do.

### 4.5 Array literals

```luce
let magic: u8[4] = [0x4c, 0x55, 0x43, 0x45]
let primes = [2, 3, 5, 7]                       # i64[4]
```

`[...]` is a fixed array whose length is its element count and whose element type comes from context or from the elements, which must agree. There are no list, map, or set literals in Base, because there are no built-in collections (§12.1).

## 5. Types

Base is statically and nominally typed. Every expression has one type. Inference is local: from an initialiser to its binding, from arguments to generic parameters, from a declared result into `return`, from context into literals and `none`. It never crosses a public signature, which always spells its types. There is no subtyping. The implicit conversions are exactly these:

- an integer to a wider integer of the same signedness (§7.5);
- a pointer to a more-qualified pointer of the same pointee (`T*` to `const T*`, `volatile T*`, or both);
- any object pointer to `void*`, and any read-only object pointer to `const void*`;
- a mutable `T[N]` to `T[]`, any `T[N]` to `const T[]`, and `T[]` to `const T[]`;
- a string literal, or a `str` produced by `format`, to `cstr`;
- an ASCII character literal to `u8`;
- `T` to `T?`, and `T` to a successful `T!`;
- a non-fallible function to the corresponding fallible function type;
- a pointer to a conforming type to an interface view (§14.3).

Everything else is written.

### 5.1 Scalars

| Type | Meaning |
| --- | --- |
| `bool` | `true` or `false`; one byte holding `0` or `1`; no numeric conversion |
| `u8`, `u16`, `u32`, `u64` | unsigned integers |
| `i8`, `i16`, `i32`, `i64` | two's-complement signed integers |
| `usize`, `isize` | unsigned and signed integers the width of a pointer on the target |
| `f16`, `f32`, `f64` | IEEE 754 binary floats |
| `char` | one Unicode scalar value |
| `unit` | the single value `()` of a function that returns nothing |
| `never` | the type of an expression that cannot complete: `error(...)`, `trap(...)`, a function that never returns |

`usize` and `isize` are the types of `sizeof`, `alignof`, `offsetof`, span lengths, array indices, and pointer differences. Their width is that of the target: 64 bits on the native targets, 32 on WebAssembly (§19.5). The compiler always compiles for one target, so a `usize` expression built from literals, `sizeof`, and arithmetic is a constant: it may be a top-level `let`, an array length, and the condition of a module-level `assert` (§11.6). The shared intermediate representation carries such a constant symbolically and the backend folds it, so the representation stays target-neutral while the source does not have to.

`never` coerces to any type because the value never exists. Operands before a `never` operand are still evaluated, left to right.

### 5.2 The `c` module

C's types live in the standard `c` module, so that a C signature can be written exactly and `luce bind` (§17.5) never has to guess a width. A `c` type whose width and signedness are the same on every supported target is an **alias** of the Base type with that width, and either spelling may be used. A `c` type whose width or signedness varies between targets is a **distinct** nominal type with explicit conversion, so that code cannot depend on one target's answer by accident.

| `c` type | C type | Base type | Width and signedness |
| --- | --- | --- | --- |
| `c.int`, `c.uint` | `int`, `unsigned int` | alias of `i32`, `u32` | 32 bits everywhere |
| `c.short`, `c.ushort` | `short`, `unsigned short` | alias of `i16`, `u16` | 16 bits |
| `c.schar`, `c.uchar` | `signed char`, `unsigned char` | alias of `i8`, `u8` | 8 bits |
| `c.longlong`, `c.ulonglong` | `long long`, `unsigned long long` | alias of `i64`, `u64` | 64 bits |
| `c.float`, `c.double` | `float`, `double` | alias of `f32`, `f64` | 32 and 64 bits |
| `c.bool` | `_Bool` | alias of `bool` | 1 byte, 0 or 1 |
| `c.size`, `c.uintptr` | `size_t`, `uintptr_t` | alias of `usize` | pointer width |
| `c.ssize`, `c.ptrdiff`, `c.intptr` | `ssize_t`, `ptrdiff_t`, `intptr_t` | alias of `isize` | pointer width |
| `c.char` | `char` | distinct | 8 bits; unsigned on AArch64 Linux, signed on the other supported targets |
| `c.long`, `c.ulong` | `long`, `unsigned long` | distinct | 64 bits on SysV and AAPCS64 targets, 32 on Windows x64 |
| `c.wchar` | `wchar_t` | distinct | 32 bits except on Windows, where it is 16 |
| `c.va_list` | `va_list` | opaque | may only be passed through to C |

A binding therefore reads `w: i32, h: i32, flags: u32`, and uses `c.long` or `c.char` only where C's width varies. `long double` and `_Complex` have no Base type; `luce bind` refuses them and names the shim recipe. `c.long(x)` from `i64` is a checked conversion; `cstr` is `const c.char*`.

**Why aliases.** A distinct `c.int` would make every binding say `c.` on every parameter for no information: `int` is 32 bits on every target Base supports. The distinct types are kept for the cases where the answer differs by target, because those are the cases where a program written against one target would be wrong on another.

### 5.3 Pointers

```luce
T*                 # pointer to mutable T; never null
const T*           # pointer to read-only T; never null
volatile T*        # pointer whose loads and stores are observable effects
T*?                # nullable pointer; `none` is C's null; one word
void*              # untyped pointer; C's void *
const void*        # untyped read-only pointer
const (T*)*        # C's T *const *: a pointer to a read-only pointer to T
```

- A pointer type is a complete type followed by `*`. `const` and `volatile` prefix the pointee and may combine. A qualifier applies to the innermost type; to qualify a pointer itself, parenthesise it. `?` may follow any `*`, so `Link*?*` is a pointer to a nullable pointer; the one-layer rule of §5.8 forbids only `??`.
- `void` may appear only immediately before `*`, optionally after `const`.
- A bare pointer is never null. Every operation that could produce a null pointer produces `T*?` instead: an integer-to-pointer cast (§7.5), an `extern` slot declared `?` (§17.1), a nullable field read. `T*?` is an ordinary optional whose representation is the null niche: `none` is address zero and the type is one word. It is unwrapped like any optional (§11.1). There is no `NULL` literal.
- `T*` converts implicitly to `const T*`, `volatile T*`, and `const volatile T*`. A qualifier is never removed implicitly. Any object pointer converts implicitly to `void*`, and any `const` object pointer to `const void*`. The reverse directions are casts.
- `==` and `!=` compare addresses between any two object pointers, across qualifiers and with `void*`. `<`, `<=`, `>`, `>=` between two object pointers are the total order of addresses, and Base guarantees that order across objects, which C does not. Pointers are `Hashable` by address, with the process seed of §7.4.

**Why never null.** In C, every pointer is nullable and almost none of them should be; checking for null before every dereference is the source of many of C's crashes and of much of its redundant code. A bare `T*` that cannot be null lets a function's signature say which pointers may be absent, and lets the compiler put the single check where the `?` is. Hare, Zig, and Cyclone reached the same design.

**Why a niche.** A nullable pointer must be one word so that a struct holding one has the layout its C definition has. Full Luce represents optional foreign handles as a token plus a flag, because a C library may legitimately use a zero handle and the two must remain distinguishable. Base's pointers are not handles: zero is null, so the niche is correct, and the compiler's intermediate representation gains a nullable-pointer type for Base and for native Luce's raw pointers (§19.2).

### 5.4 Arrays and spans

```luce
T[N]               # fixed-size value array, N a positive constant
T[]                # span: pointer plus length, elements mutable, non-owning
const T[]          # span with read-only elements
```

- `[N]` or `[]` after a complete type is an array or span. A bracket after a type *name* that contains types is a generic argument list (§13.1); a bracket that contains a constant expression, or nothing, after a complete type is always an array or span suffix. `Pair[i64, str][4]` is four pairs; `u8[4][4]` is an array of four arrays of four bytes, read inside-out as in C; `Node*[]` is a span of pointers; `u8[sizeof(Header)]` is a byte array the size of a header.
- `N` is a positive constant expression (§6.4). C forbids zero-length arrays and Base does not declare what C cannot.
- An array is a value: it copies element by element, may live inline in a struct or a local, and has C's layout.
- A span is two words, a pointer and a `usize` length. It does not own its elements and does not keep them alive. It is made from an array, from a pointer and a count, or by slicing:

```luce
var buffer: u8[4096]
let all: u8[] = buffer                  # an array converts to a span that points into it
let head = all[0..<16]                  # half-open slicing, checked
let tail = all[16..]
let view = u8[](pointer, count)         # from a pointer and a length; `pointer` must address `count` elements
```

- Indexing and slicing are bounds-checked and trap on violation, in every build. `span.length` is `usize`; `span.data` is the pointer; `span.first()` and `span.last()` are `T?`; `span.indexed()` yields `(usize, T)` pairs for `for`.
- An empty span's `data` is a non-null, correctly aligned, dangling pointer that must not be dereferenced. This keeps the empty span distinct from `none`, so `T[]?` uses the ordinary tagged optional representation, not a niche. A C caller that passes `(NULL, 0)` to an exported span parameter receives the empty span; the export wrapper normalises it (§17.6).
- A span of a local array is a pointer into the frame; §6.6 states the escape rule.

**Why spans are the common case.** A common C pattern is "a pointer to N of these, beside N", and the bugs in that pattern occur when the pointer and the count are passed or stored separately. A span carries both, checks the index, and costs the same two registers.

### 5.5 Text

`str` is an immutable UTF-8 view: a `const u8*` and a `usize` byte count, with the invariant that the bytes are valid UTF-8. Equality and ordering compare bytes. `text.length` is the byte count, O(1), as `strlen` is; `text.bytes` is a `const u8[]`; `for character in text` iterates Unicode scalars as `char`. `str` does not support integer indexing, because byte and scalar boundaries differ; slice `text.bytes` and convert, or iterate. Nothing about `str` allocates, and `+` on two strings does not exist; write into a buffer with `format` or a `Writer`.

`cstr` is `const c.char*` pointing at NUL-terminated text of unknown encoding. It exists for the C boundary.

Conversions between text and bytes use the two conversion spellings of §7.5:

- `str(bytes)` from a `const u8[]` validates UTF-8 and yields `str!`; `(str)bytes` yields a `str` with no check, for bytes already known to be valid.
- `str(value)` from a `cstr` scans to the NUL, validates, and yields `str!`.
- `(cstr)text` yields a `cstr` with no check. It is correct when the byte after the text's last byte is a NUL: a literal, the result of `format`, or text that came from C. Otherwise it is undefined (§12.6), as `(char*)` is in C.

A formatted string `f"..."` has no value of its own. It is consumed in one of four ways: `print(f"...")`; `writer.write(f"...")` on a `Writer` (§14.4); `format(buffer: u8[], f"...") -> str!`, which writes into the caller's buffer, appends a NUL when there is room, and returns a view of the text; or a parameter of type `fmt`, which a function declares to accept a formatted string or a `str` and may only pass on to one of these four (§9.1). Interpolation lowers to appends on the sink; no intermediate string exists. This is the `printf` replacement.

**Why a view.** Full Luce's `str` is owned and reference-counted, which is what makes concatenation safe there. Base has no reference counting, so a string is a view of storage that something else owns: static data, a buffer, an arena. Keeping the name `str` in both tiers means "text" reads the same in both; the difference is who owns the bytes, and Chapter 18 states how the two cross. The formatted-string rule follows: there is no owner for a fresh string, so the interpolation goes to whoever asked for it.

### 5.6 Function types

`func(A, B) -> R` is a C function pointer: one word, never null; `func(...)?` is nullable with the null niche. A named function, a method through its type (`Point.distance`), and a capture-free lambda convert to it. Base has no closures (§9.6), so a function type is exactly a C function pointer, and an `extern` declaration's function-pointer parameters are written with `func`. A function type has no zero value (§6.1). Conversion between a function pointer and `void*` is an explicit cast (§7.5).

### 5.7 Tuples

`(i64, str)` is a fixed-size anonymous value used for local grouping and multiple results (§9.3). Tuples have no field names, no methods, and no one-element form; `()` is the value of `unit`. A tuple has C's layout as a struct of its components.

### 5.8 Optionals

`T?` is either `.some(value)` or `.none`. It is one layer deep: `T??` cannot be written, and a generic instantiation that would produce it, such as `first[T]` with `T = Node*?`, represents the outer optional with a tag beside the value. Its representation is the null niche for pointer, function, and interface-view types, and a tag beside the value otherwise. Chapter 11 states how it is produced and consumed.

### 5.9 Atomic types

`@T` is an atomic variant of `T`, permitted when `T` is an integer, `bool`, `usize`/`isize`, a pointer, or a nullable pointer, of at most pointer width; and, on targets with a double-width compare-and-swap, a struct of two pointer-width words, for which only `cas`, `load`, and `store` are defined. Chapter 15 specifies the operations. `@` is part of the type: `&x` on an `@u32` yields `@u32*`, and no cast removes `@`.

### 5.10 Type aliases

```luce
pub type Pixel = f32[4]
type Callback = func(void*, i32) -> unit
type Link = FreeBlock*?
```

An alias is another spelling for the same type. It creates no distinction, has no methods, and is not generic. For a domain distinction, declare a one-field struct.

Aliases are how pointer shapes are kept readable. `T*`, `const T*`, `T*?`, and `T[]` read well on their own; a type that stacks more than two of `*`, `?`, `[]`, and `[N]` does not, and should be named: `Link[64]` rather than `FreeBlock*?[64]`. The linter reports a spelling with more than two postfix operators.

### 5.11 Layout

Every Base aggregate has the target C ABI's layout: declaration order, the ABI's alignment and padding, no reordering. A struct has at least one field, as in C. `packed struct Name:` removes padding; `align(N) struct Name:` raises alignment to the power of two `N`; `align(N)` before a field raises that field's alignment, which is how two hot atomics are placed on separate cache lines. Taking the address of a field of a packed struct is a compile error unless the field's natural alignment is one, because C makes the resulting dereference undefined and Base does not produce the pointer.

`sizeof(T)`, `sizeof(expression)`, `alignof(T)`, and `offsetof(T, field)` are constants of type `usize` (§5.1). `alignof` takes a type only, as in C.

Base has no type-based aliasing rule. Any pointer may alias any object of compatible size and alignment, and a backend may never assume otherwise. A parameter may be declared `noalias` (§9.2), which is the one place a program asserts non-aliasing, and violating it is undefined (§12.6).

**Why.** A Base struct is a C struct; that is what lets Base be the language C libraries are bound in and the runtime is written in. The aliasing guarantee exists because unions and `(T*)` casts are how C reinterprets memory, and a future backend adding type-based alias analysis would break every program that uses them. `noalias` gives the optimiser the fact it needs in the code that needs it, DSP loops and matrix kernels, without a global rule.

## 6. Bindings and initialisation

### 6.1 `let`, `var`, and zero values

```luce
let width = 1920
let title: str = "Preview"
var frame = 0
frame += 1
```

`let` binds once. `var` may be reassigned and never changes type. Parameters are `let`. A `let` requires an initialiser. A `var` requires an initialiser unless it has a written type that is **zeroable**, in which case it holds that type's zero value.

A type is zeroable when the all-zero bit pattern is a valid value of it:

- integers, floats, `bool`, `char`, `usize`, `isize`;
- nullable pointers, nullable functions, nullable interface views;
- `@T` of a zeroable `T`;
- spans and `str`: the zero value is the empty span with the dangling pointer of §5.4;
- `T?` of any type: the zero value is `none`;
- integer-backed enums with a case whose value is `0`;
- payload enums whose first declared case carries no payload: that case is the zero;
- arrays and unions of zeroable types;
- structs whose fields are all zeroable and that declare neither a custom `init` nor a field default.

The following have no zero value and always require an initialiser: `T*`, `const T*`, `volatile T*`, `func(...) -> R`, a bare interface view, and every aggregate that contains one of them.

```luce
var count: u32                  # 0
var buffer: u8[4096]            # zeroed
var next: Node*?                # none
var origin: Point               # every field zero; rejected if Point holds a bare pointer
```

**Why.** C's `char buf[4096] = {0};` is the most common initialiser in systems code, and `= {0}` is easy to forget. Zero by default is what Go and Odin do, and it is safe precisely because the never-null types are excluded: a zeroed `Node*` would be a null pointer with a non-null type, so it is not allowed to exist.

### 6.2 Explicit non-initialisation

```luce
var buffer: u8[4096] = ---
var nodes: Node[64] = ---
```

`---` allocates the storage without writing it, for a `var` of any type. Reading before writing is undefined, as in C. The linter warns on every `---` by default; a diagnostic build fills the storage with a pattern.

**Why.** A large buffer that a `read` call will fill should not be zeroed twice, and a pool of structs that hold bare pointers has no other spelling: the type is not zeroable, and the pool will be filled by the program before use. The spelling is deliberately conspicuous so that it can be searched for.

### 6.3 Module globals

```luce
var next_id: u32
var seed: u32 = 0x9E3779B9
thread_local var current_arena: Arena*?
pub let max_header: usize = 16 * 1024
```

A module may declare `var` at top level. It is zero before `main` runs, or holds the constant its initialiser names; the initialiser must be a constant expression (§6.4), so there is no initialisation order. `thread_local var` is one such variable per thread, C11's `_Thread_local`: ordinary zero-initialised or constant-initialised storage in each thread, with no per-access guard. A top-level `let` is a constant. C's function-scope `static` local is a module-level `var`.

**Why.** Full Luce forbids mutable globals because their initialisation order and their effect on test isolation are unmanageable. Base cannot forbid them, because a C replacement without globals is not a C replacement, but it removes the two hazards: a constant initialiser is static data with nothing to order, and the test runner reports which globals a test wrote (§16.5).

### 6.4 Constant expressions

A constant expression is built from literals; `sizeof`, `alignof`, and `offsetof`; `luce.location`, `luce.file`, `luce.line`, and `luce.function`; arithmetic, bit, comparison, and cast operators on constants; array and tuple literals of constants; enum cases and `|` on integer-backed enums; struct construction from constants; the address of a global or a function; and top-level `let` names. It may appear as a top-level initialiser, an array length, a default parameter value, and the condition of a module-level `assert`. `luce.location` (and its pieces) expand to the file, line, and function of the use site; when used as a default argument they expand at the call site.

### 6.5 Assignment

The right-hand side is fully evaluated before the destination changes. Values copy. A field or index path is one lvalue, evaluated once. Assignment is a statement; there are no `++`, `--`, or assignment expressions. A tuple binding `let (a, b) = pair` destructures once, left to right.

```luce
var cursor = Cursor(position = 1)
cursor.position = 4
items[index].selected = true
*p = value
p.field = value
```

### 6.6 Address-of, mutability, and the escape rule

`&x` yields the address of an lvalue. The pointer's qualifier comes from the nearest root of the path: a `var` binding or a dereference of a `T*` yields `T*`; a `let` binding, a dereference of a `const T*`, or a read-only span yields `const T*`. So `&task.link` is `Link*` when `task` is a `let` holding a `Task*`, because the path is rooted at the pointee, not the binding. Stores follow the pointee: `*p = v` requires `T*`; `p.field = v` requires `T*` and a `var` field.

The address of a local, and a span or `str` of a local array, may be passed down but not up. The compiler rejects four uses of such a pointer or view: returning it; passing it as the message to `error(...)`; storing it in a global; and storing it through a pointer parameter or into a struct that is returned. The check follows `let` aliases within one function and stops at calls. It catches the common mistakes and promises nothing about the rest, which remain the programmer's responsibility, as in C.

**Why.** C has `&x` and no way to say whether the result may be written through. Deriving the qualifier from the path gives the same information with no annotation, and it is the same rule Luce already uses to decide whether a field path may be assigned.

## 7. Expressions

### 7.1 Evaluation order

Left to right, always: receiver then arguments, operands, array elements, interpolation fields, constructor arguments. A backend may reorder only when traps and side effects are unobservable.

### 7.2 Arithmetic

| Operators | Meaning |
| --- | --- |
| `+`, `-`, `*` | checked integer or IEEE float arithmetic; integer overflow traps |
| `+%`, `-%`, `*%`, unary `-%` | two's-complement wrapping; unary `-%` is defined on unsigned types too, so `x & -%x` isolates the lowest set bit |
| `+|`, `-|`, `*|` | saturating |
| `+?`, `-?`, `*?` | checked, yielding `T?`: `none` on overflow |
| `/` | float division; both operands must be floats |
| `//` | integer division, truncating toward zero: `-7 // 2 == -3`, as in C |
| `%` | remainder with the sign of the dividend: `-7 % 2 == -1`, as in C |
| unary `-` | sign; rejected on unsigned types (use `-%`) |

Integer division by zero traps. `minimum_signed // -1` traps as overflow. Floor division and modulo are `math.floor(a, b)` and `math.mod(a, b)`. Constant folding uses the same rules as runtime. Float arithmetic is IEEE 754 with no contraction or reassociation.

**Why trapping is the default.** C wraps unsigned arithmetic silently and leaves signed overflow undefined, and both are the source of most exploitable integer bugs. Base traps, because a trap reports the location of the overflow and a wrap does not, and the check is one predicted branch. Hashing, PRNGs, and checksums wrap on purpose and use the `%` operators; overflow-aware code uses `+?` and handles `none`.

**Why C's division.** Full Luce's `//` and `%` floor. Floor division costs an adjustment after the divide instruction on signed operands, and it differs from C silently for negative operands, which a port would not notice until a result was wrong. Base follows the instruction and C. This is a stated difference from full Luce for negative signed operands only (§23).

### 7.3 Bits, comparison, logic

Integers, including `usize` and `isize`, support `&`, `|`, `^`, `~`, `<<`, `>>`. A shift count of the operand's width or more traps; `<<` discards bits shifted past the width, as in C, and is never an overflow. Signed right shift is arithmetic. Conditions are `bool`; there is no truthiness. `and` and `or` short-circuit; `not` takes a `bool`. Comparisons do not chain: write `low <= x and x < high`. `not a == b` is rejected; write `not (a == b)`.

### 7.4 Equality and hashing

`==` and `!=` exist for scalars, `char`, `str` (by bytes), tuples, arrays, structs, enums, optionals, and pointers (by address), when every component supports equality. A struct or enum whose components are hashable is hashable, and `hash(value) -> u64` is process-seeded and not stable across runs. Pointers hash by address. Unions and interface views have neither. No user type overloads an operator; a domain with unusual equality exposes a named method.

### 7.5 Conversions and casts

Base has two conversion spellings with two meanings.

**Widening is implicit.** An integer converts implicitly to a wider integer of the same signedness: `u8` to `u16`, `u32`, `u64`, or `usize`; `i32` to `i64` or `isize`. No value can change, so no spelling is required. Nothing else converts implicitly: narrowing, a change of signedness, and conversion to or from a float are always written.

**`T(x)` is the checked conversion.** `u32(length)` traps if the value does not fit. `f64(count)` rounds to nearest. `i32(f)` truncates toward zero and traps on NaN or out of range. `c.long(n)` is checked. `Kind(n)` from an integer traps when `n` is not a declared case of the integer-backed enum `Kind`. `str(bytes)` and `str(value)` validate text (§5.5) and yield `str!` rather than trapping, because invalid input is ordinary data, not a program error. A conversion the compiler can prove impossible, such as `u32(-1)` from a negative literal, is an error.

**`(T)x` is C's cast, with every case defined:**

| Cast | Meaning |
| --- | --- |
| integer to narrower integer | truncation to the low bits |
| integer to a different signedness | reinterpretation of the bits |
| float to integer | truncation toward zero, saturating to the destination's range; NaN becomes `0` |
| integer to float, float to float | value conversion as in C |
| `(u32)flag` | an integer-backed enum to its representation |
| `(Kind)n` | an integer to an integer-backed enum with no check |
| `(str)bytes`, `(cstr)text` | text reinterpretation with no check (§5.5) |
| `(T*)p` from `U*`, `void*`, `const T*`, `const void*` | pointer conversion; removing `const` is permitted and explicit, and modifying an object that was declared `let` or `const` through the result is undefined as in C |
| `(T*?)n` from `usize` | integer to pointer; nullable because zero is a valid integer |
| `(usize)p` | pointer to integer |
| `(func(...) -> R)p` from `void*`, `(void*)f` | function-pointer conversion, target-dependent, provided for `dlsym` |
| `(T*)f`, `(func)p` for an object pointer | rejected |

There is no reinterpreting cast between scalars of different kinds. `f32.bits(u32)` and `f64.bits(u64)` build a float from its bits and `value.bits()` reads them; a union (§10.4) reinterprets anything else.

The parser reads `(` *type* `)` as a cast when the parenthesised text is a type ending in `*`, `[]`, `[N]`, or `?`, or is a scalar, `str`, `cstr`, or `c.` type name, or a parenthesised function type. `(Name)(x)` with a bare struct name is a call, not a cast; struct-to-struct casts do not exist, so nothing is lost.

**Why implicit widening.** Full Luce requires every integer conversion to be written, and its arithmetic code is correspondingly full of `u64(index)`. A widening of the same signedness cannot change a value or lose one, so requiring it to be written costs the reader without protecting them. Zig admits exactly this conversion and nothing more, and the rule has held there.

**Why two spellings.** The checked form is Luce's and is the ordinary one. The C cast exists because ported code is full of `(uint8_t)x` that means "the low byte", and making it trap would make porting impossible. Giving the C spelling C's meaning, and defining the one case C leaves undefined, keeps the two distinct: the parenthesised one is C, the constructed one is Luce.

### 7.6 Members, calls, indexing, slicing

```luce
image.width
image.resize(scale = 0.5)
pixels[10]
pixels[10..<20]
decode[Header](data)
```

`.` accesses a field, method, or module member, and auto-dereferences a pointer: `p.field` is `(*p).field`. Calls use `()`. Indexing and slicing are checked. Slicing is half-open, `[start..<end]`, with `[..<end]` from zero and `[start..]` to the end. There is no negative indexing and no step. Brackets whose contents are types are generic arguments; brackets whose contents are an expression are an index; a bracket immediately after a name that is a type is never an index.

### 7.7 Pointer operations

| Form | Meaning |
| --- | --- |
| `*p` | load; `*p = v` stores |
| `p.field`, `p.method()` | auto-dereference; `->` is not needed and remains the type arrow |
| `&x` | address-of (§6.6) |
| `p + n`, `p - n`, `p[i]` | element-scaled arithmetic and unchecked indexing; `n` and `i` are `usize` or `isize` |
| `p - q` | element difference as `isize`; same pointee type required |
| `p == q`, `p < q` | address comparison (§5.3) |

Pointer arithmetic is unchecked: producing a pointer outside the object `p` addresses, or one past its end, and using it, is undefined as in C. Spans cover the cases where a length is known.

**Reading typed values from untyped memory.** Prefer declaring a struct for what is stored and casting once where the untyped memory enters; the cast-and-dereference form `*(Link*)block` is legal and is the last choice. When no struct fits, a header decoded from a buffer or a C structure walked by offsets, `memory.read[T](address: void*) -> T` and `memory.write[T](address: void*, value: T)` copy `sizeof(T)` bytes at the address with no alignment assumption. `memory.copy(to, from, count)`, `memory.move(to, from, count)` for overlapping ranges, and `memory.set(span, byte)` are `memcpy`, `memmove`, and `memset`.

**Why no `->`.** Luce already uses `->` to declare a result type, and a symbol with two meanings is a cost. Auto-dereference on `.` is what Go, Odin, and Zig do, and C++ references do the same.

### 7.8 Conditional and `match` expressions

```luce
let label = "ready" if ready else "waiting"

let kind = match c:
    '0'..='9' => "digit"
    'a'..='z', 'A'..='Z' => "letter"
    _ => "other"
```

The conditional expression requires both branches and one common type. A `match` expression yields the chosen arm's value with `=>`; §8.4 states the pattern rules, which are shared with the statement form.

### 7.9 Discarded values

A call may appear as a statement. A non-`unit` result is discarded and the linter warns; `discard(call())` states the intent. A fallible result must be handled before it can be discarded.

### 7.10 Precedence

From tightest to loosest: member, call, index; unary `try`, `not`, `-`, `-%`, `+`, `~`, `*`, `&`, cast; `*`, `/`, `//`, `%`, `*%`, `*|`, `*?`; `+`, `-`, `+%`, `-%`, `+|`, `-|`, `+?`, `-?`; `<<`, `>>`; `&`; `^`; `|`; `..<`, `..=`; comparison; `and`; `or`; conditional expression; the optional `else` fallback (§11.1); `catch`.

## 8. Control flow

Control flow is structured. Base has `if`, `while`, `for`, `match`, labeled `break` and `continue`, `return`, `defer`, `errdefer`, `with` (§12.3), and inline assembly. It has no exceptions and, in this revision, no `goto`.

### 8.1 `if` and `if let`

```luce
if temperature > 30.0:
    fan.start()
elif temperature < 10.0:
    heater.start()
else:
    climate.hold()

if let user = cache.find(name):
    greet(user)
else:
    log("not found")
```

Conditions are `bool`. `if let name = optional:` binds the present value inside the branch only; it may be followed by `elif` and `else`.

### 8.2 `while` and `while let`

```luce
while cursor.more():
    parse_one(cursor)

while let token = lexer.next():
    consume(token)
```

`while let` binds on each iteration while the expression is present. It is the replacement for C's assignment-in-condition idiom. There is no `do while`; write the first step before the loop.

### 8.3 `for`, ranges, and iteration

```luce
for item in items: render(item)
for index in 0..<image.width: draw_column(index)
for value: u32 in 1..=4: try ring.push(value)
for entity in &entities: entity.position += entity.velocity
for (index, byte) in data.indexed(): table[byte] = index
for character in text: count += 1
```

`start..<end` is half-open, `start..=end` closed; both are integer ranges that ascend by one. When the bounds are untyped literals or one bound is untyped, the range's type is the typed bound's, and a range of two untyped literals is `usize`. The loop variable may carry a type, `for value: u32 in 1..=4`, which becomes the range's type. Descending and stepped traversal are library iterators. The loop variable is a `let` scoped to the body.

`for x in items` over a span or array yields each element by value. `for x in &items` yields a pointer to each element, `T*` for a mutable span or array and `const T*` otherwise, so the body may modify elements in place. `items.indexed()` yields `(usize, T)` pairs. `for character in text` yields Unicode scalars.

`for` consumes the `Iterable` protocol (§14.4): it calls `source.iterator()` once, stores the resulting value iterator in a hidden local of its concrete type, and calls its `mutating next() -> T?` until `none`. It never forms an interface view of the iterator, so nothing dangles and nothing allocates. Spans, arrays, ranges, and `str` are iterable; a user type implements `Iterable` with a value iterator.

### 8.4 `match`

```luce
match command:
    .open(path): open_document(path)
    .save(path) if path.length == 0: error(empty_path, "no path")
    .save(path):
        validate(path)
        save_document(path)
    .quit: return
```

Patterns are closed: enum cases with payload bindings, where `_` stands for a payload that is not used; `.some(value)` and `.none`; boolean, integer, character, and string literals; `lower..<upper` and `lower..=upper` literal ranges; comma-separated alternatives that share one body; and `_`. A pattern may carry a guard, `pattern if condition:`, evaluated after the pattern binds; a guarded arm does not count toward exhaustiveness.

Every `match` is exhaustive, and the compiler names missing cases. A `match` on an integer, `char`, `str`, or integer-backed enum requires a `_` arm unless every value is covered. Duplicate and unreachable patterns are errors. Alternatives that bind payloads must bind the same names with the same types. There is no fallthrough and no nested destructuring. The statement form uses `:` per arm; the expression form (§7.8) uses `=>`; they do not mix.

**Why.** C's `switch` has fallthrough, no exhaustiveness, and no payloads. Exhaustive `match` over a payload enum is the largest correctness gain in this document over C, and it costs the same jump table. Guards are admitted because state machines and tokenisers need them and the alternative is one nesting level per condition; full Luce refused them, and the difference is recorded in §23.

### 8.5 `break`, `continue`, and labels

```luce
rows: for y in 0..<height:
    for x in 0..<width:
        if pixel(x, y) == target:
            found = (x, y)
            break rows
```

`break` and `continue` act on the innermost loop, or on the named loop when a label is given. A label is `name:` immediately before `while` or `for`; it lives in its own namespace and scopes over the loop body. `defer` runs for every scope left, innermost first.

**Why labels and not more.** Leaving a nested loop is the most common use of `goto` in C that `defer` does not already cover, and a labeled break is a structured jump: it leaves scopes and never enters one, which the compiler's structured intermediate representation expresses with the branch it already has. Full Luce refuses labels and asks for a helper function; Base admits them because extracting a function in order to leave a loop is a cost C code never pays.

### 8.6 `goto`

`goto` is reserved and not implemented. If a future revision admits it, it follows Go's rules: targets within the same function; a jump may leave scopes, running their deferred calls, but may not enter a scope it is not already inside; it may not skip a binding's declaration; no computed targets; no jumps into or out of `match` arms.

**Why.** The compiler's intermediate representation is structured, with blocks, loops, and branches to an enclosing region, and it was built that way so that the WebAssembly backend never has to reconstruct structure from a jump graph. An unrestricted `goto` would force that reconstruction into the compiler. Every use of `goto` in C is one of: retry (a `while`), error exit (`defer` and `errdefer`), leaving nested loops (labels), or a hand-written state machine. A state machine is written `while true: match state:` at the cost of one branch per transition; Zig, Odin, C3, and Hare have no `goto` and their users write it this way. The feature is reserved so that it can be admitted later if interpreter-style Base code shows the cost is justified.

### 8.7 `return`

`return value` exits the function. A `unit` function uses bare `return` or reaches its end. Every path of a non-`unit` function returns or terminates with `error`, `trap`, or a `never` call. There is no implicit return of a final expression.

### 8.8 `defer` and `errdefer`

```luce
import files

let file = try files.open(path)
defer file.close() catch failure:
    log(f"close failed: {failure.message}")

let buffer = try new u8[size]
errdefer free(buffer)
try fill(buffer)
return buffer
```

`defer call` registers a call for the end of the current lexical scope, run last-in-first-out on normal exit, `return`, `break`, `continue`, and error propagation. The receiver and arguments are captured at registration. The call must produce `unit`: a fallible call is deferred with a `catch` handler that recovers, or wrapped in `discard(...)`. A deferred call may not `return`, `recover` out of the enclosing function, or replace an in-flight error.

`errdefer call` registers a call in the current scope that runs only when control leaves that scope because an error is propagating out of it, from `try` or from `error(...)`, in order with the ordinary `defer` calls of the same scope. It is discarded when the scope exits normally. A `catch` that recovers inside the scope never triggers it, because no error leaves. It is a compile error in a non-fallible function.

Neither runs on a trap, process abort, or power loss.

**Why.** `defer` is C's `goto cleanup` without the label. `errdefer` is the half of it that runs only on the failure path, which is what the partial-acquisition pattern (allocate A, allocate B, fail, free A) needs and what C spells with a sequence of labels. Zig has both, and its users rely on them.

### 8.9 Inline assembly

```luce
func write(fd: i32, data: const u8[]) -> isize:
    var result: isize
    asm x86_64 (in("rax") 1, in("rdi") fd, in("rsi") data.data, in("rdx") data.length,
                out("rax") result, out("rcx") _, out("r11") _, options(nostack)):
        syscall
    asm arm64 (in("x8") 64, in("x0") fd, in("x1") data.data, in("x2") data.length,
               out("x0") result, options(nostack)):
        svc #0
    return result

func ticks() -> u64:
    var low: u32
    var high: u32
    asm x86_64 (out("rax") low, out("rdx") high):
        rdtsc
    asm arm64 (out("x0") low, out("x1") high):
        mrs x0, cntvct_el0
        lsr x1, x0, #32
    return (u64(high) << 32) | low
```

- `asm ARCH (operands):` opens a suite whose lines the lexer captures raw: no tokens, no comment stripping, because `#` is an immediate prefix in ARM64 assembly. The suite's indentation baseline is removed; the suite ends at the dedent.
- `ARCH` is a target architecture name (§19.5). A function may carry one block per architecture in sequence; the compiler emits the one matching the build target and rejects a build for a target with no block. There is no fallback. The intermediate representation carries every variant and the backend selects, so no target name enters the shared representation.
- An operand is `in(place) expression`, `out(place) lvalue`, or `inout(place) lvalue`, where `place` is a register name in quotes or `reg` for any register the compiler chooses. `out(place) _` declares a register the block destroys. `{name}` in the text stands for the register the compiler chose for `reg` operands named `name`. `options(...)` may state `nostack` (the block does not use the stack), `nomem` (the block reads and writes no memory other than its operands), `pure` (the block may be removed if its outputs are unused), and `flags` (the block preserves the condition flags). Operands are scalars and pointers.
- Every block without `pure` has side effects: it is never elided, duplicated, or reordered with another block or a `volatile` access. A block that declares `nomem` may be moved across ordinary memory operations.
- The text is the target assembler's, passed as written: GNU AT&T syntax on x86-64, standard syntax on ARM64. A block may begin with `.intel_syntax noprefix` and end with `.att_syntax prefix`.
- `naked func` (§9.8) declares a function whose body is exactly one `asm` block per architecture, with no prologue or epilogue generated: the block owns the stack and registers, which is how an interrupt entry, a context switch, and a `_start` are written.
- A module-level `asm ARCH:` block with no operand list places raw assembly at file scope: symbols, sections, data.
- The WebAssembly target rejects `asm`. The compiler's reference interpreter rejects programs containing it, so such programs are proven by the compiled backends only.

**Why this syntax.** GCC's constraint strings encode operand kinds and registers in single characters; Zig adopted them. Named operands with the register in quotes, as in Rust, are easier to read and let the block name the registers a syscall or a calling convention requires. The per-architecture block replaces `#ifdef __x86_64__` without a preprocessor.

## 9. Functions and methods

### 9.1 Declaration and calls

```luce
import io
import luce
from io import Location

pub func clamp(value: f64, minimum: f64, maximum: f64) -> f64:
    if value < minimum: return minimum
    if value > maximum: return maximum
    return value

func render(scene: Scene*, samples: u32 = 64, denoise: bool = true) -> Image!:
    ...

func log(level: Level, message: fmt, at: Location = luce.location):
    io.stderr().write(f"{at.file}:{at.line}: {message}\n")

let image = try render(&scene, samples = 256, denoise = false)
log(.warn, f"lost {count} packets")
```

- Parameter types and every non-`unit` result are explicit. A missing `->` means `unit`. A fallible function that returns nothing is written `-> !`, the short form of `-> unit!`; both are accepted.
- Arguments are positional or named with `name = value`; positional ones come first. A default is a constant expression embedded at the call site. `luce.location` is a compile-time `Location` (`file` and `function` as `str`, `line` as `u32`); as a default it expands at the call site, which is how a log records the caller without a macro. Duplicate, unknown, and missing arguments are compile errors.
- A parameter of type `fmt` accepts a formatted string or a `str`. Inside the function it may be written to a `Writer`, passed to `print` or `format`, or passed on to another `fmt` parameter, and nothing else: it cannot be stored, returned, or compared. It is lowered as a pointer to the caller's field values beside a generated formatting function, with no allocation. This is what makes a logging function possible without a macro.
- One scope holds at most one callable with a given name: no overloading. Alternatives get semantic names: `Image.open`, `Image.decode`.
- No variadic Base functions in this revision (§20). Calls to variadic C functions are §17.2.
- Recursion is allowed; running out of stack is a trap, never a crash (§11.5).
- A function that never returns is declared `-> never`; `extern func abort() -> never` binds C's `_Noreturn`.

**Why `=` for named arguments.** `:` already means "has this type" everywhere in the language. Using it for "takes this value" would put two relations behind one symbol at exactly the two places a reader confuses them.

**Why `fmt` and `luce.location`.** Function-like macros in C exist for two things above all: a logging call that takes a format string and forwards it, and a check that reports its own file and line. A parameter type that carries a formatted string without materialising it, and a compile-time source location expanded at the use site, cover both without a preprocessor. `location()` as a fake function would have been a runtime-shaped lie about a fact the compiler already knows.

### 9.2 Parameters and the calling convention

A `T[N]` parameter is a value copy. A `T[]` parameter is a lent view. A `T*` parameter is a pointer. There is no `inout`; pass a pointer. A parameter of pointer or span type may be declared `noalias`, `out: noalias f32[]`, asserting that the memory it addresses is not addressed by any other parameter or reachable pointer for the duration of the call; the backend may then keep loads in registers, and a violation is undefined (§12.6).

Every Base function whose signature is C-representable (§17.6) uses the target C calling convention whether or not it is exported, so its address may be handed to C. Sub-word parameters (`bool`, `i8`, `u8`, `i16`, `u16`, `c.char`) use the ABI's sub-word rules, which differ between AAPCS64 and Apple arm64; the backend handles them.

### 9.3 Multiple results

```luce
func divide(value: i64, divisor: i64) -> (i64, i64):
    return (value // divisor, value % divisor)

let (quotient, remainder) = divide(17, 5)
```

Tuples replace C's out-parameters for the common case. Public data with names is better served by a struct.

### 9.4 Function values

```luce
func main(arguments: str[]) -> i32:
    let operation: func(i64, i64) -> i64 = add
    return i32(operation(2, 3))
```

A function value is a C function pointer (§5.6). A non-fallible function converts to the corresponding fallible function type; nothing else converts.

### 9.5 Methods

```luce
import math

struct Point:
    pub let x: f64
    pub let y: f64

    func distance(other: Point) -> f64:
        let dx = self.x - other.x
        let dy = self.y - other.y
        return math.sqrt(dx * dx + dy * dy)

    static func origin() -> Point:
        return Point(0.0, 0.0)

struct Cursor:
    pub var position: usize

    mutating func advance(amount: usize):
        self.position += amount
```

- A `func` declared inside a type is a method. `self` is implicit: it names the receiver inside the body, is not written in the parameter list, and cannot be used as a parameter name. `point.distance(other)` passes `point` as `self`.
- `static func` declares a function that belongs to the type and has no receiver; it is called through the type, `Point.origin()`. This is the C++ and Java meaning of `static`, "of the type, not of the instance". C's other meaning, internal linkage, is what every declaration not marked `pub` already has in Base, so the two never collide.
- `mutating` marks a method that assigns `var` fields or replaces `self`; a `static func` cannot be `mutating`. The receiver at a `mutating` call site must be a `var`, a mutable pointer, or a mutable span element.
- A non-`mutating` method receives `self` as `const Self*`; a `mutating` method receives `self` as `Self*`. This is deterministic so that exported headers are stable. Because `self` aliases the receiver, a callee that mutates the receiver through another pointer changes what `self.x` reads mid-method. `self = value` in a mutating method stores through the pointer.
- A method is callable on a value, a `var`, or a pointer; `p.advance(3)` on `p: Cursor*` needs no dereference. Calling on an rvalue materialises a temporary.
- `value.member` without `()` is always a field. There are no computed properties.
- A method named `init` is the initialiser (§10.1); it is never `static`.

**Why implicit `self`.** Full Luce writes `self` as the first parameter, which is Python's convention and is justified in a language where a function inside a type may or may not be a method. In Base, having a receiver is what a function inside a type does by default, `mutating` already states what the receiver permits, and a C programmer reading `func distance(other: Point)` inside a struct knows what it is. The explicit parameter would have been a parameter in every method that carries no information, and `static` is the word C programmers already use for the exception.

**Why `self` is a pointer.** In full Luce a non-mutating method receives a copy, which is safe under reference counting and invisible to the caller. In Base a copy of a large struct on every method call is a cost C programmers would notice, and a struct method in C3, Zig, and Odin takes a pointer. Making the convention deterministic, rather than "by value if small", is what lets the generated C header say `const Point*`.

### 9.6 Closures

There are none. A capture-free lambda `(x) => x + 1` converts to a function pointer. A callback that needs state is written the way C writes it: a function pointer beside a `void*` or `T*` context.

**Why.** A closure that captures by reference needs its environment to outlive the frame, and without reference counting there is no owner for that environment. Every manual-memory language that offers closures either restricts them to non-escaping, or hands the programmer an allocator at closure creation. Both are plausible later; neither belongs in the first revision.

### 9.7 The entry point

```luce
pub func main(arguments: str[]) -> i32:
    print("Hello")
    return 0
```

`main` takes `str[]` or `cstr[]` and returns `i32` or `i32!`. The startup shim builds `arguments` from `argc` and `argv`; with `str[]` it validates each argument as UTF-8 and traps `invalid_utf8` on failure, so a program that takes paths from the command line declares `cstr[]`, because a path on POSIX is bytes. A returned error is printed to standard error as its code and message and becomes exit status 1.

### 9.8 Declaration attributes

A small closed set of words may precede a `func` or a top-level `var`, each one a fact the linker or the code generator needs and nothing else can express:

| Word | Meaning |
| --- | --- |
| `inline func` | inline at call sites; an implementation without an inliner ignores it |
| `noinline func` | never inline |
| `cold func` | rarely executed; place and optimise accordingly |
| `naked func` | no prologue or epilogue; the body is one `asm` block per architecture (§8.9) |
| `weak func`, `weak var` | a weak symbol that another definition may override |
| `used func`, `used var` | keep the symbol even if nothing references it |
| `section("name") func`, `section("name") var` | place the symbol in the named linker section |

They combine, `used section(".isr_vector") var vectors: Handler[64] = ...`. There is no general attribute syntax; this set is the language.

**Why.** Embedded and kernel code needs an interrupt table in a named section, a weak default handler, a symbol the linker must keep, and a function with no prologue. Each is a fact about one declaration, and a word before the declaration is the shortest way to state it.

## 10. Structs, enums, and unions

### 10.1 Structs

```luce
pub struct Style:
    pub let color: Color = Color(0.0, 0.0, 0.0, 1.0)
    pub let width: f64 = 1.0
    var cache: Layout*?

pub struct Percentage:
    let value: f64

    pub func init(value: f64) -> !:
        if value < 0.0 or value > 100.0:
            error(out_of_range, "percentage must be 0 through 100")
        self.value = value

func main(arguments: str[]) -> i32!:
    let thin = Style(width = 0.5)                 # color defaulted; cache is nullable and defaults to none
    let opacity = try Percentage(75.0)
    return 0
```

- Fields use `let` or `var` and are private to the module unless `pub`. Declaration order is layout order (§5.11).
- Without an `init`, the compiler synthesises a memberwise initialiser, `Style(color = ..., width = ...)`, positional or named. A field with a default may be omitted; a zeroable field without a default may be omitted and receives its zero value; every other field is required. The initialiser is public when the struct and every required field are public.
- A custom `init(...)` returns `unit` or `!`, assigns every field exactly once, and cannot read a field or call a method before every field is assigned. Declaring one suppresses the memberwise form. Construction `Percentage(75.0)` produces `Percentage` or `Percentage!`.
- A struct copies by value. There is no spread or update syntax; a mutable local updates `var` fields, and immutable transformations are named methods.
- A struct with all-hashable fields is equatable and hashable structurally (§7.4).
- A struct cannot contain itself directly; use a pointer.

### 10.2 Enums

```luce
pub enum Direction:
    north
    east
    south
    west

pub enum Command:
    open(path: str)
    save(path: str)
    resize(width: u32, height: u32)
    quit
```

A case is written `Direction.north` or `.north` where the type is known. A payload is reached only by `match`. The tag and payload layout is the compiler's; ordinary code never sees an ordinal. Adding a case is a source-compatibility change because every exhaustive `match` must be updated, which is intended. A case may not be named `none`.

### 10.3 Integer-backed enums

```luce
enum Access as u32:
    empty = 0
    read = 1
    write = 2
    execute = 4

let mode = Access.read | Access.write
if (mode & Access.write) != Access.empty: ...
let bits = (u32)mode                     # 3
let kind = Access(value)                 # traps unless value is a declared case
let raw = (Access)value                  # no check
```

An enum declared `as` an integer type with explicit values has that representation. `|`, `&`, `^`, and `~` are defined and produce the same enum type, so flags combine and test without conversion. Because a combination need not be a declared case, a `match` on an integer-backed enum requires a `_` arm. `(u32)flag` reads the representation; `Access(n)` is the checked conversion and traps on an undeclared value; `(Access)n` is the C cast and does not. Its C header form is §17.6.

**Why.** C enums are integers and are used as flags and array indices; a Base that refused that could not bind C libraries. The operators produce undeclared values by design, which is why the `_` arm is mandatory: the compiler cannot enumerate what a flag set may hold.

### 10.4 Unions

```luce
union Value:
    integer: i64
    real: f64
    bytes: u8[8]
```

A union stores one member at one address. Its alignment is its most-aligned member's, and its size is its largest member's size rounded up to that alignment. Reading a member other than the last written reinterprets the bytes, the rule of C11 §6.5.2.3 footnote 95. A member may be of any type; reading a member whose type has an invariant, a `bool`, an enum, a bare pointer, a `str`, an optional, after another member was written is undefined (§12.6), as it is in C. A union is zeroed as bytes, may declare methods, and cannot implement interfaces or cross into full Luce.

**Why.** The tagged enum is the safe sum type. The raw union exists because C has it, C libraries expose it, and type punning through it is defined C. Members of every type are admitted because a union declared in Base must be able to mirror one bound from C.

## 11. Absence, failure, and traps

Base separates three conditions: absence is data, `T?`; recoverable failure crosses a function boundary as `T!` with an `Error`; a trap is a violated invariant and is not catchable. There are no nullable references, no exceptions, and no error hierarchies.

### 11.1 Optionals

```luce
func find(id: UserId) -> User*?

if let user = find(id):
    show(user)

let count = parse(text) else 0                             # fallback value
let n = parse(text) else trap("not a number")              # assert with a reason
let w = create_window() else error(no_window, "no window") # absence becomes failure
```

`none` needs an expected optional type. `T` promotes to `T?` where expected; the reverse needs `if let`, `match`, or the three-arm `else`. Optionals are one layer (§5.8). There is no force-unwrap operator; `else trap("reason")` is the explicit spelling.

### 11.2 Fallible functions

```luce
import files

func load(path: cstr) -> Config!:
    let data = try files.read(path)
    return try config.parse(data)
```

`T!` means "returns `T` or an `Error`". `try expression` is valid only on a `T!` and only inside a fallible function: on success it yields `T`, on failure it returns the same error from the current function. A non-fallible caller must `catch`. `T!` is a result effect, not a storable type: it cannot be a parameter, field, or element; a program that must hold a result declares an enum with a success and a failure case. `T?!` is a fallible optional.

Representation: a `T!` is returned as the value plus a two-word `Error` and a flag, in registers where the ABI allows. Exported fallible functions use the status form of §17.6.

### 11.3 Errors

```luce
pub let not_found: ErrorCode = ErrorCode.package(1)

error(not_found, "configuration file does not exist")
```

`error(code, message)` has type `never` and is legal only in a fallible function or a `catch` handler. `Error` is `{ code: ErrorCode, message: str }`. `ErrorCode` is a package identity plus a `u32`. The identity is the package name from the manifest (§16.4), so codes never collide across packages; `ErrorCode.package(n)` may appear only as a top-level constant initialiser, and the compiler rejects two constants in one package with the same `n`. The message is a `str` view: a literal costs nothing, and a formatted message uses `format` on a buffer that outlives the function, an arena or a caller's buffer, never a local; the escape rule of §6.6 rejects the local case.

### 11.4 `catch` and `recover`

```luce
import files

let text = files.read(path) catch failure:
    if failure.code == files.missing:
        recover ""
    error(failure.code, failure.message)
```

`expression catch name:` handles only that expression's failure. The handler must `recover value`, terminate with `error` or `trap`, `return`, or, inside a loop, `break` or `continue`. `catch` binds more loosely than any operator.

### 11.5 Traps

`trap(message)` stops the program with a diagnostic and a source trace. The compiler inserts traps for: out-of-bounds indexing, checked overflow, division by zero, shift by width, a failed `T(x)` conversion, `else trap`, `assert`, a zero arriving in a bare pointer slot at a C boundary (§17.1), invalid UTF-8 in `main`'s `str[]` arguments, `new` or `alloc` with no allocator set (§12.3), and stack exhaustion. Stack exhaustion is detected by a guard page that the startup shim installs; a `--freestanding` program that supplies its own `_start` installs its own or has none. Traps are never recoverable; `defer` does not run.

### 11.6 Assertions

`assert(condition)` and `assert(condition, message)` trap when the condition is false. The condition must be side-effect-free. Assertions are never removed by a build profile. An `assert` at module level, outside any function, is evaluated at compile time and its condition must be a constant expression; it is C's `static_assert`:

```luce
assert(sizeof(Header) == 32, "Header must match the wire format")
```

### 11.7 Out of memory

Out of memory is a recoverable `memory.exhausted` error from `new` and `alloc` (§12.2), not a fatal termination as in full Luce.

**Why.** Full Luce makes allocation infallible and out-of-memory fatal, because making every list append fallible would make every API fallible for a condition most hosts cannot recover from anyway. Base programs often run with a fixed memory budget: an allocator over a fixed buffer running out is an ordinary condition there, and the caller wrote the allocation call, so it can handle the failure.

## 12. Memory

### 12.1 Allocation is written, never hidden

Only two operations allocate: `new` and `alloc`. No other operation, expression, or built-in type allocates. There are no built-in collections; a list, map, or string builder is a library type that allocates with `new` like any other code. The compiler never inserts an allocation.

### 12.2 `new`, `alloc`, and `free`

```luce
let node = try new Node(value = 1, next = none)      # Node*!: one Node, initialised
let zeroed = try new Node                             # Node*!: the zero value; Node must be zeroable
let leaf = try new Expr.number(value = 2.0)           # Expr*!: an enum case
let items = try new u8[4096]                          # u8[]!: 4096 zeroed bytes
let pool = try alloc Node[64]                         # Node[]!: 64 uninitialised nodes
let raw = try alloc(size, alignment)                  # u8[]!: uninitialised bytes
let scratch = try new u8[size] in arena               # from a named allocator
free(node)
free(items)
free(scratch) in arena
```

- `new` followed by a construction expression, a struct's initialiser call or an enum case, allocates storage for that value, constructs the value in it, and yields `T*`. `new T` with no arguments allocates the zero value and requires `T` to be zeroable (§6.1). `new T[count]` allocates `count` zeroed elements and yields `T[]`; `count` is a `usize` expression. To allocate a single fixed array rather than a span, parenthesise the type: `new (u8[4])` yields `u8[4]*`.
- `alloc T[count]` allocates `count` uninitialised elements of any type and yields `T[]`; reading an element before writing it is undefined (§12.6). `alloc(size, alignment)` yields `u8[]` of uninitialised bytes, the raw form for code that lays memory out itself.
- Every `new` and `alloc` is fallible, because allocation can fail: the type is `T*!` or `T[]!`, and a fallible initialiser adds its own failures to the same result. `try`, `catch`, or `else` handles it as for any fallible call. Failure is `memory.exhausted`.
- `free(x)` returns storage obtained from `new` or `alloc` to its allocator. `x` is a `T*`, a `T[]`, or a `u8[]`. `free` is a statement.
- `new`, `alloc`, and `free` take the current allocator (§12.3) unless followed by `in expression`, which names the allocator: `new Node(...) in arena`, `alloc(n, 16) in parent`, `free(p) in arena`. The expression is a `var` whose type implements `Allocator`, or an `Allocator` view.
- Storage must be freed with the allocator that provided it; §12.3 states how a structure keeps that pairing.

### 12.3 The current allocator and `with`

```luce
import memory

var arena = try Arena.over(memory.allocator, 1 << 20)
defer arena.destroy()
with arena:
    let tokens = try new Token[capacity]     # allocated in the arena
    ...                                      # released all at once by arena.destroy()
```

- `memory.allocator` is a thread-local `var` holding an `Allocator` view, the **current allocator**. `new`, `alloc`, and `free` without `in` use it. `memory.heap` is the process's initial allocator: the C allocator when the C library is linked, the Luce runtime's heap inside a full Luce program (§18.9), and none in a `--freestanding` build.
- `with allocator:` sets `memory.allocator` to `allocator` for the suite and restores the previous value on every exit, including `return` and error propagation. `allocator` is a `var` whose type implements `Allocator`, whose address is taken, or an `Allocator` view. The suite is a scope, so a `defer free(x)` inside it runs before the previous allocator is restored.
- The startup shim sets `memory.allocator` to `memory.heap` before `main`, and `thread.spawn` sets it to `memory.heap` in the new thread (§15.3). In a `--freestanding` build the program assigns `memory.allocator` before its first `new`; a `new` or `alloc` with none set traps `memory.unset`.
- A structure that allocates on behalf of its caller records the allocator it was built with and frees with `free(...) in self.allocator`, so that a caller's `with` block cannot change which allocator releases the structure's memory. Every standard library container does this. The linter reports a `free` without `in` whose argument did not come from a `new` or `alloc` without `in` in the same function.

**Why a current allocator, and why `in` beside it.** Zig passes an `Allocator` to every function that allocates; Odin keeps one in an implicit context passed as a hidden argument. The hidden argument changes the calling convention and would break the plain C ABI that every Base function has; the explicit parameter makes the most common operation in systems code the most verbose one. A thread-local reference keeps the convention plain and keeps `new Node(...)` short for the common case. The `in` form exists because a structure's memory must be released by the allocator that provided it, whatever the caller has current at the time, and a rule that depends on ambient state at every `free` is a rule that programs will get wrong; with `in`, the pairing is written where the structure is defined and the linter can check the rest.

### 12.4 Implementing an allocator

```luce
pub interface Allocator:
    mutating func allocate(size: usize, alignment: usize) -> u8[]?
    mutating func resize(block: u8[], size: usize) -> bool
    mutating func release(block: u8[])
```

A type that implements `Allocator` can be named by `in` and made current with `with`. The requirements are `mutating` because an allocator has state. `allocate` answers uninitialised memory of at least `size` bytes at the given alignment, or `none`. `resize` grows or shrinks a block in place and answers whether it could. `release` returns a block; an allocator that needs the block's alignment or size class recovers it from its own bookkeeping, as `free` in C does, so `free` never has to carry it.

The standard library provides `PageAllocator` (host pages), `CAllocator` (`malloc` and `free`), `FixedBuffer` (over a caller's `u8[]`), and `Arena` (bump allocation over a parent allocator, released all at once by `reset` or `destroy`; §24.3 shows its source). `CAllocator` and `PageAllocator` may be used from several threads at once; `FixedBuffer` and `Arena` may not. In a diagnostic build each of them quarantines released blocks, fills them with a pattern, and records allocation sites.

### 12.5 Ownership conventions

There is no ownership syntax. A pointer parameter is borrowed for the call unless the documentation says the callee takes it; a pointer result is owned by the caller unless the documentation says it is borrowed. Use `defer` and `errdefer` to pair a `free` with its `new`. The linter reports a `new` whose result has no `free`, `defer free`, or `errdefer free` in scope and is not returned or stored.

### 12.6 The safety statement

These two lists are exhaustive for the language. A library may add checks; nothing removes one.

**Defined and checked in every build:** integer overflow in `+`, `-`, `*`, `//`, `%` (trap); shift by the operand width or more (trap); division by zero and `minimum_signed // -1` (trap); indexing and slicing of arrays, spans, and `str` (trap); unwrapping every optional (trap through `else trap`, or a compile error without it); dereference of a bare pointer (cannot be null by type, with the one boundary check of §17.1); reading an uninitialised local (compile error, except after `---`); non-exhaustive `match` (compile error); converting an integer to an integer-backed enum with `T(n)` (trap); checked conversions `T(x)` (trap); float-to-integer casts (saturate); reads and writes through `volatile` (never elided or merged); every atomic operation; aliasing of compatible objects (no type-based aliasing rule, except where `noalias` is written); signed right shift (arithmetic); `<<` past the width (discards).

**Undefined, exactly as in C, and the programmer's responsibility:** use after free and double free; freeing storage with an allocator other than the one that provided it; dereferencing a dangling pointer, including one the escape rule of §6.6 could not see; pointer arithmetic that leaves the object, including `p[i]` out of range; misaligned access after a `(T*)` cast; reading storage declared with `---`, or obtained from `alloc`, before writing it; reading a union member with an invariant after another member was written; modifying a `let` or `const` object through a cast; `(cstr)text` on text that is not NUL-terminated; `(str)bytes` on bytes that are not UTF-8; a `noalias` parameter that aliases; a data race on memory that is not `@T`; a `longjmp` through a Base frame; an `asm` block that violates its declared operands or options; a C callee that retains a lent pointer past the call; a C caller that violates a contract stated in an `extern` declaration.

**Why two lists.** Two exhaustive lists can be checked against the C standard's own catalogue of undefined behaviour, and every future rule has to add itself to one of them.

## 13. Generics

### 13.1 Declarations and constraints

```luce
from luce import Comparable

func first[T](values: const T[]) -> T?:
    if values.length == 0: return none
    return values[0]

struct Pair[A, B]:
    pub let first: A
    pub let second: B

func largest[T: Comparable](left: T, right: T) -> T:
    if left.compare(right) >= 0: return left
    return right
```

Type parameters are declared in square brackets and inferred from argument types; when no argument mentions a parameter, it is written at the call, `decode[Header](bytes)`. A constraint is one or more interfaces joined by `&`. A generic body type-checks from its declaration and written constraints alone; it never accepts syntax that happens to work for one instantiation. Generic code is monomorphised: each instantiation is compiled separately, and the compiler reports every instantiation's origin and size and rejects an infinite chain.

A type argument to a Base generic may be any type a Base module can spell: scalars, pointers, spans, structs, enums, unions, interface views, function types. It may not be a runtime-dependent type of full Luce. Inside Base this is automatic; at the boundary, §18.11 states it.

There are no value parameters (array length is the one built-in exception), no variadic generics, no specialisation, no compile-time code execution, and no associated types; an interface that needs a second type takes it as a parameter, as `Iterable[T, I]` does (§14.4).

**Why monomorphisation and constraints, not `comptime`.** Zig checks a generic body only at instantiation, and its users report that constraints are stated only inside the body and errors are reported at the instantiation, not the declaration. Constrained generics checked at declaration give the C programmer what templates and `void*` were used for, with errors at the declaration. The cost is the closed list of things generics cannot express, which is short and stated.

## 14. Interfaces

### 14.1 Declaration and conformance

```luce
from io import Writer

pub interface Writer:
    mutating func write(bytes: const u8[]) -> usize!

pub struct FileWriter: Writer:
    var descriptor: i32

    pub mutating func write(bytes: const u8[]) -> usize!:
        ...
```

An interface is a nominal set of method requirements. Conformance is declared on the type, `struct Name: Interface:`, with several interfaces separated by commas, never retroactively. Every requirement is supplied with the exact signature; an implementation may be non-fallible where the requirement is fallible. Interfaces do not inherit, have no default bodies, no fields, no constructors, and no generic methods.

### 14.2 Static use

```luce
from io import Writer

func header[W: Writer](writer: W*, header: Header) -> !:
    discard(try writer.write(header.bytes))
```

A constrained generic is statically dispatched and monomorphised. Nothing allocates and nothing goes through a table.

### 14.3 Interface views

```luce
from io import Writer

var file = FileWriter(descriptor = 1)
let writer: Writer = &file
try writer.write(data)
```

Using an interface as a type denotes a two-word value, a pointer to the conforming object and a pointer to its witness table, that borrows the object and does not own it.

- A view is formed from `T*` where `T` implements the interface, or from `const T*` when the interface has no `mutating` requirement. A `mutating` requirement therefore needs the conformer addressable as `var`; mutability is fixed when the view is formed.
- Calls dispatch through the table: one indirect call. The table is static data, emitted once per type-and-interface pair.
- The view is copyable and carries no lifetime; the object must outlive every view of it, the same obligation as for any pointer.
- `Writer?` uses the null niche on the data word.
- There is no boxing, no equality, and no downcast. Code that needs to recover the concrete type should use an enum.

**Why a view.** Full Luce's interface value boxes the concrete value and gives it copy semantics, which needs an owner. Without reference counting, the two-word borrowed form of Rust's `dyn Trait`, Zig's `std.mem.Allocator`, and Go's interface is the only representation that requires no allocation to create, and nominal explicit conformance means every table is known at compile time.

### 14.4 Standard protocols

`Equatable` and `Hashable` are the compiler-known marker interfaces behind `==` and `hash`; they are derived structurally (§7.4) and cannot be implemented by hand. `Comparable` is implemented with `compare(other) -> i64`, negative, zero, or positive; the compiler supplies it for integers, floats (IEEE order, with NaN unordered and `compare` trapping on it), `char`, and `str`.

`Iterable[T, I]` and `Iterator[T]` are what `for` consumes (§8.3): `interface Iterable[T, I: Iterator[T]]: func iterator() -> I`, and `interface Iterator[T]: mutating func next() -> T?`. The iterator type is a parameter, so `for` resolves it statically.

`Display` writes a value to a sink: `func display(sink: Writer) -> !`. The compiler supplies it for integers (decimal), floats (shortest decimal that round-trips, `-5.0` keeps its `.0`, `inf` and `nan` spelled so), `bool`, `char`, `str`, and pointers (hexadecimal with `0x`). Formatted strings call it for each field; `hex(value)`, `bin(value)`, and `pad(value, width)` are one-word functions that answer a value whose `Display` is the requested form.

`Writer` (§14.1) is the standard sink. `io.stdout()` and `io.stderr()` answer one. `print(text)` and `print(f"...")` write to standard output with a newline and ignore a failed write.

**Why `Display` takes a sink.** Full Luce's `display` returns an owned string. In Base there is no owner for it, so the value writes itself to the caller's sink, which is also how C's `snprintf` chain works, minus the format string.

## 15. Atomics, volatile, and threads

### 15.1 Atomic types

```luce
import atomic

var ready: @bool
var hits: @u64
var head: @Node*?

hits += 1                                       # atomic add, seq_cst: the instruction, wrapping as C11 does
ready = true                                    # store, seq_cst
if ready: ...                                   # load, seq_cst
let previous = hits.add(1, .relaxed)
ready.store(true, .release)
while not ready.load(.acquire): ...
let (exchanged, observed) = head.cas(expected, replacement, .acq_rel, .acquire)
atomic.fence(.release)
```

- Plain reads and `=` are sequentially consistent loads and stores.
- `+=`, `-=`, `|=`, `&=`, `^=` on an `@T` are the sequentially consistent read-modify-write instructions, and they wrap as C11's do. Checked arithmetic does not apply to atomics: the `@` on the type is the marker that the instruction is what is wanted.
- The methods, each returning the previous value: `add(v, order)`, `sub(v, order)`, `set(mask, order)` (or), `clear(mask, order)` (and-not), `flip(mask, order)` (xor), `max(v, order)`, `min(v, order)`, `swap(v, order)`; and `load(order)`, `store(v, order)`. `cas(expected, desired, success, failure, weak = false) -> (bool, T)` answers whether it exchanged and the value observed; `failure` may not be `release` or `acq_rel` and may not be stronger than `success`; with `weak = true` it may fail spuriously. `wait(expected)` blocks while the value equals `expected`; `wake(count)` wakes up to `count` waiters, `wake(0)` all. The `Ordering` cases are `relaxed`, `acquire`, `release`, `acq_rel`, `seq_cst`, and an omitted `order` is `seq_cst`. `atomic.fence(order)` is a standalone fence, and `atomic.fence(.signal)` is a compiler-only barrier for signal handlers.
- Every `@T` is lock-free by construction; the double-width form of §5.9 exists only where the target has the instruction.
- Semantics are the C11 memory model.

**Why on the type.** Atomicity is a property of the location. If the marker were on the access, forgetting it once would be a plain access to the same location, which is a data race by definition; with it on the type, every access is atomic and the compiler can prove no mixed access exists. Refusing atomic structs refuses the lock that C11 uses inside `_Atomic struct`.

**Why `+=` is the instruction.** A counter written `hits += 1` must compile to the atomic add; a checked loop would spin under contention where C does not. The type already says the programmer has left checked arithmetic; `+?` remains available for a checked atomic update written as a `cas` loop.

### 15.2 `volatile`

A load or store through `volatile T*` is an observable effect: never elided, merged, reordered with another volatile access or an `asm` block, or widened. It says nothing about other threads. Hardware registers use `volatile`; shared memory uses `@`.

### 15.3 Threads

Base has no thread syntax. Threads are the standard `thread` module over the host's threads: POSIX threads, Windows threads, and WebAssembly workers where the host enables them.

```luce
import thread

struct Job:
    var input: const u8[]
    var checksum: u32

func run(context: void*):
    let job = (Job*)context
    job.checksum = checksum(job.input)

pub func main(arguments: str[]) -> i32!:
    var jobs: Job[4]
    var handles: thread.Handle[4]
    for index in 0..<4:
        jobs[index] = Job(input = part(index), checksum = 0)
        handles[index] = try thread.spawn(run, &jobs[index])
    for handle in handles:
        try handle.join()
    return 0
```

- `thread.spawn(entry: func(void*) -> unit, context: void*, stack: usize = 0, name: str = "") -> thread.Handle!` starts a thread that runs `entry(context)` and ends when it returns; `stack = 0` means the host default. `Handle` is an integer-shaped zeroable value. `handle.join() -> !` waits for the thread and is not `mutating`; `handle.detach()` lets it run unjoined. A `Handle` that is neither joined nor detached when its scope ends is a linter diagnostic; the thread keeps running. `thread.current()`, `thread.pause()` (the CPU spin hint), `thread.yield()`, and `thread.sleep(milliseconds)` exist.
- The memory model is C11's. Two threads that access one non-atomic location, where at least one writes, with no ordering between them, are a data race, and a data race is undefined (§12.6). Ordering is established by `@T` operations, `atomic.fence`, `thread.spawn` (everything before the spawn is visible to the new thread), `join` (everything the thread did is visible after it), and the `sync` module's `Mutex` (`lock`, `unlock`, `try`), `Condition` (`wait(mutex)`, `signal`, `broadcast`), `Once` (`run(function)`), and `Semaphore` (`acquire`, `release`), all zeroable so that `var lock: sync.Mutex` is a valid unlocked mutex, and built over `@T` with `wait` and `wake`.
- `thread_local var` (§6.3) is one variable per thread. `memory.allocator` (§12.3) is thread-local: a new thread starts with `memory.heap`, not the spawning thread's current allocator; a thread that should allocate elsewhere receives the allocator through its context and uses `with` or `in`.
- A trap on any thread stops the whole process, with the trace of the thread that trapped.
- A thread started by Base may not call into full Luce (§18.7). Inside a full Luce program, full Luce's workers are built above this module.

**Why a module and not syntax.** Everything a thread needs from the language is already present: `@T`, `thread_local`, function pointers, and a `void*` for its argument. C's threads are a library, and Base's are too. Full Luce's isolated workers, which copy values and forbid sharing, are the safer design for application code, and they are built on this layer.

## 16. Modules, packages, and tests

### 16.1 Files and modules

One file is one module; its path is its package-relative path: `src/image/color.lucb` is `image.color`. There is no module declaration and no re-export. Module cycles are errors. Declarations are private unless `pub`, and a public signature may mention only public types.

### 16.2 The three module kinds

The **Luce runtime** (§1.3) is the library that implements full Luce's reference-counted classes, its `list`, `map`, and `set` storage, closures, worker transfer, and boxed interface values. A module that uses any of those features is linked against it.

| Kind | Suffix | May use classes, collections, closures | May use raw pointers | Needs the Luce runtime |
| --- | --- | --- | --- | --- |
| Safe Luce | `.luc` | yes | no | yes |
| Native Luce | `.lucn` | yes | yes, through audited intrinsics | yes, unless it uses none of the features above |
| Base | `.lucb` | no | yes, with C spelling | never |

The suffix is the kind. A safe module is ordinary full Luce. A native module is full Luce with one addition, the audited raw-pointer intrinsics. It exists so that a class in full Luce can own a Base or C resource and free it in `deinit`; it is the bridge between the safe tier and Base (§18.6). It is not a lower-level language: it still has classes and collections, and so it still needs the runtime. The runtime itself is written in this tier and is the one native package that needs no runtime, because it uses none of the features it implements. A Base module has no classes, collections, or closures, and so never needs the runtime.

A Base module imports Base modules and C, never a full Luce module. A safe or native module imports any kind. A package whose modules are all Base is a Base package; the sealed runtime package is one, with the single exception that it may contain `.lucn` modules under its own identity for its two sealed intrinsics (§18.13).

**Why the direction is one-way.** Everything a Base module can reach is Base or C, so a Base artifact needs no runtime, and that is checkable: the compiler's reachability analysis reports the first full Luce function a freestanding build touches.

### 16.3 Imports

```luce
import image.color
import data.serialisation as serial
from image.geometry import Point
```

`import` keeps a module qualified, with an optional alias. `from ... import` brings named declarations in and also makes the module visible by its last component, so `from io import Writer` alone lets a program write `io.stdout()`; an `import io` beside it is redundant and is pruned. There are no wildcards and no relative imports. An import nothing resolves through is pruned by the checker, so nothing after it sees the import; a duplicate import is an error with an automatic fix.

### 16.4 Packages

A package has a `luce.toml` manifest and an exact lock. The manifest names the package, its source roots, its dependencies, its C inputs (§17.4), and the `symbol_prefix` for exports (§17.6). The package name is the identity of its error codes (§11.3). There are no build scripts. A minimal manifest and the commands that build a program are at the start of Chapter 24.

### 16.5 Tests

```luce
test "cursor advances by one":
    var cursor = Cursor(data = "ab".bytes, offset = 0)
    try cursor.advance(1)
    assert(cursor.offset == 1)
```

`test` is a declaration, compiled to a hidden `unit!` function and discovered statically; `luce test` runs every test and `luce build` removes them all. A test may use its module's private declarations. Inside a full Luce program tests run under the shared harness. In a Base artifact they run under a freestanding runner with a Base `testing` module providing assertions, deterministic seeds, and a fixed-buffer allocator made current for each test; facilities that need an isolated execution domain are absent, and a trap ends the run after naming the test. A test that writes a module global is not isolated from the others, and the runner reports which globals it wrote.

### 16.6 Standard modules

The language depends on these modules by name. Their full surfaces are in the library reference; what the language needs is stated here. A standard module is used like any other: nothing is visible without an import, `import io` makes the module visible as `io`, and `from io import Writer` brings one of its declarations into scope by its bare name. Only the core types (`str`, `ErrorCode`, the scalars) and the core functions of §3.5 need no import.

| Module | What the language relies on |
| --- | --- |
| `memory` | `allocator` (thread-local current allocator), `heap` (the initial allocator), `exhausted` and `unset` (error codes), `read`, `write`, `copy`, `move`, `set`, `grow` |
| `io` | `stdout()` and `stderr()` as `Writer`s |
| `files` | `read(path: cstr) -> u8[]!` allocating from the current allocator, `write`, `list(path: cstr) -> str[]!`, `missing` (error code) |
| `process` | `run(program: cstr, arguments: cstr[]) -> i32!` |
| `math` | `floor`, `mod`, `sqrt`, the NaN and infinity constants |
| `thread` | `spawn`, `Handle`, `current`, `pause`, `yield`, `sleep` |
| `sync` | `Mutex`, `Condition`, `Once`, `Semaphore` |
| `atomic` | `fence`, `Ordering` |
| `c` | the C types of §5.2, `errno()`, `errno(value)`, `stdin()`, `stdout()`, `stderr()` |
| `testing` | assertions, seeds, and the per-test allocator of §16.5 |
| `runtime` | `heap()` inside a full Luce program (§18.9) |

## 17. Calling C

Base is the layer C bindings are written in. There is no marshalling: a Base pointer is a C pointer, a Base struct is a C struct, `cstr` is `char*`. What full Luce needs three layers for (a foreign declaration, an audited raw module, and a safe wrapper) is one layer in Base, and a safe wrapper for full Luce, when one is wanted, is ordinary Base code behind a `.lucn` module (§18.6).

### 17.1 Declarations

```luce
extern type Window                          # opaque handle; pointer-shaped
extern func SDL_CreateWindow(title: cstr, x: i32, y: i32, w: i32, h: i32, flags: u32) -> Window?
extern func SDL_GetWindowSize(window: Window, out w: i32, out h: i32) -> bool
extern blocking func SDL_Delay(ms: u32)
extern var SDL_version_number: i32
extern func sdl_init as "SDL_Init"(flags: u32) -> i32

extern struct Rect:
    x: i32
    y: i32
    w: i32
    h: i32

extern union Event:
    kind: u32
    key: KeyEvent
    padding: u8[56]
```

- `extern func` binds a C function. `out` parameters become extra results, received as a tuple after the declared return. `blocking` marks a call that may park the thread; in a Base artifact it is informational, and inside a full Luce program it is the contract full Luce's workers rely on. `as "name"` binds a C symbol under a Base-style name.
- `extern type` declares an opaque pointer-shaped handle; bare it is never null, `?` is the null niche. `extern type Handle = u32` declares an integer-shaped one.
- `extern struct` and `extern union` declare C layout and may carry `packed` or `align(N)`. Fields may be any C-representable type (§17.6): `name: c.char[32]`, `next: Node*?`, `callback: func(void*) -> unit`. An `extern struct` may be passed and returned by value, because the backend performs the target's aggregate classification.
- `extern var` binds a C global of scalar or pointer type; reads and writes are direct.
- Pointers and function pointers in signatures are written as Base pointers and `func` types. `str` is not admitted in an `extern` signature; C text is `cstr`, and `str(value)` validates it.

**The one boundary check.** A bare pointer, function, or handle slot in an `extern` signature, or in an exported function's signature, is a contract the C side may violate. At every such crossing the compiler inserts one comparison: a zero arriving in a bare slot, in either direction, traps `null_foreign`. Declare the slot `?` when null is legitimate, and no check is emitted. This comparison is the whole of what Base does at the boundary.

**Why one check and not none.** Without it, `extern func malloc(size: usize) -> void*` declared without `?` would give the program a null pointer with a non-null type, and the never-null rule would not hold at the place where C code enters. One comparison per crossing is the cost of making the rule hold.

### 17.2 Variadic calls

`extern func printf(format: cstr, ...) -> i32` declares a variadic function. The argument types in a variadic position:

| Argument | Passed as |
| --- | --- |
| an untyped integer literal | `i32`; an error if it does not fit |
| an untyped float literal | `f64` |
| `bool`, `char`, `i8`, `u8`, `i16`, `u16`, `c.char` | promoted to `i32` |
| `f32` | promoted to `f64` |
| an integer-backed enum | its representation, promoted as above |
| a string literal | `cstr` |
| any other integer, float, or pointer | as itself |
| `str`, a span, a struct, a union, an optional | rejected |

The linter asks for an explicit `c.long` or `c.char` when a value of one of the distinct `c` types is intended and a Base-width integer was written. Base functions cannot be declared variadic in this revision.

**Why the literal rule.** `printf("%d", 5)` must pass an `int`. In a variadic position there is no parameter type for a literal to adapt to, so the default would have been `i64`, which happens to work on 64-bit targets because every variadic slot is eight bytes, and breaks on wasm32 and every ILP32 target. Naming C's own default type for the literal is the only rule under which `printf("%d", 5)` is correct on every target.

### 17.3 Using the results

```luce
let window = SDL_CreateWindow("Luce", 100, 100, 800, 600, 0) else error(no_window, "no window")
let (ok, width, height) = SDL_GetWindowSize(window)
```

Nothing is decoded. A handle is a pointer; a nullable result is unwrapped with the ordinary optional forms; an `out` parameter is a tuple component.

### 17.4 C sources and libraries

```toml
[native]
sources = ["vendor/stb_image.c", "shims.c"]
libraries = ["sqlite3", "m"]
link_search = ["/opt/homebrew/lib"]
frameworks = ["Metal"]
pkg_config = ["sdl2"]
```

`sources` are compiled with the host C compiler the build already uses for assembly and linking, and linked into the artifact. `libraries`, `link_search`, `frameworks`, and `pkg_config` are passed to the linker. There is no inline C inside a `.lucb` file: the formatter, the language server, and the test runner would each need a C parser, and a sidecar `.c` file gives the same power with the tooling intact.

### 17.5 `luce bind`

`luce bind header.h` generates a `.lucb` declaration module from a C header and reports what it could not map. Its parser is a Luce-owned C declaration parser; Clang may validate its output but is not a dependency of the compiler. The **recipe** is a file beside the header that states what the header does not: which pointer parameters may be null, which results are owned, which macros to wrap and with what signatures.

- Functions, structs, unions, enums, typedefs, pointers, arrays, function pointers, constant `#define`s, `const`, `_Noreturn`, and `static inline` functions (compiled into `shims.c`) map directly.
- A C enum becomes an integer-backed enum over the compiler's compatible type.
- An array parameter `int a[4]` becomes `a: i32*`, because C adjusts it to a pointer.
- A flexible array member `T data[];` is omitted from the struct, and a method `data() -> T*` is generated from `sizeof` of the fixed part; the recipe may name the count field to produce a span instead. A struct declared in Base with a trailing payload uses `alloc` with a computed size and the same accessor pattern.
- Anonymous struct and union members are flattened into the enclosing type with generated names. A struct with bit-fields is refused unless the recipe names accessor shims for its fields.
- `errno` becomes `c.errno()` and `c.errno(value)`; `stdin`, `stdout`, and `stderr` become `c.stdin()` and the other two, because all are macros over thread-local or platform accessors.
- Function-like macros, `_Generic`, `va_list`-taking definitions, `long double`, `_Complex`, attributes beyond `noreturn`, `_BitInt`, `typeof`, `#embed`, and compiler extensions are mapped only through the recipe, for which the tool emits `static inline` wrappers into `shims.c`.

Generated files are ordinary source, checked in and reviewed.

**Why not parse headers in the compiler.** Zig moved `@cImport` out of the language into the build step and rewrote its translator off libclang; Odin and Kotlin/Native use generators. Every one of them fails on function-like macros, so `luce bind` requires a recipe for them rather than guessing.

### 17.6 Export

```luce
export func blend(left: Pixel, right: Pixel) -> Pixel: ...

struct Cursor:
    pub var offset: usize

    export mutating func advance(count: usize) -> !: ...
```

Export is opt-in. `export func name(...)` gives a function C linkage under `name`, or under the manifest's `symbol_prefix` followed by `name`. `export` on a method exports it as `Type_method` with `self` first. A `pub` function that is not exported has hidden visibility and a module-qualified symbol and cannot collide with C. An `export` is a compile error when the signature is not C-representable or when two exports share a symbol.

C-representable means: scalars, `bool`, `usize`/`isize`, `c.*` types, pointers, `cstr`, spans in parameter position (a pointer and a `usize` length, in that order; the wrapper normalises `(NULL, 0)` to the empty span), structs and unions of C-representable fields, integer-backed enums, and function pointers with C-representable signatures. Not representable: `T[N]` in a signature (C has no by-value array parameters), spans in result, field, or function-pointer positions, `str`, tagged optionals, interface views, `fmt`, and every generic. A fallible function exports in a status form: the C function returns an `int` status and writes the value through a final out-pointer.

The generated header spells:

- a struct as its C definition, with `__attribute__((packed))` or `__attribute__((aligned(N)))` where declared;
- `bool` as C's `bool`, `usize` as `size_t`, `isize` as `ptrdiff_t`;
- `T*?` as the same pointer type with a comment that it may be null;
- an integer-backed enum as `typedef uint32_t Flags;` with `enum { Flags_empty = 0, ... };` for C17, plus `enum Flags : uint32_t` under C23, because a C17 enum's compatible type is implementation-defined.

`luce build --lib` produces a static or shared library plus the header for the package's exported surface.

**Why opt-in.** Making every `pub` function a global C symbol would make two modules' `pub func init` a link error and a `pub func read` an interposition of libc for the whole process. Export is a linkage decision, and it is written explicitly.

## 18. Working with full Luce

This chapter is the contract between a full Luce program and the Base modules it imports. It names full Luce types; a reader who does not use full Luce may skip it. Full Luce's own definitions are in its specification; the one-line glosses here are enough to read the tables.

### 18.1 One program, two representations

A full Luce program that imports a Base package compiles both into one intermediate representation. A call from full Luce into Base whose signature uses only shared types (§18.2) or plain types (§18.3) is an ordinary call: no thunk, no marshalling. A call whose signature involves a type with two representations, `str`, `Error` (and so every `T!`), spans, or interface views, goes through an adapter the compiler generates at the call site, which lends or copies as §18.4 and §18.5 state. Every adapter is reported by `luce build --costs`, the cost report.

### 18.2 Shared types

| Base | Full Luce | Gloss | Where full Luce may use it |
| --- | --- | --- | --- |
| `T*` | `native_mut_ptr[T]` | full Luce's raw pointer | `.lucn` modules only |
| `const T*` | `native_ptr[T]` | raw read-only pointer | `.lucn` modules only |
| `void*`, `const void*` | `foreign` | untyped raw pointer | `.lucn` modules and `extern` signatures |
| `T*?` | `native_mut_ptr[T]?` | nullable raw pointer; the null niche in both | `.lucn` modules |
| `func(A) -> R` | `cfunc(A) -> R` | C function pointer | anywhere `cfunc` is admitted |
| `T[N]` | `array[T, N]` | fixed array | anywhere |
| integer-backed `enum` | `export c enum` | enum with a fixed integer representation | anywhere |

Types with two representations, crossed only through adapters: `str` (a view here, an owned reference-counted string there); `Error`; `T[]` against `slice[T]` (an owner-retaining view) and `list[T]` (a growable collection); interface views against interface values (boxed). `usize` and `isize` convert (§18.4). Types that never cross: `cstr`, `union`, `@T`, `volatile T*`, `fmt`, `thread_local` globals.

### 18.3 Plain types

A type is **plain** when its representation is copied data with no reference identity and no pointer: scalars, `bool`, `char`; enums, tuples, arrays, structs, and optionals of plain types; function pointers with plain signatures. `usize`, pointers, spans, `str`, `cstr`, unions, `@T`, interface views, and every runtime-dependent full Luce type are not plain. `Plain` is a compiler-derived marker constraint, derived structurally with the same machinery as full Luce's check that a value may be sent to a worker, and the compiler names the field that fails.

### 18.4 Full Luce calls Base: parameters

| Base parameter | Full Luce argument | Crossing |
| --- | --- | --- |
| plain `T` | plain `T` | by value |
| `usize` / `isize` | `u64` / `i64` | by value; a checked narrowing that traps on a 32-bit target |
| `const T[]`, `T` plain | `slice[T]`, `list[T]`, `array[T, N]`, `bytes` | **lent**: the existing dense storage is viewed, nothing is copied |
| `T[]`, `T` plain | `list[T]` | lent under the list's mutation guard: structural change through any alias traps until the call returns, and element storage does not move |
| `str` | `str` | lent: the owned string's bytes are viewed |
| `cstr` | `str` | lent as a NUL-terminated temporary |
| interface view | a conforming value, class instance, or interface value | lent: the payload's address and the same static witness table; for a `mutating` requirement the argument must be a `var` or a class instance |
| `func(...) -> R`, plain signature | a capture-free function or lambda | by value |
| `T*`, `const T*`, `void*`, `T*?` | the native pointer from a `.lucn` module | by value; a safe module cannot produce one |
| `union`, `@T`, `volatile T*`, `fmt` | nothing | rejected |

"Lent" means the callee may read (or, for `T[]`, write elements of) the storage until it returns and may not retain the pointer. Retaining it is undefined by contract. The compiler cannot check the callee; it reports the crossing.

### 18.5 Base returns to full Luce: results

| Base result | Full Luce receives | Crossing |
| --- | --- | --- |
| plain `T` | `T` | by value |
| `usize` / `isize` | `u64` / `i64` | exact widening |
| `str` | owned `str` | **copied** into a fresh owned string by the adapter; the allocation is reported |
| `T!` | `T!` | `T` by the rules above; on failure the adapter builds a full Luce `Error` with the same code and a copied message |
| `T*`, `const T*`, `void*`, `T*?`, `func` | the native type | only into a `.lucn` module |
| `T[]`, `cstr`, `union`, `@T`, interface view | nothing | rejected: no owner for a view |

**The rule in both tables is the same: owned values are lent into Base; views are copied out of Base.** Nothing crosses that could dangle in safe code.

### 18.6 Wrapping a Base resource for safe Luce

A Base type that owns memory (an arena, a parser, a device) is exposed to safe Luce the way a C library is: a `.lucn` module holds the Base pointer in a final class whose `deinit`, the method full Luce runs when the last reference is released, calls the Base destroy function, and whose methods lend and copy by the tables above. Base does not need to know it is wrapped.

### 18.7 Base calls full Luce

A Base module cannot name a full Luce declaration. It reaches full Luce only through function pointers it was handed, and only inside a program that contains the runtime. A full Luce module converts a capture-free function with a plain signature to a Base `func` and passes it down. State crosses as a `void*` context. A `.lucn` module obtains one from a class reference with `native.retain(object) -> foreign`, which increments the reference count and hands out the address; reads it back during a callback with `native.borrow[T](handle) -> T`, a reference valid for the callback; and ends the ownership with `native.release[T](handle) -> T`. These three intrinsics are sealed to `.lucn` modules. The callback runs synchronously on the thread that entered Base. A thread Luce never entered may not call full Luce; the runtime stops the process when it detects one. A Base artifact built without the runtime cannot reach full Luce at all, and reachability analysis reports the first full Luce function reached with its call path.

### 18.8 Globals

A `pub var` in a Base module is not accessible from full Luce, which has no mutable globals; Base exposes accessor functions. A `pub let` is accessible when plain.

### 18.9 Allocation

Inside a full Luce program, `memory.heap` is the runtime's heap, also available as `runtime.heap()`, and it is the current allocator for Base code called from full Luce. Memory Base allocates from it belongs to Base structures; full Luce sees those only through §18.6 wrappers. Memory owned by full Luce is never freed by Base.

### 18.10 Errors and traps

`ErrorCode` is one type in both tiers. `Error` is two types with one meaning, converted by the `T!` adapter. A trap in Base inside a full Luce program is full Luce's trap, with the same reporter and trace. A trap in a freestanding Base artifact is reported by the Base trap reporter and ends the process.

### 18.11 Generics and interfaces across the boundary

A full Luce generic instantiated with a plain Base type is ordinary. A Base generic called from full Luce is instantiated with a type argument that has a Base spelling or is plain, and with nothing else; a Base `first[T](items: const T[])` called with a `list[Point]` instantiates `T = Point` and lends the list as the span. A full Luce generic cannot be instantiated inside Base. An interface declared in either tier may be implemented in either when every requirement signature uses plain or shared types; static dispatch crosses free, and the witness table a Base view dispatches through is the same static table full Luce's boxed values use. `Display` and `Writer` are the exceptions: their Base signatures are not plain, so a full Luce conformer or sink reaches Base through a `.lucn` adapter.

### 18.12 Why the contract is shaped this way

Two properties were required: Base must need nothing from the runtime, and safe Luce must never receive a value that can dangle. Everything else follows. Lending owned values into Base requires no copy because the runtime already stores every plain element type densely at its C width. Copying views out of Base costs an allocation, which is reported, because the alternative is a view into storage nobody owns. The one place the two tiers differ in representation, `str` and `Error`, is handled by adapters rather than by treating the representations as the same.

### 18.13 The runtime

The runtime that implements full Luce's classes and collections becomes a Base package. The two intrinsics only it may use, the arena provider that asks the backend for committed memory and the storage-service binding through which generated code reaches the allocator, stay sealed to `.lucn` modules under its package identity; everything else it does, it does in ordinary Base. The port needs only pointers, spans, globals, and unions, which are the first Base slices to be implemented, and it is the first real Base program: it must compile freestanding, produce the machine code the current tier produces, and pass the differential corpus that already checks every runtime behaviour through three executions. Exposing the runtime's heap to other Base code as `memory.heap` needs interface views (§14.3) and comes after.

## 19. Compilation and runtime

### 19.1 Pipeline

Source is tokenised, laid out, parsed, resolved, and typed into the same typed intermediate representation as full Luce, then lowered to the canonical machine representation every backend consumes. A Base module is checked by the same passes with the Base profile: the grammar additions of Chapter 21 are admitted, runtime-dependent constructs are rejected with a diagnostic naming the tier they belong to, and the freestanding property is checked by reachability.

### 19.2 What Base adds to the shared representation

All target-neutral:

- a pointer-width integer type, and symbolic layout constants for `sizeof`, `alignof`, and `offsetof` that the backend folds;
- a nullable pointer type, the one recorded exception to full Luce's uniform tagged optional, justified by the C layout of struct fields, and shared with native Luce's raw pointers;
- a two-word unmanaged interface-view type with a witness-table-address instruction;
- a union type;
- a memory-zeroing instruction;
- pointer difference, pointer-integer conversion, and pointer ordering;
- a `volatile` flag on loads and stores, and a `noalias` fact on parameters;
- atomic load, store, read-modify-write, compare-and-swap, wait, wake, and fence, each with an ordering;
- an `asm` region carrying every architecture variant, and a `naked` function form;
- a variadic call form;
- the attribute facts of §9.8.

The verifier checks each. `new`, `alloc`, and `free` lower to calls through the `memory` module's thread-local view and add nothing to the representation.

### 19.3 Backends and bridges

The stage-1 backend is QBE, and every Base construct is planned to compile through it. Where QBE lacks a native form, the backend bridges, at a stated cost:

- **Atomics** lower to calls of the size-suffixed `__atomic_*` library functions (`__atomic_load_4`, `__atomic_fetch_add_8`, `__atomic_compare_exchange_4`, with C's memory-order encoding), which compiler-rt supplies on macOS and libatomic supplies on Linux, where the driver adds `-latomic`. A call is an opaque barrier to QBE, so ordering is preserved. `weak = true` lowers to the strong call. `wait` and `wake` are the host's futex or equivalent. One call per operation.
- **Fences** have no library function; they lower to a one-instruction out-of-line assembly function per target (`mfence` or `lock addl $0, (%rsp)` on x86-64, `dmb ish` on ARM64).
- **`volatile`** lowers to the relaxed `__atomic_load_N` and `__atomic_store_N` calls, because QBE's load optimiser forwards stores to loads and removes repeated loads of one address. Widths 1, 2, 4, and 8.
- **`asm`** blocks are emitted as out-of-line assembly functions with the C calling convention. Named-register operands are moved into their registers by a generated prologue and out by a generated epilogue; `reg` operands use argument registers; registers named as outputs or destroyed are saved and restored if the convention requires. A block cannot observe the caller's frame or flags; `naked` functions and `nostack` blocks are honoured as written, because they are whole functions. One call per block.
- **Variadic calls** and **by-value aggregates** use QBE's native support.
- **`noalias`**, `inline`, `noinline`, and `cold` are carried but have no effect on QBE, which has no inliner and no alias analysis; §1.2's cost claim holds on the native backends.
- **WebAssembly** supports atomics where the host enables threads, rejects `asm`, and passes variadic arguments through a shadow-stack buffer as Clang does.

The Luce-owned native backends scheduled after stage 1 implement all of these natively with no source change.

### 19.4 Artifacts and build profiles

A Base executable links a startup shim and a trap reporter and no Luce runtime (§1.3). By default the shim uses the host C library for process start, output, and exit; `--freestanding` drops it and the program supplies `_start` through a `naked func` or a module-level `asm` block. There are two build profiles: `default`, and `diagnostic`, selected with `luce build --profile diagnostic`. No profile changes overflow, bounds, evaluation order, or error behaviour. The diagnostic profile additionally fills `---` storage, quarantines released blocks in the standard allocators, and records allocation sites.

### 19.5 Targets

| `--target` | `asm` name | Pointer width | `c.long` | `c.char` | Calling convention |
| --- | --- | --- | --- | --- | --- |
| `x86_64-linux` | `x86_64` | 64 | 64 | signed | SysV |
| `x86_64-macos` | `x86_64` | 64 | 64 | signed | SysV |
| `x86_64-windows` | `x86_64` | 64 | 32 | signed | Windows x64 |
| `arm64-linux` | `arm64` | 64 | 64 | unsigned | AAPCS64 |
| `arm64-macos` | `arm64` | 64 | 64 | signed | Apple arm64 |
| `wasm32` | none | 32 | 32 | signed | WebAssembly C ABI |

### 19.6 Tooling

`luce fmt`, `luce check`, `luce build`, `luce test`, and `luce bind` apply to Base modules. `-W` on `check`, `build`, or `test` prints the checker's warnings: an unused local (a name beginning with `_` is exempt), an unused import, a private function nothing references, a statement no path reaches, and a branch or loop whose literal condition rules it out. Each is also pruned from the program by the checker, so nothing after the checker sees it; an unused binding whose initialiser may have an effect stays as that expression. `luce build --lib` produces a library and header. `luce build --freestanding` drops the shim. `luce build --costs` prints the adapter and allocation report of §18.1. `luce build --target` with no argument lists the targets above and the `asm` architectures a package covers.

## 20. Exclusions

Absent from Base, each with the reason it is not a loss:

- **The preprocessor, conditional compilation, macros.** Generics, constants, per-target modules, `asm ARCH`, `fmt` parameters, `luce.location`, and `luce bind` cover every use.
- **Implicit narrowing, signedness change, and integer-float conversion.** These change values; the one implicit conversion, same-signedness widening, cannot.
- **`NULL` and nullable-by-default pointers.** `T*?`.
- **`->`.** Auto-dereference on `.`.
- **Braces, semicolons, `&&`, `||`, `!`, `++`, `--`, the comma operator, statement expressions, `do while`, `switch` fallthrough.** One parser, one formatter, one spelling per idea.
- **`goto`.** Reserved (§8.6).
- **`setjmp`/`longjmp`.** ISO C, but incompatible with `defer`, `errdefer`, and initialisation analysis. A `longjmp` through a Base frame is undefined. A library that reports errors through it (libpng, libjpeg, the Lua C API) is bound through a `shims.c` function that contains the `setjmp` and returns a status.
- **Computed goto, VLAs, `alloca`, `register`, `long double`, `_Complex`.** GNU extensions, not representable by the backends, or not portable.
- **Variadic function definitions, bit-field widths in Base structs, designated array initialisers, a storable result type.** Deferred until enough exported and C-layout code exists to test the rules against; a storable result is an enum with two cases today.
- **Classes, reference counting, closures, built-in collections, owned strings, workers.** The runtime. Use full Luce.

## 21. Grammar

Repetition is `{...}`, optional syntax is `[...]`, quoted text is a token. `NEWLINE`, `INDENT`, and `DEDENT` come from the layout lexer; `RAW_LINE` is a physical line captured without tokenisation after removal of the suite's indentation baseline. `IDENT` is an identifier that is not a reserved word; `TYPE_IDENT` is a `PascalCase` identifier; `TYPE_PATH` is a `TYPE_IDENT` optionally qualified by a module path; `CORE_TYPE` is one of the scalar type names, `str`, `cstr`, `fmt`, `unit`, `never`; `COMPARE_OP` is `==`, `!=`, `<`, `<=`, `>`, `>=`; `FORMAT_START`, `FORMAT_TEXT`, `FORMAT_END` are the lexer's pieces of one `f"..."` literal; `constant_expression` is an `expression` meeting §6.4. Semantic restrictions in the earlier chapters remain normative over this shape.

```ebnf
module          = { import_decl }, { top_decl }, EOF ;

import_decl     = "import", module_path, [ "as", IDENT ], NEWLINE
                | "from", module_path, "import", IDENT, { ",", IDENT }, NEWLINE ;
module_path     = IDENT, { ".", IDENT } ;

top_decl        = [ "pub" ], ( constant_decl | global_decl | type_alias
                             | function_decl | struct_decl | enum_decl
                             | union_decl | interface_decl | extern_decl )
                | "export", function_decl
                | test_decl
                | "assert", argument_list, NEWLINE
                | asm_module_decl ;

constant_decl   = "let", IDENT, [ ":", type ], "=", constant_expression, NEWLINE ;
global_decl     = [ "thread_local" ], { attribute }, "var", IDENT, ":", type,
                  [ "=", constant_expression ], NEWLINE ;
type_alias      = "type", TYPE_IDENT, "=", type, NEWLINE ;
attribute       = "inline" | "noinline" | "cold" | "naked" | "weak" | "used"
                | "section", "(", STRING_LITERAL, ")" ;

function_decl   = { attribute }, [ "static" ], [ "mutating" ], "func", IDENT,
                  [ generic_params ], parameter_list, result_clause, ":", suite ;
function_sig    = [ "mutating" ], "func", IDENT, [ generic_params ],
                  parameter_list, result_clause, NEWLINE ;
generic_params  = "[", generic_param, { ",", generic_param }, [ "," ], "]" ;
generic_param   = TYPE_IDENT, [ ":", interface_type, { "&", interface_type } ] ;
parameter_list  = "(", [ parameter, { ",", parameter }, [ "," ] ], ")" ;
parameter       = IDENT, ":", [ "noalias" ], type, [ "=", constant_expression ] ;
result_clause   = [ "->", ( type | "!" ) ] ;

conformance     = [ ":", interface_type, { ",", interface_type } ] ;
struct_decl     = [ "packed" | "align", "(", constant_expression, ")" ],
                  "struct", TYPE_IDENT, [ generic_params ], conformance, ":",
                  NEWLINE, INDENT, type_member, { type_member }, DEDENT ;
type_member     = [ "pub" ], ( field_decl | [ "export" ], function_decl ) ;
field_decl      = [ "align", "(", constant_expression, ")" ], ( "let" | "var" ), IDENT, ":", type,
                  [ "=", constant_expression ], NEWLINE ;

enum_decl       = "enum", TYPE_IDENT, [ generic_params ], [ "as", integer_type ],
                  conformance, ":", NEWLINE, INDENT,
                  enum_case, { enum_case }, { [ "pub" ], function_decl }, DEDENT ;
enum_case       = IDENT, [ payload_list | "=", constant_expression ], NEWLINE ;
payload_list    = "(", payload, { ",", payload }, [ "," ], ")" ;
payload         = IDENT, ":", type ;

union_decl      = "union", TYPE_IDENT, ":", NEWLINE, INDENT,
                  union_member, { union_member }, { [ "pub" ], function_decl }, DEDENT ;
union_member    = IDENT, ":", type, NEWLINE ;

interface_decl  = "interface", TYPE_IDENT, [ generic_params ], ":", NEWLINE,
                  INDENT, function_sig, { function_sig }, DEDENT ;

test_decl       = "test", STRING_LITERAL, ":", suite ;
asm_module_decl = "asm", IDENT, ":", NEWLINE, INDENT, RAW_LINE, { RAW_LINE }, DEDENT ;

extern_decl     = "extern", ( extern_type | extern_func | extern_var
                            | extern_struct | extern_union ) ;
extern_type     = "type", TYPE_IDENT, [ "=", ( "u32" | "i32" | "u64" | "i64" ) ], NEWLINE ;
extern_func     = [ "blocking" ], "func", IDENT, [ "as", STRING_LITERAL ],
                  "(", [ extern_parameter, { ",", extern_parameter } ], [ ",", "..." ], ")",
                  result_clause, NEWLINE ;
extern_parameter
                = [ "out" ], IDENT, ":", type ;
extern_var      = "var", IDENT, ":", type, NEWLINE ;
extern_struct   = [ "packed" | "align", "(", constant_expression, ")" ], "struct", TYPE_IDENT, ":",
                  NEWLINE, INDENT, union_member, { union_member }, DEDENT ;
extern_union    = "union", TYPE_IDENT, ":", NEWLINE, INDENT, union_member, { union_member }, DEDENT ;

suite           = simple_stmt
                | NEWLINE, INDENT, statement, { statement }, DEDENT ;
statement       = simple_stmt | if_stmt | while_stmt | for_stmt | match_stmt
                | labeled_loop | with_stmt | asm_stmt ;
simple_stmt     = binding_stmt | assignment_stmt
                | "break", [ IDENT ], NEWLINE
                | "continue", [ IDENT ], NEWLINE
                | "return", [ expression ], NEWLINE
                | "defer", deferred_call, NEWLINE
                | "errdefer", deferred_call, NEWLINE
                | "recover", expression, NEWLINE
                | "free", "(", expression, ")", [ "in", expression ], NEWLINE
                | expression, NEWLINE ;
deferred_call   = call_expression, [ "catch", IDENT, ":", suite ] ;

binding_stmt    = "let", binding_pattern, [ ":", type ], "=", expression, NEWLINE
                | "var", binding_pattern, ":", type, [ "=", ( expression | "---" ) ], NEWLINE
                | "var", binding_pattern, "=", expression, NEWLINE ;
binding_pattern = IDENT | "(", IDENT, ",", IDENT, { ",", IDENT }, [ "," ], ")" ;

assignment_stmt = lvalue, ASSIGN_OP, expression, NEWLINE ;
lvalue          = ( IDENT | "self" ), { lvalue_part }
                | "*", unary_expr
                | "(", lvalue, ")", { lvalue_part } ;
lvalue_part     = ".", IDENT | "[", expression, "]" ;
ASSIGN_OP       = "=" | "+=" | "-=" | "*=" | "/=" | "//=" | "%=" | "+%=" | "-%=" | "*%="
                | "+|=" | "-|=" | "*|=" | "&=" | "|=" | "^=" | "<<=" | ">>=" ;

if_stmt         = "if", condition, ":", suite, { "elif", condition, ":", suite },
                  [ "else", ":", suite ] ;
condition       = expression | "let", IDENT, "=", expression ;
while_stmt      = "while", condition, ":", suite ;
for_stmt        = "for", ( IDENT, [ ":", type ] | "(", IDENT, ",", IDENT, ")" ), "in",
                  [ "&" ], expression, ":", suite ;
labeled_loop    = IDENT, ":", ( while_stmt | for_stmt ) ;
with_stmt       = "with", expression, ":", suite ;

match_stmt      = "match", expression, ":", NEWLINE, INDENT, match_arm, { match_arm }, DEDENT ;
match_arm       = pattern, { ",", pattern }, [ "if", expression ], ":", suite ;
pattern         = "_" | literal_pattern | case_pattern ;
literal_pattern = pattern_literal, [ ( "..<" | "..=" ), pattern_literal ] ;
pattern_literal = literal | "-", ( INTEGER_LITERAL | FLOAT_LITERAL ) ;
case_pattern    = ".", ( IDENT | "none" | "some" ), [ "(", [ ( IDENT | "_" ), { ",", ( IDENT | "_" ) } ], ")" ] ;

asm_stmt        = "asm", IDENT, [ "(", asm_operand, { ",", asm_operand }, ")" ],
                  ":", NEWLINE, INDENT, RAW_LINE, { RAW_LINE }, DEDENT ;
asm_operand     = "in", "(", asm_place, ")", expression
                | ( "out" | "inout" ), "(", asm_place, ")", ( lvalue | "_" )
                | "options", "(", IDENT, { ",", IDENT }, ")" ;
asm_place       = STRING_LITERAL | "reg" ;

expression      = else_expr, [ "catch", IDENT, ":", suite ] ;
else_expr       = conditional_expr, [ "else", else_expr ] ;
conditional_expr
                = or_expr, [ "if", or_expr, "else", conditional_expr ] ;
or_expr         = and_expr, { "or", and_expr } ;
and_expr        = comparison_expr, { "and", comparison_expr } ;
comparison_expr = range_expr, [ COMPARE_OP, range_expr ] ;
range_expr      = bit_or_expr, [ ( "..<" | "..=" ), bit_or_expr ] ;
bit_or_expr     = bit_xor_expr, { "|", bit_xor_expr } ;
bit_xor_expr    = bit_and_expr, { "^", bit_and_expr } ;
bit_and_expr    = shift_expr, { "&", shift_expr } ;
shift_expr      = additive_expr, { ( "<<" | ">>" ), additive_expr } ;
additive_expr   = multiply_expr, { ( "+" | "-" | "+%" | "-%" | "+|" | "-|" | "+?" | "-?" ), multiply_expr } ;
multiply_expr   = unary_expr, { ( "*" | "/" | "//" | "%" | "*%" | "*|" | "*?" ), unary_expr } ;
unary_expr      = { "try" | "not" | "+" | "-" | "-%" | "~" | "*" | "&" | cast_prefix }, postfix_expr ;
cast_prefix     = "(", type, ")" ;        (* only when the parenthesised text is cast-shaped, §7.5 *)

postfix_expr    = primary_expr, { ".", IDENT | argument_list | type_arguments, argument_list
                                | "[", index_or_slice, "]" } ;
call_expression = postfix_expr ;
argument_list   = "(", [ argument, { ",", argument }, [ "," ] ], ")" ;
argument        = [ IDENT, "=" ], expression ;
index_or_slice  = expression | expression, "..<", expression | "..<", expression | expression, ".." ;

primary_expr    = literal | IDENT | "self" | ".", IDENT, [ argument_list ]
                | "(", expression, [ ",", expression, { ",", expression }, [ "," ] ], ")"
                | "(", ")"
                | "[", [ expression, { ",", expression }, [ "," ] ], "]"
                | lambda_expr | match_expr | span_constructor | new_expr | alloc_expr ;
lambda_expr     = "(", [ IDENT, [ ":", type ], { ",", IDENT, [ ":", type ] } ], ")", "=>", expression ;
match_expr      = "match", expression, ":", NEWLINE, INDENT,
                  match_value_arm, { match_value_arm }, DEDENT ;
match_value_arm = pattern, { ",", pattern }, [ "if", expression ], "=>", expression, NEWLINE ;
span_constructor
                = type, "[", "]", argument_list ;          (* u8[](pointer, count) *)
new_expr        = "new", new_target, [ "in", expression ] ;
new_target      = type_core, { "*" | "?" }, ( "[", expression, "]" | argument_list | ".", IDENT, argument_list | ) ;
alloc_expr      = "alloc", ( type_core, { "*" | "?" }, "[", expression, "]" | argument_list ), [ "in", expression ] ;

type            = [ "@" ], [ "const" ], [ "volatile" ], type_core,
                  { "*", [ "?" ] | "[", constant_expression, "]" | "[", "]" }, [ "?" ], [ "!" ] ;
type_core       = type_name, [ type_arguments ]
                | "func", "(", [ type, { ",", type } ], ")", "->", type
                | "void"
                | "(", type, ")"
                | "(", type, ",", type, { ",", type }, [ "," ], ")" ;
type_name       = TYPE_PATH | CORE_TYPE ;
type_arguments  = "[", type, { ",", type }, [ "," ], "]" ;
interface_type  = type_name, [ type_arguments ] ;
integer_type    = "u8" | "u16" | "u32" | "u64" | "i8" | "i16" | "i32" | "i64" | "usize" | "isize" ;
literal         = INTEGER_LITERAL | FLOAT_LITERAL | CHAR_LITERAL | STRING_LITERAL | BYTES_LITERAL
                | formatted_string | "true" | "false" | "none" ;
formatted_string
                = FORMAT_START, { FORMAT_TEXT | "{", expression, "}" }, FORMAT_END ;
```

Notes on the shape. Qualifiers bind to the innermost `type_core`, so `const (T*)*` is C's `T *const *` and `void` must be followed by at least one `*`. A `?` may follow any `*` and the final suffix; `??` is rejected semantically. A `type_arguments` bracket contains types; a bracket after a complete type containing a constant expression or nothing is an array or span suffix. `*%` and the other operator forms are recognised only between two operands, so they cannot be confused with a dereference. The tokens `@`, `---`, `...`, and the wrapping, saturating, and checked operators are matched longest-first and admitted by the parser in Base modules only, so the formatter stays single. A statement beginning `IDENT ":"` is a labeled loop and nothing else, because no other statement begins with a bare identifier and a colon. `new T[n]` is a span allocation; `new (T[4])` is a pointer to an array.

## 22. C to Base

```text
int *p = &x;                 let p: i32* = &x
const T *p                   p: const T*
T *const *pp                 pp: const (T*)*
volatile uint32_t *reg       reg: volatile u32*
void *ctx                    ctx: void*
p->f                         p.f
*p = v                       *p = v
(T *)q                       (T*)q
(uint8_t)x                   (u8)x           # u8(x) is the checked form
(int)f                       (i32)f          # saturating; i32(f) traps
NULL / if (p)                none / if let q = p:
sizeof(T), offsetof(T, f)    sizeof(T), offsetof(T, f)
_Static_assert(c, "m")       assert(c, "m")   at module level
T a[N]                       var a: T[N]
char buf[N] = {0};           var buf: u8[N]
char buf[N];                 var buf: u8[N] = ---
T *items, size_t count       items: T[]
char *s                      s: cstr
p = malloc(sizeof(T))        let p = try new T(...)
p = calloc(n, sizeof(T))     let items = try new T[n]
p = malloc(n * sizeof(T))    let items = try alloc T[n]
free(p)                      free(p)
a / b, a % b (ints)          a // b, a % b    # same truncation as C
x * 31 + c (wrapping)        x *% 31 +% c
x & -x                       x & -%x
if (a + b < a) overflow      let sum = a +? b else ...
a && b, !a                   a and b, not a
goto cleanup                 defer / errdefer
break out of nested loop     outer: for ... / break outer
int f(T *out)                func f() -> (bool, T)
int f(void) /* may fail */   func f() -> !
uint32_t x = byte;           let x: u32 = byte          # widening is implicit
switch                       match, `1, 2, 3:` alternatives, ranges, guards
static int helper()          func helper()   # private by default
static int counter = 3;      var counter: i32 = 3        # module level
_Thread_local int t;         thread_local var t: i32
#include "x.h"               import x
#ifdef __x86_64__            asm x86_64: ... / per-target modules
printf("%d\n", n)            print(f"{n}")
LOG("x=%d", x) (macro)       log(f"x={x}")   with a `fmt` parameter
__FILE__, __LINE__           luce.file, luce.line, luce.location
_Atomic int n; n++;          var n: @i32; n += 1
union { int i; float f; }    union Value: / integer: i32 / real: f32
__attribute__((noreturn))    -> never
__attribute__((section(s)))  section(s) func ...
```

## 23. Relationship to Luce 1.0

Base is a profile of Luce, and this document restates every shared rule so that it can be read alone. For a reader who knows full Luce, these are the differences, and the reason for each.

| Area | Full Luce | Base | Why |
| --- | --- | --- | --- |
| Reference identity | `class`, ARC, `weak`, `deinit` | none | no runtime |
| Collections | `list`, `map`, `set` built in | library types over the current allocator | no hidden allocation |
| Allocation | implicit, in the runtime | `new`, `alloc`, `free`, `with`, `in` | allocation is a language operation |
| Text | owned, reference-counted `str`; `+` concatenates | `str` is a view; `format` and `fmt` | no owner for a fresh string |
| Slices | `slice[T]` retains its owner | `T[]` is a non-owning span | no reference counting |
| Pointers | `native_ptr[T]` in audited modules only | `T*` everywhere, C spelling | Base is the native tier |
| Optional pointers | token plus flag | null niche | C layout of struct fields; Base pointers are not handles |
| Interface values | boxed, copy-on-write | borrowed two-word view | no owner for a box |
| Closures | environment capture | capture-free function pointers | no owner for an environment |
| Errors | `Error.message` owned | `Error.message` a view | no allocation |
| Out of memory | fatal | recoverable `!` | bounded systems handle it |
| Integer division | `//` and `%` floor | truncate, as the instruction and C do | never slower or silently different from C |
| Integer widening | always written | implicit for the same signedness | no value can change |
| Zero values | every local initialised | typed `var` of a zeroable type is zero; `---` for any type | C idiom, with never-null types excluded |
| Globals | none | `var` with a constant initialiser, `thread_local var` | C needs them; no initialisation order |
| Labels, guards | refused | `break label`, `pattern if condition` | structured jumps and state machines |
| Methods | explicit `self` parameter; a type function has none | implicit `self`; `static func` | the receiver is what a function in a type has by default |
| Non-mutating `self` | a copy | `const Self*` | no copy per call; deterministic C header |
| Fallible `unit` result | `-> unit!` | `-> !` | the most common signature in systems code |
| `Display` | returns owned `str` | writes to a `Writer` | no allocation |
| `new` | constructs a class | allocates from an allocator | Base has no classes |
| `c.int` and kin | distinct types | aliases where the width is fixed | a distinct type carried no information |
| Atomics, `volatile`, `asm`, `union`, `usize`, `fmt`, attributes | absent | present | C capability, with a checkable restriction |
| Module suffix for audited code | `.native.luc` | `.lucn` | one scheme for three kinds |
| Export | `export c func` | `export func`, opt-in | one word for a linkage decision |

Five rules change for every module, Base or not, and are recorded here so that the 1.0 specification can be updated to match: the audited-module suffix becomes `.lucn`; `Plain` joins the closed list of compiler-known marker protocols; `native.retain`, `native.borrow`, and `native.release` join the closed intrinsic set of `.lucn` modules; the shared intermediate representation gains the additions of §19.2; and labels and mutable globals, excluded in full Luce, are admitted in Base modules only.

## 24. Examples

Complete programs, each written against the rules of this document. They are the shortest programs that use each part of the language, not the shortest programs possible. Every program is in a package with this manifest and is built and run as shown:

```toml
[package]
name = "examples"
```

```text
luce build main.lucb -o main      # one file, native target of the host
./main
luce test                         # every `test` declaration in the package
luce build --lib                  # a library plus its C header
```

### 24.1 Arguments and output

```luce
pub func main(arguments: str[]) -> i32:
    if arguments.length < 2:
        print("usage: greet NAME")
        return 1
    print(f"hello, {arguments[1]}")
    return 0
```

### 24.2 A struct with defaults, a custom initialiser, and a static constructor

```luce
pub let out_of_range: ErrorCode = ErrorCode.package(1)

pub struct Style:
    pub let width: f64 = 1.0
    pub let dashed: bool = false
    pub let color: u32 = 0xFFFFFF

pub struct Percentage:
    let value: f64

    pub func init(value: f64) -> !:
        if value < 0.0 or value > 100.0:
            error(out_of_range, "percentage must be 0 through 100")
        self.value = value

    pub static func half() -> Percentage:
        return Percentage(value = 50.0) catch failure: trap("50 is in range")

pub func main(arguments: str[]) -> i32!:
    let thin = Style(width = 0.5)                    # dashed and color defaulted
    let opacity = try Percentage(75.0)
    let half = Percentage.half()
    print(f"{thin.width} {opacity.value} {half.value}")
    return 0
```

### 24.3 A ring buffer

A struct that owns a span, allocated from the allocator that was current when it was made and released through the same one.

```luce
import memory
from memory import Allocator

pub let full: ErrorCode = ErrorCode.package(2)

pub struct Ring:
    var items: u32[]
    var head: usize
    var count: usize
    var allocator: Allocator

    pub static func create(capacity: usize) -> Ring!:
        return Ring(items = try new u32[capacity], head = 0, count = 0, allocator = memory.allocator)

    pub mutating func push(value: u32) -> !:
        if self.count == self.items.length:
            error(full, "ring buffer is full")
        self.items[(self.head + self.count) % self.items.length] = value
        self.count += 1

    pub mutating func pop() -> u32?:
        if self.count == 0: return none
        let value = self.items[self.head]
        self.head = (self.head + 1) % self.items.length
        self.count -= 1
        return value

    pub mutating func destroy():
        free(self.items) in self.allocator

pub func main(arguments: str[]) -> i32!:
    var ring = try Ring.create(4)
    defer ring.destroy()
    for value: u32 in 1..=4:
        try ring.push(value)
    while let value = ring.pop():
        print(f"{value}")
    return 0
```

### 24.4 An arena allocator

A type that implements `Allocator`, remembers its parent, and is used through `with`.

```luce
import files
import memory
from memory import Allocator

pub struct Arena: Allocator:
    var parent: Allocator
    var block: u8[]
    var used: usize

    pub static func over(parent: Allocator, capacity: usize) -> Arena!:
        return Arena(parent = parent, block = try alloc(capacity, 16) in parent, used = 0)

    pub mutating func allocate(size: usize, alignment: usize) -> u8[]?:
        let base = (usize)self.block.data
        let start = ((base + self.used + alignment - 1) & ~(alignment - 1)) - base
        let end = start +? size else return none
        if end > self.block.length: return none
        self.used = end
        return self.block[start..<end]

    pub mutating func resize(block: u8[], size: usize) -> bool:
        return size <= block.length

    pub mutating func release(block: u8[]):
        return                                   # an arena releases everything at once

    pub mutating func reset():
        self.used = 0

    pub mutating func destroy():
        free(self.block) in self.parent

func words(text: const u8[]) -> usize:
    var count: usize = 0
    var inside = false
    for byte in text:
        let space = byte == ' ' or byte == '\n'
        if not space and not inside: count += 1
        inside = not space
    return count

pub func main(arguments: cstr[]) -> i32!:
    var arena = try Arena.over(memory.allocator, 1 << 20)
    defer arena.destroy()
    with arena:
        let text = try files.read(arguments[1])   # allocated in the arena, freed by destroy
        print(f"{words(text)}")
    return 0
```

### 24.5 An intrusive list

Pointers, nullable pointers, `offsetof`, and the two casts C uses for the same structure.

```luce
struct Link:
    var next: Link*?

struct Task:
    var name: str
    var priority: u32
    var link: Link

func task_of(link: Link*) -> Task*:
    return (Task*)((u8*)link - offsetof(Task, link))

func push(head: Link*?*, task: Task*):
    task.link.next = *head
    *head = &task.link

func highest(head: Link*?) -> Task*?:
    var best: Task*? = none
    var current = head
    while let link = current:
        let task = task_of(link)
        if let leader = best:
            if task.priority > leader.priority: best = task
        else:
            best = task
        current = link.next
    return best

pub func main(arguments: str[]) -> i32:
    var tasks = [Task(name = "build", priority = 2, link = Link(next = none)),
                 Task(name = "test", priority = 5, link = Link(next = none)),
                 Task(name = "ship", priority = 1, link = Link(next = none))]
    var head: Link*? = none
    for task in &tasks:
        push(&head, task)
    if let task = highest(head):
        print(task.name)                          # test
    return 0
```

### 24.6 A tagged union

An enum with payloads, heap-allocated cases with `errdefer`, and an exhaustive `match` expression.

```luce
enum Expr:
    number(value: f64)
    add(left: Expr*, right: Expr*)
    multiply(left: Expr*, right: Expr*)
    negate(operand: Expr*)

func evaluate(expression: const Expr*) -> f64:
    return match *expression:
        .number(value) => value
        .add(left, right) => evaluate(left) + evaluate(right)
        .multiply(left, right) => evaluate(left) * evaluate(right)
        .negate(operand) => -evaluate(operand)

func release(expression: Expr*):
    match *expression:
        .number(_): return
        .add(left, right), .multiply(left, right):
            release(left)
            release(right)
        .negate(operand): release(operand)
    free(expression)

pub func main(arguments: str[]) -> i32!:
    let two = try new Expr.number(value = 2.0)
    errdefer release(two)
    let three = try new Expr.number(value = 3.0)
    errdefer release(three)
    let sum = try new Expr.add(left = two, right = three)
    errdefer free(sum)
    let negated = try new Expr.negate(operand = sum)
    defer release(negated)
    print(f"{evaluate(negated)}")                # -5.0
    return 0
```

### 24.7 Flags

```luce
enum Access as u32:
    empty = 0
    read = 1
    write = 2
    execute = 4

func describe(mode: Access) -> str:
    return match mode:
        .empty => "no access"
        .read => "read-only"
        _ if (mode & Access.write) != Access.empty => "writable"
        _ => "other"

pub func main(arguments: str[]) -> i32:
    let mode = Access.read | Access.write
    print(f"{describe(mode)} {(u32)mode}")       # writable 3
    return 0
```

### 24.8 Calling C

A binding written by hand: an opaque handle, a nullable result, C text, and cleanup with `defer`.

```luce
extern type Window
extern func SDL_Init(flags: u32) -> i32
extern func SDL_GetError() -> cstr
extern func SDL_CreateWindow(title: cstr, x: i32, y: i32, w: i32, h: i32, flags: u32) -> Window?
extern func SDL_DestroyWindow(window: Window)
extern blocking func SDL_Delay(ms: u32)
extern func SDL_Quit()

pub let sdl_failed: ErrorCode = ErrorCode.package(3)

func last_error() -> str:
    return str(SDL_GetError()) catch failure:
        recover "SDL reported invalid text"

pub func main(arguments: str[]) -> i32!:
    if SDL_Init(0x20) != 0:
        error(sdl_failed, last_error())
    defer SDL_Quit()
    let window = SDL_CreateWindow("Luce Base", 100, 100, 800, 600, 0) else error(sdl_failed, last_error())
    defer SDL_DestroyWindow(window)
    SDL_Delay(2000)
    return 0
```

### 24.9 Exporting to C

A Base function with a C-representable signature, and the header the compiler writes for it.

```luce
export func checksum(data: const u8[]) -> u32:
    var sum: u32 = 0
    for byte in data:
        sum = sum *% 31 +% byte                  # byte widens to u32 implicitly
    return sum
```

```c
#include <stdint.h>
#include <stddef.h>

uint32_t checksum(const uint8_t *data, size_t data_length);
```

### 24.10 A spinlock and a counter

```luce
import thread

struct SpinLock:
    var locked: @bool

    mutating func acquire():
        while self.locked.swap(true, .acquire):
            thread.pause()

    mutating func release():
        self.locked.store(false, .release)

struct Shared:
    var lock: SpinLock
    var total: u64

func bump(context: void*):
    let shared = (Shared*)context
    for _ in 0..<100_000:
        shared.lock.acquire()
        shared.total += 1
        shared.lock.release()

pub func main(arguments: str[]) -> i32!:
    var shared: Shared
    var handles: thread.Handle[4]
    for index in 0..<4:
        handles[index] = try thread.spawn(bump, &shared)
    for handle in handles:
        try handle.join()
    print(f"{shared.total}")                     # 400000
    return 0
```

### 24.11 A generic over a span

```luce
from luce import Comparable

func largest[T: Comparable](values: const T[]) -> T?:
    if values.length == 0: return none
    var best = values[0]
    for value in values:
        if value.compare(best) > 0: best = value
    return best

pub func main(arguments: str[]) -> i32:
    let numbers = [3, 9, 4]
    let top = largest(numbers) else trap("nonempty")
    print(f"{top}")                              # 9
    return 0
```

### 24.12 Text without allocation

Formatted output into a caller's buffer, a `str` that views it, and a `fmt` parameter that forwards a formatted string.

```luce
import io
import luce
from io import Location

func describe(bytes: usize, buffer: u8[]) -> str!:
    if bytes >= 1 << 20:
        return try format(buffer, f"{bytes >> 20} MiB")
    if bytes >= 1 << 10:
        return try format(buffer, f"{bytes >> 10} KiB")
    return try format(buffer, f"{bytes} B")

func log(message: fmt, at: Location = luce.location):
    discard(io.stderr().write(f"{at.file}:{at.line}: {message}\n") catch failure: recover 0)

pub func main(arguments: str[]) -> i32!:
    var buffer: u8[32]
    let size = try describe(3_145_728, buffer)
    print(size)                                  # 3 MiB
    log(f"described {size}")
    return 0
```

### 24.13 A string builder

A `Writer` over memory it owns, growing with `resize` where the allocator allows and reallocating otherwise.

```luce
import memory
from io import Writer
from memory import Allocator

pub struct Builder: Writer:
    var bytes: u8[]
    var length: usize
    var allocator: Allocator

    pub static func create(capacity: usize) -> Builder!:
        return Builder(bytes = try alloc u8[capacity], length = 0, allocator = memory.allocator)

    pub mutating func write(data: const u8[]) -> usize!:
        let needed = self.length +? data.length else error(memory.exhausted, "builder overflow")
        if needed > self.bytes.length:
            let grown = try alloc u8[needed * 2] in self.allocator
            memory.copy(grown, self.bytes, self.length)
            free(self.bytes) in self.allocator
            self.bytes = grown
        memory.copy(self.bytes[self.length..], data, data.length)
        self.length = needed
        return data.length

    pub func text() -> str:
        return (str)self.bytes[..<self.length]

    pub mutating func destroy():
        free(self.bytes) in self.allocator

pub func main(arguments: str[]) -> i32!:
    var builder = try Builder.create(16)
    defer builder.destroy()
    for index in 0..<3:
        try builder.write(f"line {index}\n")
    print(builder.text())
    return 0
```

### 24.14 A hash map

A generic open-addressing map keyed by any hashable, equatable type.

```luce
import memory
from luce import Equatable, Hashable
from memory import Allocator

pub struct Map[K: Hashable & Equatable, V]:
    var keys: K[]
    var values: V[]
    var used: bool[]
    var count: usize
    var allocator: Allocator

    pub static func create(capacity: usize) -> Map[K, V]!:
        return Map(keys = try alloc K[capacity], values = try alloc V[capacity],
                   used = try new bool[capacity], count = 0, allocator = memory.allocator)

    func slot(key: K) -> usize:
        var index = (usize)(hash(key) % self.keys.length)
        while self.used[index] and not (self.keys[index] == key):
            index = (index + 1) % self.keys.length
        return index

    pub mutating func insert(key: K, value: V) -> !:
        if self.count == self.used.length:
            error(memory.exhausted, "map is full")
        let index = self.slot(key)
        if not self.used[index]:
            self.used[index] = true
            self.keys[index] = key
            self.count += 1
        self.values[index] = value

    pub func find(key: K) -> V?:
        let index = self.slot(key)
        if not self.used[index]: return none
        return self.values[index]

    pub mutating func destroy():
        free(self.keys) in self.allocator
        free(self.values) in self.allocator
        free(self.used) in self.allocator

pub func main(arguments: str[]) -> i32!:
    var ages = try Map[str, u32].create(16)
    defer ages.destroy()
    try ages.insert("ada", 36)
    try ages.insert("alan", 41)
    let age = ages.find("alan") else trap("inserted")
    print(f"{age}")                              # 41
    return 0
```

### 24.15 Error handling end to end

A fallible function calling another, one `catch` that recovers, one that adds context through a caller's buffer, and `main` reporting.

```luce
import files

pub let missing_field: ErrorCode = ErrorCode.package(4)

struct Config:
    var name: str
    var threads: u32

func field(text: str, key: str) -> str?:
    let bytes = text.bytes
    var start: usize = 0
    while start < bytes.length:
        var end = start
        while end < bytes.length and bytes[end] != '\n':
            end += 1
        let line = bytes[start..<end]
        if line.length > key.length and (str)line[..<key.length] == key:
            var value = line[key.length..]
            while value.length > 0 and (value[0] == ' ' or value[0] == '='):
                value = value[1..]
            return (str)value
        start = end + 1
    return none

func number(text: str) -> u32!:
    if text.length == 0:
        error(missing_field, "empty number")
    var value: u32 = 0
    for byte in text.bytes:
        if byte < '0' or byte > '9':
            error(missing_field, "not a digit")
        value = value * 10 + u32(byte - '0')
    return value

func parse(text: str, scratch: u8[]) -> Config!:
    let name = field(text, "name") else error(missing_field, "name")
    let threads = field(text, "threads") else error(missing_field, "threads")
    let count = number(threads) catch failure:
        error(failure.code, try format(scratch, f"threads: {failure.message}"))
    return Config(name = name, threads = count)

func load(path: cstr, scratch: u8[]) -> Config!:
    let bytes = files.read(path) catch failure:
        if failure.code == files.missing:
            return try parse("name = default\nthreads = 4", scratch)
        error(failure.code, failure.message)
    defer free(bytes)
    return try parse(try str(bytes), scratch)

pub func main(arguments: cstr[]) -> i32!:
    var scratch: u8[256]
    let config = try load(arguments[1], scratch)
    print(f"{config.name} on {config.threads} threads")
    return 0
```
