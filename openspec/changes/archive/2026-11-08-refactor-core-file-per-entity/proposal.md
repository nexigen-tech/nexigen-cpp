# Proposal

## Why

`nexigen::core` has six files with multiple top-level types living in one header — `thread_management.hpp` alone packs 856 lines containing `CpuSet`, `SchedulingPolicy`, `ThreadApplyMode`, `ThreadSpec`, `ThreadApplyReport`, and `ThreadStateSnapshot`. This hurts navigation and causes include bloat (a test needing only `CpuSet` today pulls in the full thread-management suite). The logging module's file-per-entity refactor already established the convention in `openspec/config.yaml`; this change applies it to core as the second module.

## What Changes

- Split multi-entity headers so each top-level `enum`/`struct`/`class` gets its own file:
  - `thread_management.hpp` (856 lines, 6 types) → `cpu_set.hpp`, `scheduling_policy.hpp`, `thread_apply_mode.hpp`, `thread_spec.hpp`, `thread_apply_report.hpp`, `thread_state_snapshot.hpp`.
  - `settings_vocabulary.hpp` (3 types) → `settings_source.hpp`, `settings_paths.hpp`, `settings_diagnostic.hpp`.
  - `shard_pool.hpp` (2 types) → `shard_pool_state.hpp`, `shard_pool.hpp`.
  - `settings_io.hpp` (2 types) → `file_read.hpp` + `settings_io.hpp` (free-function utility).
  - `serialization_mandate.hpp` — kept as a group utility (tightly coupled mandate concepts + formatter, inseparable without circular deps).
- No namespace changes (all types are already in `nexigen::core` or `nexigen::core::mandate`; no `detail` aliases to promote).
- No `src/core/` changes (core is header-only — 0 cpp files).
- Update `#include` sites across `chrono`, `logging`, `metrics`, `tracing`, `diagnostics`, `runner`, `hub`, `app`, `tests`, `examples`.

No behavior changes: pure header reorganization. All serialization mandates (`to_string`, `std::formatter`, ADL `to_json`/`from_json`) are preserved verbatim.

## Capabilities

Pure refactor — no spec-level behavior changes. `skip_specs: true`.

### New Capabilities

None.

### Modified Capabilities

None.

## Impact

- **Code**: ~13 core headers become ~20; `#include` updates across all modules that consume core types (`CpuSet`, `ThreadSpec`, `SettingsSource`, `SettingsPaths`, `SettingsDiagnostic`, `ShardPool`, `ShardPoolState`, `FileRead`).
- **Dependencies**: include-graph only. No new external dependencies.
- **Performance & memory**: zero impact — header-only module, no code changed, all `alignas(64)` layouts, `constexpr`/`inline` qualifiers, and `std::memory_order` choices preserved.
- **Compatibility**: non-breaking — all public names and namespaces unchanged.