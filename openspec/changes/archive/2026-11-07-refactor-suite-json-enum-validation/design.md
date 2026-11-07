# Design

## Context

`json_enum_fields_valid` in `src/runner/app_suite.cpp` validates string/enum
fields in a raw suite manifest JSON document before structured deserialization.
It has cyclomatic complexity **26** and embeds four distinct responsibilities:

1. Validate suite-level `log_level`.
2. Validate legacy `runner_control_cpus` / `runner_logger_cpus` arrays.
3. Validate canonical `runner_control_thread` / `runner_logger_thread` specs.
4. Validate each app's `impact_on_app_suite_operability` and `log_level`.

The function uses two local lambdas (`valid_level`, `valid_cpu_array`) that are
re-created on every call and are not reusable elsewhere. The goal is to split
these into small, named helpers without changing which documents are accepted or
rejected.

## Goals / Non-Goals

**Goals:**

- Reduce the cyclomatic complexity of `json_enum_fields_valid` to under 10.
- Extract one helper per validation responsibility.
- Preserve exact pass/fail behavior for every manifest document.
- Keep all helpers inside the existing anonymous namespace in
  `src/runner/app_suite.cpp`.
- Add unit tests covering each extracted helper through the public
  `load_suite_manifest` entry point.

**Non-Goals:**

- Changing the manifest schema, the `SuiteManifest` structure, or the public API.
- Refactoring unrelated functions in `app_suite.cpp`.
- Reusing these validators outside the manifest loader.

## Decisions

### 1. Validator decomposition

Extract:

- `valid_json_log_level(const nlohmann::json& object, const char* key)` —
  missing/null, string, and recognized log level.
- `valid_json_cpu_array(const nlohmann::json& object, const char* key)` —
  missing or array of unsigned integers ≤ `UINT32_MAX`.
- `valid_json_thread_spec(const nlohmann::json& object, const char* key)` —
  missing or parseable by `core::parse_thread_spec_json`.
- `valid_json_app_entry(const nlohmann::json& entry)` — object with string
  `impact_on_app_suite_operability` and valid app-level `log_level`.

`json_enum_fields_valid` becomes an orchestrator:

1. Reject non-object documents.
2. Validate suite log level.
3. Validate legacy CPU arrays.
4. Validate canonical thread specs.
5. Validate `apps` array and each entry.

**Rationale:** Each helper maps directly to one conceptual validation rule,
making it obvious where to add a new field check.

### 2. Reuse the existing `read_log_level` pattern

The new `valid_json_log_level` will mirror the logic already present in
`read_log_level` but operate on a raw `nlohmann::json` node rather than a
`SuiteManifest` member. This avoids behavioral drift between loader pre-validation
and structured deserialization.

**Rationale:** Consistency with existing code reduces the chance that the same
field is validated differently in two places.

### 3. Keep helpers internal

All helpers remain in the anonymous namespace. No new public symbols are added.

**Rationale:** These validators are specific to the manifest loader's pre-flight
enum checks; exposing them would create unnecessary coupling.

### 4. Test through `load_suite_manifest`

Unit tests will craft JSON documents that exercise exactly one failure path at a
time and assert that `load_suite_manifest` returns `InvalidArgument`. The
existing `app_suite_test.cpp` already has a `rejected()` helper for this purpose.

**Rationale:** This proves the orchestrator still rejects the same documents and
avoids making helpers public just for testing.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| Behavioral regression: a document that previously failed now passes, or vice versa | Add one test per rejection path before the refactor and keep them green. |
| Helper names leak implementation details | Name helpers after the JSON field/rule they check, not the work step. |
| Refactor does not reduce complexity enough | Target each helper under 8 cyclomatic complexity; measure with `lizard`. |

## Open Questions

None.
