# Design

## Context

`nexigen::runner::validate_suite_manifest` in `src/runner/app_suite.cpp` currently
contains every manifest validation rule in a single 110-line function. The cyclomatic
complexity is 59, which is well above the maintainability threshold. The rules span
multiple layers:

1. **Suite-level scalars and top-level paths** (`version`, `suite_id`, `run_dir`,
   `artifacts_path`, restart budgets).
2. **Per-application checks** repeated for every entry in `apps`.
3. **Cross-application checks** (duplicate `(app, instance)` pairs).
4. **Derived path-length checks** that depend on both suite and app fields.

The existing helpers (`safe_id`, `absolute_path`, `has_parent_segment`, `read_log_level`,
etc.) already handle small, reusable checks, but `validate_suite_manifest` still
orchestrates them inline. See `proposal.md` for the motivation to refactor.

Project constraints that shape the design:

- C++23, `-fno-exceptions`/`-fno-rtti`; `core::Status` is `std::expected<void, ErrorCode>`.
- Suite supervision is cold-path only, so readability and testability matter more than
  micro-optimizations.
- The public API (`load_suite_manifest`, `SuiteManifest`, `AppSpec`) must stay unchanged.

## Goals / Non-Goals

**Goals:**

- Reduce the cyclomatic complexity of `validate_suite_manifest` from 59 to under 15.
- Split the function into focused validators with a single responsibility each.
- Preserve exact validation semantics and `InvalidArgument` error codes.
- Make each rule independently testable through the public orchestrator.
- Keep all changes inside `src/runner/app_suite.cpp`; no new public headers.

**Non-Goals:**

- Changing the suite manifest schema or adding/removing validation rules.
- Optimizing validation runtime (it is already cold-path and fast enough).
- Introducing new dependencies or abstractions (validators, dependency injection, policy
  objects) beyond simple free functions.
- Refactoring unrelated complex functions in `app_suite.cpp` (e.g., `json_enum_fields_valid`).

## Decisions

### 1. Validator decomposition

`validate_suite_manifest` will become a thin orchestrator that calls, in order:

1. `validate_suite_header_and_paths(manifest)` — checks `version == 1`, suite log level,
   `suite_id`, absolute `run_dir`/`artifacts_path`, null bytes in paths,
   `global_settings_path` absoluteness, and restart budgets. It delegates to:
   - `validate_suite_version_and_log_level`
   - `validate_suite_paths`
   - `validate_suite_restart_budgets`
2. `validate_app_count(manifest)` — checks `!apps.empty()` and `apps.size() <= max_apps`.
3. `validate_app_spec(app, manifest, online_cpus)` — per-application orchestrator that
   delegates to:
   - `validate_app_identity_and_command`
   - `validate_app_arguments`
   - `validate_app_environment`
   - `validate_app_impact_and_log_level`
   - `validate_app_timeouts`
   - `validate_app_settings_paths`
   - `validate_app_cpu_set`
   - `validate_app_paths`
4. `validate_unique_app_instances(apps)` — checks that no `(app, instance)` pair is
   duplicated.

**Rationale:** Each validator maps to one conceptual layer (suite, app, cross-app),
matching the way the requirements in `runner/app-suite` are organized. This makes it
obvious where a new rule belongs.

**Alternative considered:** Extract every tiny check into its own function. Rejected
because it fragments related logic and would create too many functions with shared
context (online CPU count, path limits).

### 2. Keep validators in the anonymous namespace

The new validators will live in the existing anonymous namespace in
`src/runner/app_suite.cpp`. They will not be exposed in `app_suite.hpp`.

**Rationale:** The validators are implementation details. Making them public would widen
the module's interface and encourage tests to depend on internal structure.

**Alternative considered:** Move validators to a testable `detail` namespace in a header.
Rejected to avoid increasing public API surface and to prevent tests from becoming
brittle to refactorings.

### 3. Test through the public orchestrator

Unit tests will continue to call `runner::validate_suite_manifest` (or
`runner::load_suite_manifest`) with crafted inputs that exercise exactly one rule at a
time. This proves that the decomposed validators still compose into the same behavior.

**Rationale:** This avoids exposing internals while still giving each rule independent
coverage. The existing `app_suite_test.cpp` already builds manifests programmatically, so
extending it is straightforward.

**Alternative considered:** Test each validator directly. Rejected because it would
require making them public or using `friend` tests, both of which add coupling.

### 4. Error handling stays uniform

Every validator returns `core::Status`. The orchestrator forwards the first failure
immediately. All failures remain `core::ErrorCode::InvalidArgument`.

**Rationale:** This preserves the current contract and keeps the refactor behaviorally
transparent to callers.

### 5. CPU online count is fetched once

`sysconf(_SC_NPROCESSORS_ONLN)` will still be called once in the orchestrator and passed
as a parameter to `validate_app_spec`, avoiding repeated syscalls.

**Rationale:** Maintains the existing efficiency and avoids side effects inside pure
validation functions.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| Behavioral regression in an edge-case rejection | Add one test per rejection path before/after the refactor. Run the full runner test suite and the CLI smoke tests. |
| Refactor does not reduce complexity enough | Target each validator under 10 cyclomatic complexity; measure with `lizard` before marking the change complete. |
| Diff becomes large and hard to review | Keep the decomposition mechanical: move existing code blocks into new functions without rewriting logic in the first commit. |
| Merge conflicts with concurrent manifest changes | Keep the change scoped to `app_suite.cpp` and communicate with owners of `runner/app-suite`. |

## Open Questions

None. The scope is a pure internal refactor with no spec-level changes.
