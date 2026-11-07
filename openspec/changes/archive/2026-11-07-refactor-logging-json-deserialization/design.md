# Design

## Context

The logging module keeps all JSON parsing and settings merging in a few large functions (see `proposal.md - Why`). `src/logging/logging_settings.cpp` validates the raw user document field-by-field in `sanitize_document` and then merges environment overrides in `apply_environment`. `src/logging/logger_options_format.cpp` restores typed `LoggerOptions`, `FileLoggerConfig`, `ConsoleLoggerConfig`, and `SpamProtectionConfig` values from sanitized JSON. All of this work is initialization-time only: it runs once per logger construction and never on the producer or writer hot paths.

The current implementation is correct, but the field-level validation rules are embedded as inline blocks inside each consumer. This makes the functions large and couples "how to read one field" to "how to assemble a whole struct".

## Goals / Non-Goals

**Goals:**

- Reduce the cyclomatic complexity of `sanitize_document` to under 15.
- Reduce the cyclomatic complexity of each `from_json` overload in `src/logging/logger_options_format.cpp` to under 15.
- Extract reusable cold-path JSON field-readers so future fields follow the same shape.
- Preserve the exact behavior of `load_logging_settings`, including diagnostic messages, merge precedence (global → local → environment), and the treatment of missing/invalid fields.
- Keep all helpers internal to the translation unit; test only through existing public orchestrators.

**Non-Goals:**

- Changing the public API (`load_logging_settings`, `Settings`, `LoggerOptions`, `FileLoggerConfig`, `ConsoleLoggerConfig`, `SpamProtectionConfig`).
- Changing hot-path code or allocation behavior.
- Refactoring JSON deserialization in other modules (metrics/tracing); those are separate changes.

## Decisions

### 1. One helper per field in `sanitize_document`

Each top-level key in the input document gets a named helper, for example `sanitize_log_root_directory`. The helper takes the raw value, validates it, writes the sanitized copy to `clean` on success, and calls `add_diagnostic` on failure. This mirrors the current per-field block exactly, so behavior is preserved.

*Alternative considered:* a generic schema-driven validator. Rejected because the current rules include project-specific constraints (NUL-byte checks, 511-byte limits, unsigned ranges) that are clearer as explicit helpers, and a generic dispatcher would not reduce complexity.

### 2. Templated/parameterized field readers for `from_json`

`logger_options_format.cpp` introduces small helpers such as `read_string_field`, `read_u64_field`, `read_bool_field`, `read_log_level_field`, `read_overflow_policy_field`, `read_thread_spec_field`, `read_cpu_set_field`. Each helper:

- Looks up a key in the JSON object.
- Returns `true` if the key is missing or successfully parsed.
- Returns `false` on type mismatch.

The struct-level `from_json` becomes an orchestrator that calls the helpers in the original order and returns early on the first type mismatch, preserving the current "wrong shape leaves the target untouched" contract.

*Alternative considered:* a single generic `read_field<T>` using `if constexpr`. Rejected because the enum and `ThreadSpec`/`CpuSet` parsing needs project-specific `from_string`/`parse_*` calls; explicit helpers are easier to read and keep complexity visible.

### 3. Environment helpers mirror sanitized-field helpers

`apply_environment` gets one helper per `NEXIGEN_*` variable (e.g., `apply_log_level_env`, `apply_log_dir_env`). Each helper reads the environment variable, validates it, updates `settings.options`, and adds a diagnostic on failure.

### 4. Internal, TU-local helpers

All new helpers live in the anonymous namespace of their existing `.cpp` file. They are not declared in headers, so the public surface stays unchanged and the compiler can inline them freely.

## Risks / Trade-offs

- **Risk:** Refactoring validation rules can subtly change which diagnostic is emitted or whether a field is committed.  
  **Mitigation:** Keep each helper's control flow one-to-one with the original block; rely on the extensive existing tests plus new rejection-path tests before archiving.
- **Risk:** Splitting a function into many tiny helpers can reduce readability if overdone.  
  **Mitigation:** Helpers are grouped by semantic field (not by primitive operation), and each helper fits on one screen.
- **Risk:** Cold-path performance might nominally change due to extra function calls.  
  **Mitigation:** Everything is cold path; with LTO the helpers will be inlined. No hot path is touched.

## Migration Plan

Not applicable. This is a source-level refactor with no deployment or data migration steps.
