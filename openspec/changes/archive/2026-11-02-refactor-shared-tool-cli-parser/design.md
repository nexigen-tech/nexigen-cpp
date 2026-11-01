# Design

## Context

Both tools implement the same unsigned-decimal parser in their `main.cpp`:

- `tools/nexigen-hub/main.cpp:46` — `parse_u64(std::string_view, std::uint64_t&) -> bool`
- `tools/nexigen-run/main.cpp:45` — `parse_uint(std::string_view, std::uint64_t&) -> bool`

The logic is byte-for-byte the same (empty/non-digit/overflow rejection, leaving `out` unchanged
on failure). `tools/CMakeLists.txt` builds each tool from a single `main.cpp` with no shared
include directory.

## Goals / Non-Goals

**Goals:**

- Extract one shared parser and have both tools use it, removing the duplication.
- Preserve exact behavior (empty input, non-digit, and overflow all fail; `out` unchanged).

**Non-Goals:**

- No behavior change, no new options, no library change.
- No extraction of tool-specific glue (`print_usage`, the signal flag, the option loop) — those are
  unique to each tool and would not deduplicate.

## Decisions

### D1 — Header-only `tools/detail/cli.hpp` with `nexigen::tools::parse_u64`

Create `tools/detail/cli.hpp` exposing `nexigen::tools::parse_u64`, and add
`target_include_directories(<tool> PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})` for both tools so each
`main.cpp` can `#include "detail/cli.hpp"`.

- *Rationale:* the function is a small `inline` helper, so a header keeps the tools dependency-free
  (no new translation unit or library); the include directory is the standard way to share a
  header across the two tool sources.
- *Alternative considered:* put the helper in `nexigen::core`. Rejected: it is a CLI-only concern
  unrelated to the real-time library, and would drag tooling concerns into the public library.

### D2 — Unify on the `parse_u64` name

Rename `nexigen-run`'s `parse_uint` to the shared `nexigen::tools::parse_u64` (three call sites in
the tool: the timeout and grace parsing). The hub already uses `parse_u64`.

- *Rationale:* one name across tools; the semantic (unsigned 64-bit) is clearer than `uint`.

## Risks / Trade-offs

- [Include-path coupling] Both tools now depend on a shared header under `tools/`. →
  Mitigation: the header is self-contained and dependency-free; the include directory is added only
  to the two tool targets.
- [Silent behavior drift] Replacing the local parser must not change behavior. → Mitigation:
  the shared body is copied from the existing code verbatim, and the tool help smoke tests
  (`nexigen_run_help`, `nexigen_hub_help`) plus a build in all modes re-verify it.

## Migration Plan

Not applicable — internal tooling refactor with no persisted format or API change.

## Open Questions

None.
