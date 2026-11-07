# Proposal

## Why

`nexigen::runner::validate_suite_manifest` in `src/runner/app_suite.cpp` has a cyclomatic
complexity of **59**, making it the most complex function in the project according to the
C++ project analysis. A single validator of this size is hard to reason about, expensive
to review, and difficult to unit-test exhaustively. Each new manifest rule increases the
risk of regressions because the control flow is deeply nested and mixes suite-level,
app-level, cross-app, and derived-path checks.

We should refactor it into a set of small, focused validators whose combined behavior is
identical to the original, but whose individual complexity is low enough to be readable,
testable, and safe to extend.

## What Changes

- **Decompose `validate_suite_manifest`** into focused internal validators, each returning
  `core::Status`:
  - `validate_suite_header_and_paths` — `version`, `suite_id`, `run_dir`,
    `artifacts_path`, `global_settings_path`, restart budgets, and path length limits.
  - `validate_app_spec` — per-application checks for identity, command, arguments,
    environment variables, impact enum, log level, timeouts, settings paths, CPU set, and
    derived path lengths.
  - `validate_cross_app_constraints` — duplicate `(app, instance)` pairs and global
    application count limits.
- **Introduce an internal helper** (`manifest_validation_error`) for uniform
  `InvalidArgument` returns so every validator uses the same error path.
- **Preserve exact public behavior**: `load_suite_manifest`, `SuiteManifest`, `AppSpec`,
  and all error codes remain unchanged.
- **Add unit tests** for each decomposed validator covering both valid inputs and the
  specific rejection cases they own.

No manifest schema changes, no new public APIs, and no hot-path code changes.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
None. This is a pure internal refactor; no spec-level behavior changes.
`skip_specs: true` is set in `.openspec.yaml` for this change.

## Impact

- **Affected code:** `src/runner/app_suite.cpp` (internal implementation only).
- **Tests:** New or extended tests under `tests/runner/` for the decomposed validators.
- **API/ABI:** No public API or manifest schema changes.
- **Performance:** No impact; suite manifest validation is a cold-path operation that runs
  once before the detached supervisor is forked.
