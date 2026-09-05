# Conformance ledger

Every section of [`language/base.md`](language/base.md) maps to tests. A row
is never silently omitted: it is `unsupported` until a slice lands.

Status: `done` (gate-green), `partial`, `unsupported`.

| Section | Topic | Status | Tests |
| --- | --- | --- | --- |
| 3.1 | Encoding (UTF-8, BOM, NUL, bidi, CRLF, tabs) | done | `source_test`, `lex_test` encoding cases |
| 3.1 | Confusable punctuation | done | `lex_test` look-alikes |
| 3.2 | Layout, 4-space indent, delimiter suites | done | `lex_test` layout cases |
| 3.3 | Comments and `##` documentation | done | `lex_test` comments |
| 3.4 | Naming (formatter/linter) | unsupported | later |
| 3.5 | Scope, no shadowing | partial | `check_no_shadow`; modules later |
| 3.6 | Reserved words; `class`/`spawn` refused | done | `lex_test` keywords, `parse_class_belongs_to_full_luce` |
| 4.1 | `true` `false` `none` | done | `lex_test` keywords |
| 4.2 | Integer literals | done | `lex_test` numbers |
| 4.3 | Float literals | done | `lex_test` numbers |
| 4.4 | Characters, strings, bytes, raw, formatted | done | `lex_test` strings, `parse_formatted_string` |
| 4.5 | Array literals | done | `parse_array_literal` |
| 5 | Types as syntax | partial | `parse_pointer_and_span_types`; scalars checked in M5 |
| 7.3 | No chained comparisons; `not a == b` refused | done | `parse_chained_comparison`, `parse_not_before_comparison` |
| 7.6 | Indexing and slicing | done | `agree_array_index`, `agree_slice`, `agree_index_oob_traps` |
| 7.7 | Pointer `*`, `&`, `p+n` | done | `agree_pointer_deref`, `agree_assign_through_pointer` |
| 8 | Control flow syntax | partial | if/while/for/match/defer/labels in `parse_test` |
| 8.3 | `for` over arrays and spans | done | `agree_for_span` |
| 8.3 | `for` over ranges | done | `agree_for_range` |
| 8.6 | `goto` reserved | done | `parse_goto_is_reserved` |
| 8.9 | `asm` raw lines | done | `lex_asm_body_is_raw`, `parse_asm` |
| 9–10 | Func/struct/enum/union/interface syntax | partial | `parse_test` |
| 13–14 | Generics/interfaces as syntax | partial | `parse_interface` |
| 16 | Imports, packages, `test` | done | `pkg_test`, `testdata/m9`, `lucb test` |
| 17 | `extern func` | partial | `parse_extern_func` |
| 21 | Grammar | partial | parser accepts the productions; not every form has a fixture |
| 5.1 | Integer scalars, `usize`/`isize`, `char` | done | `agree_u8_wrap`, `agree_sizeof_usize`, `check_u8_literal_ok` |
| 5.1 | `f32`/`f64` | partial | `agree_f64_to_i64`; `f16` unsupported |
| 5.3 | Pointers `T*`, `const T*`, `void*`, `T*?` | partial | `agree_pointer_deref`, `check_escape_local`; `T*?` is a nullable pointer |
| 5.4 | Arrays `T[N]`, spans `T[]` | done | `agree_array_index`, `agree_span_from_array`, `agree_slice` |
| 5.5 | `str` as a view | partial | `agree_str_length`; `str(bytes)` as `str!` waits on errors |
| 5.11 | `sizeof`, `offsetof`, `packed` / `align(N)` | done | `agree_sizeof_i64`, `agree_offsetof_packed`; `alignof` of types works |
| 6.6 | Address-of and escape | partial | `check_escape_local`; stores into escaped params later |
| 6.1 | `let`/`var`, zero values | done | `eval_zero_var`, `agree_zero_struct`; never-null pointers still require an initialiser |
| 6.2 | `---` uninitialised `var` | done | `agree_uninit` |
| 6.3 | Module `var` / `thread_local var` | done | `agree_global`, `agree_thread_local` |
| 6.5 | Assignment, `+=` | partial | `eval_while`, struct methods |
| 6.6 | Mutability of `var` / mutating methods | partial | `check_mutating_needs_var` |
| 7.2 | Checked `+ - *`, wrapping `%`, saturating `|`, `+?` | done | `agree_u8_wrap`, `agree_u8_overflow_traps`, `agree_u8_saturating`, `agree_overflow_optional` |
| 7.3 | Bits, shifts, `and`/`or`/`not` | done | `agree_bits`, `agree_shift`, `eval_bool_and_or` |
| 7.5 | Widening, `T(x)`, `(T)x` | done | `agree_widen`, `agree_c_cast_truncates`, `parse_cast_vs_call` |
| 9.5 | Methods, implicit `self`, `mutating` | partial | `eval_struct_method`, `check_explicit_self_rejected` |
| 11.5 | Traps | partial | overflow, division by zero, `trap()` |
| 19.1 | Compile to native via C | partial | `agree_test`; host `cc` |
| 5.8 | Optionals `T?`, `none`, `else`, `if let` | done | `agree_optional_else`, `agree_if_let`, `check_optional_ok` |
| 8.4 | Exhaustive `match` | done | `agree_match_int`, `agree_payload_enum`, `check_enum_match_missing` |
| 10.2 | Payload enums | done | `agree_payload_enum` |
| 10.3 | Integer-backed `enum as u32` | done | `agree_int_enum`, `agree_enum_checked_conv_traps` |
| 10.4 | Unions | done | `agree_union` |
| 8.5 | Labeled `break`/`continue` | done | `agree_labeled_break`, `agree_break` |
| 8.8 | `defer` | done | `agree_defer` |
| 8.8 | `errdefer` | partial | `eval_errdefer`; C emit skips it |
| 11.1 | Optionals as absence | done | `agree_optional_else`, `agree_if_let` |
| 11.2–11.4 | `T!`, `try`, `error`, `catch`, `recover` | done | `agree_try_catch`, `agree_try_ok`, `check_try_needs_fallible` |
| 9.7 | `pub func main(arguments: str[]\|cstr[]) -> i32\|i32!` | done | `agree_main_hello` |
| 16.3 | `import` / `from … import` | done | `load_and_check_import`, `load_from_import` |
| 16.4 | `luce.toml` | partial | `[package] name`; source roots default to the manifest directory |
| 16.5 | `test` / `lucb test` / `assert` | done | `eval_tests_pass_and_fail` |
| 5.2 | `cstr` | partial | type and `main(arguments: cstr[])`; library `c` module later |
| 6, 11–12, 15, 18–20, 22–24 | Rest of semantics, memory, C ABI | unsupported | M10–M14 |
