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
| 5 | Types as syntax | partial | `parse_pointer_and_span_types`; checking is M3 |
| 7.3 | No chained comparisons; `not a == b` refused | done | `parse_chained_comparison`, `parse_not_before_comparison` |
| 7.5 | `(u8)x` cast vs `(value)` group | done | `parse_cast_vs_call` |
| 8 | Control flow syntax | partial | if/while/for/match/defer/labels in `parse_test` |
| 8.6 | `goto` reserved | done | `parse_goto_is_reserved` |
| 8.9 | `asm` raw lines | done | `lex_asm_body_is_raw`, `parse_asm` |
| 9–10 | Func/struct/enum/union/interface syntax | partial | `parse_test` |
| 13–14 | Generics/interfaces as syntax | partial | `parse_interface` |
| 16 | Imports, `test` | partial | `parse_import`, `parse_test_declaration` |
| 17 | `extern func` | partial | `parse_extern_func` |
| 21 | Grammar | partial | parser accepts the productions; not every form has a fixture |
| 6.1 | `let`/`var`, zero values for i64/bool | partial | `eval_zero_var`; other types later |
| 6.5 | Assignment, `+=` | partial | `eval_while`, struct methods |
| 6.6 | Mutability of `var` / mutating methods | partial | `check_mutating_needs_var` |
| 7.2 | Checked `+ - *`, truncating `//` `%` | partial | `eval_arithmetic`, overflow/div0 traps; wrapping ops later |
| 7.3 | `and`/`or`/`not`, no chaining | partial | `eval_bool_and_or`; parse rejects chaining |
| 9.5 | Methods, implicit `self`, `mutating` | partial | `eval_struct_method`, `check_explicit_self_rejected` |
| 11.5 | Traps | partial | overflow, division by zero, `trap()` |
| 6, 11–12, 15, 18–20, 22–24 | Rest of semantics, memory, C ABI | unsupported | M4–M14 |
