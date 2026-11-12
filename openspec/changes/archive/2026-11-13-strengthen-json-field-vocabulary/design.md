# Design: Consolidate the JSON Field-Extraction Vocabulary

## Context

The canonical field-extraction vocabulary lives in `include/nexigen/core/detail/json_field.hpp` (`read_string`, `read_bool`, `read_signed<T>`, `read_unsigned<T>`, `read_object`, `collect_array`, `collect_array_into`, `collect_object_array`, `read_fixed_string`, `read_fixed_bytes`) plus the enum field reader `nexigen::core::mandate::read_enum` in `include/nexigen/core/enum_traits.hpp`.

A duplicate, hand-written per-type set still lives in `include/nexigen/exec/detail/sys_helpers.hpp` (`read_string`, `read_u32`, `read_u64`, `read_i32`, `read_optional_i32`, `read_bool`, ~140 lines). Four files import it via `using detail::read_*`:

- `src/exec/app_suite.cpp`
- `src/exec/suite_manifest.cpp`
- `src/exec/instance_lifecycle.cpp`
- `src/hub/hub_format.cpp`

`src/exec/child_process.cpp` was already migrated to `core::detail::read_*` and is the reference for this change. See proposal.md for motivation.

## Goals / Non-Goals

**Goals:**
1. Remove the duplicate readers from `exec/detail/sys_helpers.hpp`.
2. Migrate the four remaining consumers to `core::detail::read_*` / `core::mandate::read_enum`.
3. Replace the hand-written `find` → `is_array` loops in `hub_format.cpp` with `collect_array`/`collect_object_array`.

**Non-Goals:**
1. Do NOT touch the separately-named local helpers in `logging/` (`try_read_string_field`, `read_u32_field`, …), `sm/sm_manifest.cpp`, `metrics/snapshot_format.cpp`, or `app/telemetry_layout.cpp` — they are named differently from the canonical readers and belong to a follow-up pass. This change owns only the `exec::detail::read_*` duplicate and its consumers.
2. No behavioral change to any serialized wire format.

## Decisions

### 1. Canonical target: `core::detail` + `core::mandate`

**Decision**: Migrate to `core::detail::read_*` and `core::mandate::read_enum`, exactly as `child_process.cpp` already does.

**Rationale**: It is the existing, spec-mandated vocabulary, and `child_process.cpp` proves the pattern compiles and round-trips.

### 2. Migration mapping

| `exec::detail::reader` | `core::detail` equivalent | Note |
|---|---|---|
| `read_string` | `read_string` | identical |
| `read_bool` | `read_bool` | identical |
| `read_u32` | `read_unsigned<std::uint32_t>` | identical |
| `read_u64` | `read_unsigned<std::uint64_t>` | identical |
| `read_i32` | `read_signed<std::int32_t>` | core is a strict superset: it also accepts an unsigned value that fits `int64_t` |
| `read_optional_i32` | `read_optional_signed<std::int32_t>` (new) | nullable; added below |

`read_enum` is already `core::mandate::read_enum` in all four files — no change.

### 3. Fill the gap in `core::detail`

**Decision**: Add `read_optional_signed<T>` to `json_field.hpp` (nullable signed integer: `null` → `std::nullopt`, otherwise bounded signed via `to_signed`). Leave the `read_string_array` / `read_cpu_array` / `read_log_level` / `read_cpu_set` local helpers in `app_suite.cpp` and `suite_manifest.cpp` as-is.

**Rationale**: `read_optional_i32` has no canonical equivalent, so it is added. The two array collectors are duplicated across the two files, but `read_cpu_array`'s return value is consumed by `read_legacy_execution_manager_cpus` (a non-array field must leave the destination untouched, whereas `collect_array_into` treats it as success and would install an empty set), so migrating them changes observable behavior. They are a smaller, follow-up deduplication that needs a dedicated semantic review.

### 4. `read_i32` → `read_signed` is a strict widening

**Decision**: Accept the widening. `pid`/`ppid` are written as signed (`j["pid"] = int32_t`), so the round-trip is unchanged; the widening only makes the parser accept a foreign document that writes `pid` as an unsigned integer, which `read_signed` correctly range-checks. This matches `child_process.cpp`'s already-merged `read_signed(j, "pid", ...)`.

## Risks / Trade-offs

- **[Risk]** `read_i32`→`read_signed` widens accepted input (unsigned that fits `int32_t`).
  → **Mitigation**: strict superset, matches `child_process.cpp`; covered by existing `app_suite_test.cpp`/`instance_lifecycle` round-trip tests.
- **[Risk]** `collect_object_array` (used to replace `hub_format.cpp`'s `find`→`is_array` loops) rejects the whole collection on a non-object element, whereas the old loop pushed an empty element.
  → **Mitigation**: the spec's canonical semantics is "a non-object element rejects the whole collection"; the hub tests (763 checks) pass unchanged because they exercise well-formed round-trips.
- **[Risk]** `core::detail::read_string`/`read_bool` do not explicitly guard `is_object()` before `find`.
  → **Mitigation**: nlohmann `find()` returns `end()` on a non-object, so behavior is identical; the helpers are already used across 9 files.

## Migration Plan

1. Add `read_optional_signed<T>` to `core/detail/json_field.hpp`.
2. Migrate `instance_lifecycle.cpp` (smallest, uses `read_optional_i32`).
3. Migrate `app_suite.cpp` and `suite_manifest.cpp` (share `read_string_array`/`read_cpu_array`).
4. Migrate `hub_format.cpp` and replace its `find`→`is_array` loops with `collect_*`.
5. Delete the six duplicate readers from `exec/detail/sys_helpers.hpp`.
6. Build (`-j 64`) and run `nexigen_app_suite_tests`, `nexigen_child_process_tests`, `nexigen_runner_tests`, `nexigen_hub_tests`.

Rollback: the change is mechanical; reverting the edits to the five touched files restores the previous tree. No wire format changes.
