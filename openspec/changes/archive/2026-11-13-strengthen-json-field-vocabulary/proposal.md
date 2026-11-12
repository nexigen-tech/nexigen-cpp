# Proposal: Strengthen the JSON Field-Extraction Vocabulary Rule

## Why

The `core/serialization` spec already forbids hand-written `find` → `is_<type>` → `get` chains, but the rule does not name the canonical helpers, and the codebase still carries a duplicate per-type reader set in `exec/detail/sys_helpers.hpp` that ~15 files import (nine of them import both vocabularies at once). Naming the helpers makes the rule enforceable and stops new modules from re-forking the vocabulary.

## What Changes

- Strengthen the `core/serialization` requirement «Manifest and config deserialization uses a shared field vocabulary» to name the canonical helper locations: the field readers in `nexigen::core::detail` (`read_string`, `read_bool`, `read_signed<T>`, `read_unsigned<T>`, `read_object`, `collect_array`, `collect_object_array`, `read_fixed_string`, `read_fixed_bytes`) and the enum field reader `nexigen::core::mandate::read_enum`.
- Tighten the migration trigger: the duplicate per-type readers in `nexigen::exec::detail::sys_helpers.hpp` SHALL be removed and their call sites migrated to the `nexigen::core` vocabulary, rather than waiting for an unrelated refactor.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `core/serialization`: the «Manifest and config deserialization uses a shared field vocabulary» requirement gains the canonical helper names and a concrete consolidation trigger.

## Impact

### Affected code (follow-up implementation, not this change)
- `include/nexigen/exec/detail/sys_helpers.hpp` — remove the duplicate readers (`read_string`, `read_u32`, `read_u64`, `read_i32`, `read_optional_i32`, `read_bool`).
- `src/exec/{app_suite,child_process,execution_manager,instance_lifecycle,suite_control,suite_manifest}.cpp`, `src/{com,hub,logging,metrics,phm,tracing}/...` — migrate to `nexigen::core::detail` readers.
- `src/hub/hub_format.cpp`, `src/app/command_line.cpp`, `src/chrono/snapshot.cpp`, `src/logging/{logger_options,file_logger_config}.cpp` — replace remaining manual `find`/`is_*` chains.

### Performance
- No hot-path impact: the vocabulary is cold-path only (configuration/manifest parsing), already `noexcept` and allocation-bounded.

### Breaking changes
- **None** at the API level; the `exec::detail::read_*` symbols are internal detail and may be removed as part of the consolidation.
