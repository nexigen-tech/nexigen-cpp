# Tasks: Consolidate the JSON Field-Extraction Vocabulary

## 1. Extend the canonical vocabulary

- [x] 1.1 Add `read_optional_signed<T>` to `include/nexigen/core/detail/json_field.hpp` (nullable signed: `null` → `std::nullopt`, else bounded signed via `to_signed`)

## 2. Migrate the four consumers off `exec::detail::read_*`

- [x] 2.1 Migrate `src/exec/instance_lifecycle.cpp` (`read_string`→`core::detail::read_string`, `read_i32`→`core::detail::read_signed<std::int32_t>`, `read_u64`→`core::detail::read_unsigned<std::uint64_t>`, `read_optional_i32`→`core::detail::read_optional_signed<std::int32_t>`)
- [x] 2.2 Migrate `src/exec/app_suite.cpp` (the six readers → `core::detail`)
- [x] 2.3 Migrate `src/exec/suite_manifest.cpp` (the six readers → `core::detail`)
- [x] 2.4 Migrate `src/hub/hub_format.cpp` (the readers → `core::detail`) and replace its `find`→`is_array` loops with `collect_object_array`

## 3. Remove the duplicate readers

- [x] 3.1 Delete `read_string`, `read_u32`, `read_u64`, `read_i32`, `read_optional_i32`, `read_bool` from `include/nexigen/exec/detail/sys_helpers.hpp`

## 4. Verification

- [x] 4.1 Build with `scripts/build.sh` (BUILD GREEN)
- [x] 4.2 Run `nexigen_app_suite_tests` (324), `nexigen_child_process_tests` (43), `nexigen_exec_tests` (308), `nexigen_hub_tests` (763) — 100% pass
- [x] 4.3 Run `openspec validate --changes strengthen-json-field-vocabulary`

## Follow-up

- [x] Extract the 8 duplicated suite/manifest helpers (`read_log_level`, `log_level_json`, `read_string_array`, `read_cpu_array`, `read_cpu_set`, `read_legacy_execution_manager_cpus`, `read_canonical_thread`, `read_env_object`) from `src/exec/app_suite.cpp` and `src/exec/suite_manifest.cpp` into `include/nexigen/exec/detail/suite_json.hpp` (verbatim, preserving the "reject on non-array" semantics of `read_cpu_array` that `collect_array_into` does not provide).
- [x] Consolidate the void/tolerant scalar and enum readers that were duplicated across files: `read_string_field` (`app/telemetry_layout.cpp`, `logging/logger_options.cpp`), `read_u32_field`/`read_bool_field`/`read_log_level_field` (`logger_options.cpp`, `spam_protection_config.cpp`), `read_string`/`read_uint`/`read_int`/`read_bool` (`sm/sm_manifest.cpp`), `read_bool`/`read_enum` (`metrics/snapshot_format.cpp`), `read_u64_field` (`logger_options.cpp`), and `read_log_level_field`/`read_overflow_policy_field` (`logger_options.cpp`) → `core::detail::read_*` / `core::mandate::read_enum`.

## Remaining (resolved)

- [x] Add the canonical "absent is valid" reader family to `core::detail` (`read_optional_string`, `read_optional_bool`, `read_optional_unsigned<T>`, `read_optional_object`) and `core::mandate::read_optional_enum`; rename `read_optional_signed` → `read_nullable_signed` to disambiguate "nullable" from "absent-ok". Migrate the `try_read_*` family in `src/logging/file_logger_config.cpp` and `src/logging/console_logger_config.cpp`.
- [x] Migrate `metrics/snapshot_format.cpp`'s local `read_unsigned`/`read_signed` templates (void, no range check, name collision) to `core::detail::read_unsigned`/`read_signed`.

## Final consolidation + enforcement

- [x] Add `read_number` (JSON number → double) and `read_counter` (unsigned-or-positive-signed) to `core::detail`; migrate `metrics/snapshot_format.cpp`.
- [x] Migrate the remaining `read_unsigned` duplicates found by the guard in `src/tracing/record_format.cpp` and `src/tracing/span_frame_format.cpp` → `core::detail::read_unsigned`.
- [x] Add `cmake/check_json_field_vocabulary.py` — a `ctest` gate (`nexigen_json_vocabulary_gate`) plus a self-test, enforcing the single-definition-site contract so a new module cannot re-fork the vocabulary.

## Correctly scoped local helpers (not duplication)

- Composite helpers (`logger_options.cpp` `read_core_mask_field`/`read_flush_cpu_affinity_field`/`read_worker_thread_field`, `file_logger_config.cpp` `try_read_throttle_field`/`try_read_rotation_field`, `exec/detail/suite_json.hpp`'s exec-specific readers, `settings_io.hpp`'s diagnostic readers, `metrics/snapshot_format.cpp`'s `read_histogram_*`, `tracing/span_frame_format.cpp`'s `read_*` specials) compose the canonical vocabulary for nested objects and domain-specific shapes; correctly scoped, no cross-file duplication of the core vocabulary.
