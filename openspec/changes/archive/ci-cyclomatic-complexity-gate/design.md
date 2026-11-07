# Design

## Context

See `proposal.md`. The gate follows the same pattern as
`Format.cmake` / `format-check` and the Python lint checks
(`check_naming.py`, `check_result_value.py`).

## Goals / Non-Goals

**Goals:**
- Prevent CCN regressions in `include/`, `src/`, `tests/`.
- Run in < 15 s as a standalone CI job (no C++ compilation needed).

**Non-Goals:**
- No changes to test or production behaviour.
- No spec-level changes (`skip_specs: true`).

## Decisions

1. **Python + lizard**: same dependency pattern as `clang-format` —
   install via `pip` in CI. Fast, no compilation.
2. **alignas filter**: lizard misidentifies `alignas(...)` as a
   function; the Python script strips these warnings.
3. **Exclude examples/**: demo code is exempt from the CCN gate.
4. **CMake target name**: `complexity-check`, parallel to
   `format-check`.
