# Proposal

## Why

`json_enum_fields_valid` in `src/runner/app_suite.cpp` has a cyclomatic complexity of
**26**. It validates enum/string fields inside a raw JSON document before the
structured `from_json` parsers run. The function currently embeds three separate
validations (suite log level, legacy CPU arrays, canonical thread specs) plus a
loop over every app that checks impact enum and app log level. This makes the
function hard to unit-test in isolation and error-prone to extend when new
runner/suite JSON fields are added.

## What Changes

- **Extract focused validators** in the anonymous namespace:
  - `valid_json_log_level(const nlohmann::json& object, const char* key)` — check
    that a log level field is null, missing, or a recognized log level string.
  - `valid_json_cpu_array(const nlohmann::json& object, const char* key)` — check
    that a legacy CPU array field is missing or an array of unsigned 32-bit values.
  - `valid_json_thread_spec(const nlohmann::json& object, const char* key)` — check
    that a canonical thread spec field is missing or parses successfully.
  - `valid_json_app_entry(const nlohmann::json& entry)` — check impact enum string
    and app-level log level.
- **Reduce `json_enum_fields_valid`** to a thin orchestrator that calls the new
  validators in the original order and returns false on the first failure.
- Keep the public behavior identical: the same documents pass/fail as before.
- Add targeted unit tests for each new validator through `load_suite_manifest` and
  the existing manifest test helpers.

## Capabilities

### New Capabilities
None.

### Modified Capabilities
None. This is a pure internal refactor; no spec-level behavior changes.
`skip_specs: true` is set in `.openspec.yaml` for this change.

## Impact

- **Affected code:** `src/runner/app_suite.cpp` (internal implementation only).
- **Tests:** New cases in `tests/runner/app_suite_test.cpp` covering each extracted
  validator.
- **API/ABI:** No public API or manifest schema changes.
- **Performance:** No impact; JSON enum validation is cold-path configuration code.
