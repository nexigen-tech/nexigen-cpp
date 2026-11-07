# Proposal

## Why

Static analysis (`lizard`) shows that cold-path logging configuration deserialization is the densest area of the codebase: `sanitize_document` in `src/logging/logging_settings.cpp` has cyclomatic complexity 47, and the `from_json` overloads in `src/logging/logger_options_format.cpp` range from 27 to 32. These functions are not on any producer/writer hot path, but their size makes reviews, testing, and safe evolution difficult. The formatting rules for every field are duplicated across `LoggerOptions`, `FileLoggerConfig`, `ConsoleLoggerConfig`, and `SpamProtectionConfig`, increasing the risk of inconsistent validation when new fields are added.

## What Changes

- Decompose `sanitize_document` in `src/logging/logging_settings.cpp` into one small helper per field (`log_root_directory`, `initial_log_level`, `max_file_size_bytes`, `spsc_ring_buffer_capacity`, `max_producer_threads`, `message_capacity`, `overflow_policy`, `core_mask`, `flush_cpu_affinity`, `worker_thread`, `worker_poll_interval_us`, `sync_flush_on_critical`, `spam_protection`).
- Decompose `apply_environment` in the same unit into one helper per environment variable.
- Decompose the `from_json` overloads in `src/logging/logger_options_format.cpp` into reusable cold-path field readers (`read_string_field`, `read_u32_field`, `read_u64_field`, `read_bool_field`, `read_log_level_field`, `read_overflow_policy_field`, `read_thread_spec_field`, `read_cpu_set_field`, `read_spam_protection_field`) and reduce each `from_json` to an orchestrator.
- Preserve exact pass/fail behavior, diagnostic messages, and the current merge precedence (global → local → environment). No public API changes.
- Extend existing logging settings unit tests with rejection-path cases for each decomposed helper, exercised through `load_logging_settings` and direct `from_json` round-trips.

## Capabilities

### New Capabilities

*None. This is a pure internal refactor; no observable behavior changes.*

### Modified Capabilities

*None. No spec-level requirements change.*

This change declares `skip_specs: true` because it only reorganizes implementation details that are already covered by the existing `logging/logger-options` spec.

## Impact

- **Affected code:** `src/logging/logging_settings.cpp`, `src/logging/logger_options_format.cpp`, and `tests/logging/logging_test.cpp`.
- **Hot path:** Unchanged. All modified code is cold-path configuration loading.
- **Performance:** No additional allocations or branches on hot paths; helper functions are cold-path only and inline-friendly.
- **Public API:** No changes.
