# Releases

## 0.77

- `recover` runs deferred cleanup for the scopes it leaves in emitted C.
- The interpreter treats recovery as success for `errdefer`, preserves a pending
  recovery while cleanup executes, and clears handled recovery state.
- Nested recovery regression agrees between interpreter and compiled execution.
  Sanitized gate: 580 tests passed on arm64 macOS.
