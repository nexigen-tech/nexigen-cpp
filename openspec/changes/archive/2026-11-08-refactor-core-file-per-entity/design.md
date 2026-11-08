# Design

## Context

See `proposal.md` for motivation. The file-per-entity convention is encoded in `openspec/config.yaml` (from the logging refactor). Core is header-only (0 `src/core/*.cpp`), so there is no translation-unit split.

## Target file tree (~20 headers)

```
include/nexigen/core/
├── affinity.hpp                   using-aliases (utility)
├── cpu_set.hpp                    class CpuSet               (from thread_management)
├── scheduling_policy.hpp          enum SchedulingPolicy       (from thread_management)
├── thread_apply_mode.hpp          enum ThreadApplyMode        (from thread_management)
├── thread_spec.hpp                class ThreadSpec            (from thread_management)
├── thread_apply_report.hpp        class ThreadApplyReport     (from thread_management)
├── thread_state_snapshot.hpp      struct ThreadStateSnapshot  (from thread_management)
├── thread_management.hpp          free functions: apply_thread_spec_self,
│                                  parse_thread_spec_json, parse_cpu_set_json (utility)
├── settings_source.hpp            enum SettingsSource         (from settings_vocabulary)
├── settings_paths.hpp             struct SettingsPaths        (from settings_vocabulary)
├── settings_diagnostic.hpp        struct SettingsDiagnostic   (from settings_vocabulary)
├── settings_vocabulary.hpp        deleted (split complete)
├── settings_io.hpp                free functions: read_text_file (utility, FileRead split out)
├── file_read.hpp                  enum FileRead               (from settings_io)
├── shard_pool_state.hpp           struct ShardPoolState       (from shard_pool)
├── shard_pool.hpp                 class ShardPool             (trimmed)
├── spsc_ring_buffer.hpp           class SpscRingBuffer        (already single)
├── serialization_mandate.hpp      mandate concepts + formatter (group utility, kept intact)
├── worker_tuning.hpp              struct WorkerTuning         (already single)
├── result.hpp                     Status/Result/ErrorCode     (already single)
├── fixed_writer.hpp               class FixedWriter           (already single)
├── sink_io.hpp                    free functions              (utility)
├── sink_pack.hpp                  struct SinkPack             (already single)
└── detail/fault_injection.hpp     (already single)
```

## Decisions

### D1 — `serialization_mandate.hpp` kept intact

Contains three `core::mandate::` concepts (`MandateToString`, `MandateFromString`, `MandateEnum`), the `MandateFormatter` struct, and `std::formatter<T>`. These form a closed, mutually dependent group: splitting them would create a circular-include chain (the formatter references the concepts, and the concepts rely on each other). Kept as a group utility per the convention's "cohesive group owned by no single entity" exception.

### D2 — `thread_management.hpp` becomes a free-function utility

After splitting out `CpuSet`, `SchedulingPolicy`, `ThreadApplyMode`, `ThreadSpec`, `ThreadApplyReport`, and `ThreadStateSnapshot`, the file retains only the free functions `apply_thread_spec_self`, `parse_thread_spec_json`, and `parse_cpu_set_json`. These form a "cohesive group owned by no single entity" — kept under the existing name for familiarity.

## Risks / Trade-offs

- **`CpuSet` has free functions** (`from_core_mask`, `parse_cpu_set_json`, `apply_thread_spec_self`) that are used by other modules. The decision to keep them in `thread_management.hpp` (the utility header) means consumers of those functions still include `thread_management.hpp` — but consumers that only need `CpuSet` (the type) now only include `cpu_set.hpp`, breaking the include coupling.
- **Header count grows 13 → ~20**, but each is small and self-describing.