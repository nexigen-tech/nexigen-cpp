# Tasks

## 1. Leaf enums (settings vocabulary)

- [x] 1.1 Split `enum SettingsSource` out of `settings_vocabulary.hpp` into `include/nexigen/core/settings_source.hpp`; update `settings_vocabulary.hpp` to include it. Verify compilation passes.
- [x] 1.2 Split `struct SettingsPaths` into `include/nexigen/core/settings_paths.hpp`. Verify.
- [x] 1.3 Split `struct SettingsDiagnostic` into `include/nexigen/core/settings_diagnostic.hpp`. Verify. Delete `settings_vocabulary.hpp`.

## 2. settings_io

- [x] 2.1 Split `enum FileRead` out of `settings_io.hpp` into `include/nexigen/core/file_read.hpp`; trim `settings_io.hpp` to `read_text_file` and other utilities. Verify.

## 3. shard_pool

- [x] 3.1 Split `struct ShardPoolState` out of `shard_pool.hpp` into `include/nexigen/core/shard_pool_state.hpp`; trim `shard_pool.hpp` to `class ShardPool`. Verify.

## 4. thread_management — leaf enums

- [x] 4.1 Split `enum SchedulingPolicy` out of `thread_management.hpp` into `include/nexigen/core/scheduling_policy.hpp`. Verify `nexigen_core` usage compiles.
- [x] 4.2 Split `enum ThreadApplyMode` into `include/nexigen/core/thread_apply_mode.hpp`. Verify.

## 5. thread_management — structs/classes

- [x] 5.1 Split `class CpuSet` into `include/nexigen/core/cpu_set.hpp`. Verify.
- [x] 5.2 Split `class ThreadSpec` into `include/nexigen/core/thread_spec.hpp`. Verify.
- [x] 5.3 Split `class ThreadApplyReport` into `include/nexigen/core/thread_apply_report.hpp`. Verify.
- [x] 5.4 Split `struct ThreadStateSnapshot` into `include/nexigen/core/thread_state_snapshot.hpp`. Verify.
- [x] 5.5 Trim `thread_management.hpp` to free-function utility (`apply_thread_spec_self`, `parse_thread_spec_json`, `parse_cpu_set_json`). Verify `nexigen_core` compiles.

## 6. Single-entity confirmation

- [x] 6.1 Confirm `result.hpp`, `spsc_ring_buffer.hpp`, `worker_tuning.hpp`, `fixed_writer.hpp`, `affinity.hpp`, `sink_io.hpp`, `sink_pack.hpp`, `detail/fault_injection.hpp` are already single-entity/utility and need no split. Verify they compile standalone.

## 7. Include-site updates

- [x] 7.1 Update `#include <nexigen/core/...>` sites in `include/nexigen/{chrono,logging,metrics,tracing,diagnostics,runner,hub,app}/` to new per-entity headers. Verify those targets compile.
- [x] 7.2 Update includes in `src/{chrono,logging,metrics,tracing,diagnostics,runner,hub,app}/*.cpp`, `examples/**`, `tests/**`, `tools/**`. Verify full project compiles.

## 8. Verification

- [x] 8.1 Build and run 100% of unit tests under Debug and Release (`-O3 -march=native -flto`). Verify `ctest` reports no failures.