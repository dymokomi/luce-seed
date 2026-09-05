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
| 3.6 | Reserved words; `class`/`spawn`/`weak` refused | done | `lex_test` keywords, `parse_class_belongs_to_full_luce`, `parse_spawn_belongs_to_full_luce`, `parse_weak_belongs_to_full_luce` |
| 4.1 | `true` `false` `none` | done | `lex_test` keywords |
| 4.2 | Integer literals | done | `lex_test` numbers |
| 4.3 | Float literals | done | `lex_test` numbers |
| 4.4 | Characters, strings, bytes, raw, formatted | done | `lex_test` strings, `parse_formatted_string` |
| 4.5 | Array literals | done | `parse_array_literal`, `agree_array_infer` |
| 5 | Types as syntax | done | `parse_pointer_and_span_types`; scalars, aliases, func types, tuples |
| 7.3 | No chained comparisons; `not a == b` refused | done | `parse_chained_comparison`, `parse_not_before_comparison` |
| 7.6 | Indexing and slicing | done | `agree_array_index`, `agree_slice`, `agree_slice_from_zero`, `agree_index_oob_traps` |
| 7.7 | Pointer `*`, `&`, `p+n` | done | `agree_pointer_deref`, `agree_assign_through_pointer` |
| 7.7 | `memory.copy` / `move` / `set` / `read[T]` / `write[T]` | done | `agree_memory_copy`, `agree_memory_move`, `agree_memory_set`, `agree_memory_read_write`, `testdata/programs/memory.lucb` |
| 7.4 | `hash` / `hex` / `bin` / `pad` | done | `agree_hash_int`, `agree_hex_bin_pad`, `testdata/programs/hash.lucb` |
| 8 | Control flow syntax | done | if/while/for/match/defer/labels in `parse_test`; match expressions `agree_match_expr` |
| 8.3 | `for` over arrays and spans | done | `agree_for_span` |
| 8.3 | `for` over ranges | done | `agree_for_range` |
| 8.6 | `goto` reserved | done | `parse_goto_is_reserved` |
| 8.9 | `asm` raw lines | done | `lex_asm_body_is_raw`, `parse_asm` |
| 9–10 | Func/struct/enum/union/interface syntax | done | `parse_test`; field defaults `agree_field_default`; aliases, `func` values, default args, `discard` |
| 13–14 | Generics/interfaces as syntax | done | `parse_generic_func`, `parse_interface`; interface semantics in M12 |
| 16 | Imports, packages, `test` | done | `pkg_test`, `testdata/m9`, `lucb test` |
| 17 | `extern func` | done | `parse_extern_func`, `agree_extern_abs`, `agree_extern_strlen`, `agree_extern_as_name` |
| 21 | Grammar | partial | parser accepts the productions; programs in `testdata/programs/` |
| 5.1 | Integer scalars, `usize`/`isize`, `char` | done | `agree_u8_wrap`, `agree_sizeof_usize`, `check_u8_literal_ok` |
| 5.1 | `f32`/`f64` | partial | `agree_f64_to_i64`; `f16` unsupported |
| 5.3 | Pointers `T*`, `const T*`, `void*`, `T*?` | partial | `agree_pointer_deref`, `agree_ptr_int_cast`, `check_nullable_deref`, `check_escape_local` |
| 5.4 | Arrays `T[N]`, spans `T[]` | done | `agree_array_index`, `agree_span_from_array`, `agree_slice` |
| 5.5 | `str` as a view | done | `agree_str_length`, `agree_str_bytes`, `agree_str_from_bytes`, `agree_str_cstr`, `agree_str_unchecked`, `agree_str_invalid_utf8` |
| 5.6 | Function types `func(A, B) -> R` | done | `agree_func_value`, `agree_alias_func`, `check_func_type_ok`, `check_func_no_zero` |
| 5.7 | Tuples `(i64, str)` | done | `agree_tuple`, `eval_tuple`, `check_tuple_ok` |
| 5.10 | Type aliases | done | `agree_type_alias`, `eval_type_alias`, `check_type_alias_ok`, `check_alias_recursive` |
| 5.11 | `sizeof`, `offsetof`, `packed` / `align(N)` | done | `agree_sizeof_i64`, `agree_sizeof_ptr`, `agree_offsetof_packed` |
| 6.6 | Address-of and escape | partial | `check_escape_local`, `agree_escape_global`; stores into escaped params later |
| 6.1 | `let`/`var`, zero values | done | `eval_zero_var`, `agree_zero_struct`, `check_never_null_zero`; never-null pointers still require an initialiser |
| 6.2 | `---` uninitialised `var` | done | `agree_uninit` |
| 6.3 | Module `var` / `thread_local var` | done | `agree_global`, `agree_thread_local` |
| 6.5 | Assignment, `+=` | partial | `eval_while`, struct methods |
| 6.6 | Mutability of `var` / mutating methods | partial | `check_mutating_needs_var`, `check_assign_let` |
| 7.2 | Checked `+ - *`, wrapping `%`, saturating `|`, `+?` | done | `agree_u8_wrap`, `agree_u8_overflow_traps`, `agree_u8_saturating`, `agree_overflow_optional` |
| 7.3 | Bits, shifts, `and`/`or`/`not` | done | `agree_bits`, `agree_shift`, `eval_bool_and_or` |
| 7.5 | Widening, `T(x)`, `(T)x` | done | `agree_widen`, `agree_c_cast_truncates`, `parse_cast_vs_call` |
| 7.8 | Match expressions | done | `agree_match_expr`, `eval_match_expr` |
| 7.9 | `discard` | done | `agree_discard`, `eval_discard`, `check_discard_ok`, `check_discard_fallible` |
| 9.2 | Default parameters | done | `agree_default_args`, `agree_named_default`, `eval_default_args` |
| 9.3 | Multiple results / tuples | done | `agree_tuple` |
| 9.4 | Function values | done | `agree_func_value`, `agree_method_value`, `agree_func_to_fallible`, `check_func_must_be_called` |
| 9.5 | Methods, implicit `self`, `mutating` | partial | `eval_struct_method`, `check_explicit_self_rejected` |
| 9.6 | Capture-free lambdas | done | `agree_lambda`, `eval_lambda`, `check_lambda_ok`, `check_lambda_capture_rejected` |
| 11.5 | Traps | partial | overflow, division by zero, `trap()` |
| 19.1 | Compile to native via C | partial | `agree_test`; host `cc` |
| 5.8 | Optionals `T?`, `none`, `else`, `if let` | done | `agree_optional_else`, `agree_if_let`, `check_optional_ok` |
| 8.4 | Exhaustive `match` | done | `agree_match_int`, `agree_payload_enum`, `check_enum_match_missing`, `check_match_guard_not_cover` |
| 10.2 | Payload enums | done | `agree_payload_enum` |
| 10.3 | Integer-backed `enum as u32` | done | `agree_int_enum`, `agree_enum_checked_conv_traps` |
| 10.4 | Unions | done | `agree_union` |
| 8.5 | Labeled `break`/`continue` | done | `agree_labeled_break`, `agree_break` |
| 8.8 | `defer` | done | `agree_defer` |
| 8.8 | `errdefer` | done | `eval_errdefer`, `agree_errdefer` |
| 11.1 | Optionals as absence | done | `agree_optional_else`, `agree_if_let` |
| 11.2–11.4 | `T!`, `try`, `error`, `catch`, `recover` | done | `agree_try_catch`, `agree_try_ok`, `check_try_needs_fallible` |
| 9.7 | `pub func main(arguments: str[]\|cstr[]) -> i32\|i32!` | done | `agree_main_hello` |
| 16.3 | `import` / `from … import` | done | `load_and_check_import`, `load_from_import`, `check_unused_import`, `hidden_import_rejected` |
| 16.4 | `luce.toml` | partial | `[package] name`; source roots default to the manifest directory |
| 16.5 | `test` / `lucb test` / `assert` | done | `eval_tests_pass_and_fail` |
| 5.2 | `cstr` | done | type, `main(arguments: cstr[])`, `c` module, `agree_extern_strlen`, `agree_c_int` |
| 12.2 | `new` / `alloc` / `free` / `in` | done | `agree_new_i64`, `agree_new_span`, `agree_new_count_var`, `agree_new_enum_case`, `agree_alloc_span`, `check_alloc_needs_count` |
| 12.3 | current allocator, `with`, `memory.allocator` / `heap` | done | `agree_fixed_buffer`, `agree_memory_heap` |
| 12.4 | `Allocator`, `FixedBuffer`, `CAllocator` | partial | builtin view; no `implements`, `PageAllocator`, or `Arena`; `memory.grow` `agree_memory_grow`, `agree_memory_grow_fixed` |
| 12.2 | `memory.exhausted` | done | `agree_fixed_exhausted`, `eval_fixed_exhausted` |
| 13.1 | Generic functions and structs, monomorphise | done | `agree_generic_id`, `agree_generic_first`, `agree_generic_pair`, `agree_generic_pair_infer`, `agree_generic_span` |
| 13.1 | Constraints, declaration-time checking | done | `agree_generic_comparable`, `check_generic_plus_rejected`; user interfaces as constraints |
| 14.1 | Interface declaration, `implements` | done | `agree_interface_view`, `check_interface_missing_method` |
| 14.3 | Two-word interface views | done | `agree_interface_view`, `agree_writer_view` |
| 14.4 | `Writer`, `Display`, `print(f"...")` | partial | `agree_print_formatted`, `agree_writer_view`, `agree_hex_bin_pad`; user `Display` not yet |
| 5.5 / 9.1 | `fmt`, `format`, `luce.location` | done | `agree_format`, `agree_location` |
| 16.6 | `io.stdout` / `io.stderr` as `Writer` | done | `agree_io_stderr` |
| 16.6 | `files.read` / `files.write` / `files.list` | done | `agree_files_roundtrip`, `agree_files_list_missing`, `testdata/programs/list.lucb` |
| 16.6 | `process.run` | done | `agree_process_run`, `testdata/programs/spawn.lucb` |
| 17.1 | `extern` / `export`, `null_foreign` | done | `agree_null_foreign`, `agree_export_twice`, `eval_null_foreign`, `check_extern_str_rejected`; `out` unsupported |
| 17.2 | Variadic C calls | done | `agree_variadic_printf`, `check_variadic_str_rejected` |
| 17.6 | Export header | partial | `header_export_func`, `lucb header`; status-form fallible export later |
| 5.9 / 15.1 | `@T` atomics, `atomic.fence`, `Ordering` | done | `agree_atomic_add`, `agree_atomic_method`, `agree_atomic_cas`, `agree_atomic_wait` |
| 15.2 | `volatile T*` | done | `agree_volatile_store` |
| 15.3 | `thread.spawn` / `Handle` | partial | `agree_thread_spawn`, `agree_thread_current`; optional `stack`/`name` later |
| 15.3 | `sync.Mutex` / `Condition` / `Once` / `Semaphore` | done | `agree_sync_mutex`, `agree_sync_once`, `agree_sync_cond`, `agree_sync_sem` |
| 8.9 | `asm` semantics | partial | `eval_asm_rejected`, `native_asm_add`; interpreter rejects, C backend emits operands |
| 6, 11–12, 18–20, 22–24 | Rest of semantics, C ABI | unsupported | later |
