# Design

## Context

See `proposal.md` — Why. Both guards are pure text scanners over tracked sources:

- `cmake/check_naming.py` flags `struct`/`class`/`enum`/`union`/`concept` declarations and `using` type aliases whose name matches `UpperCamel_with_underscore`.
- `cmake/check_result_value.py` flags `.value()` receivers outside a small allowlist (`id`, `trace`, `span`, `TraceId`, `SpanId`, `*_id`, `entry`, `item`), because `std::expected::value()` aborts under `-fno-exceptions`.

All nine names are file-local: five in the execution-manager CLI, one in the hub CLI, three function-local aliases in two tracing tests.

## Goals / Non-Goals

**Goals:**

- Both guards exit 0 on the committed tree.
- Zero behavioral, API or diagnostic change; the CLI surface of `nexigen-exec` and `nexigen-hub` stays byte-identical.
- Existing test binaries keep passing without touching assertions.

**Non-Goals:**

- The `format-check` migration (115 files, separate change).
- Any guard-rule change: the guards stay as strict as they are, so the fix must satisfy them rather than weaken them.

## Decisions

### D1 — Rename to PascalCase, do not add `// NX_ALLOW_NAMING` opt-outs

Both guards support a line-level opt-out, but these ten names are ordinary internals with no compatibility requirement, so renaming is cheaper than accumulating exemptions. Exemptions would also have to be re-justified by every future reader. Every touched symbol is file-local, so a mechanical rename cannot affect another translation unit.

### D2 — Fix the result-value finding by renaming the test local, not by opting out

The guard documents "local ID values used by tests (`trace`, `span`, …)" as an allowed receiver, and `span_keep` is exactly that case; the allowlist simply predates the name. Renaming the local to `span` (block-scoped, no shadowing in that function) keeps the gate strict and needs no comment. `// NX_ALLOW_VALUE` stays reserved for receivers that cannot be renamed.

### D3 — Rename in place, one file per commit-sized step

Each name is declared and used in a single file, except the two tracing test aliases that repeat inside two functions of the same file. A per-file rename keeps the diff readable and lets each step be verified by the guards alone.

## Risks / Trade-offs

- **`span` shadows something in the enclosing test scope** → the variable is block-scoped inside one `{ }` in a test function; checked that no `span` identifier is live there, and the build proves it.
- **Rename drifts out of sync in one of the repeated aliases** → the compiler catches every stale use (`-Werror`), and the guard confirms zero remaining snake_case declarations.
- **`nexigen-exec`/`nexigen-hub` CLI text accidentally changes** → the CLI baselines captured for the complexity change (`build/baseline/cli/`) are still available for a byte-level diff; the tools' `--help` and error paths are re-diffed after the rename.
