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
| 3.4 | Naming (formatter/linter) | unsupported | M2+ |
| 3.5 | Scope | unsupported | M3 |
| 3.6 | Reserved words | done | `lex_test` keywords |
| 4.1 | `true` `false` `none` | done | `lex_test` keywords |
| 4.2 | Integer literals | done | `lex_test` numbers |
| 4.3 | Float literals | done | `lex_test` numbers |
| 4.4 | Characters, strings, bytes, raw, formatted | done | `lex_test` strings |
| 4.5 | Array literals | unsupported | M2 |
| 5–24 | Types through examples | unsupported | M2–M14 |

Grammar tokens of §21 that are not a literal or keyword are covered by the
operator and punctuation cases in `lex_test`.
