# Tasks

## 1. Configuration and serialization

- [x] 1.1 Create `include/nexigen/logging/logger_options.hpp` with `LoggerOptions` and `SpamProtectionConfig` aggregates, `OverflowPolicy`/`ShardState` enums, default values per spec `logging/logger-options`, and `to_string()`/`from_string()`/`to_json`/`from_json`/`std::formatter` for every public type. Verify: header compiles standalone with `-Werror -fno-exceptions -fno-rtti -std=c++23`.
- [x] 1.2 Create `src/logging/logger_options_format.cpp` with cold-path `to_json`/`from_json` definitions for `LoggerOptions`, `SpamProtectionConfig`, `OverflowPolicy`, `ShardState`. Verify: links into `nexigen_logging` and JSON round-trips under `JSON_NOEXCEPTION=1`.
- [x] 1.3 Add unit tests in `tests/logging/logging_test.cpp` (project convention: one TU per module) covering defaults, `std::bit_ceil` rounding, `from_string`/`to_string` round-trip, JSON parse-back under `-fno-exceptions`. Verify: `ctest -R nexigen_logging_tests` passes in Debug.

## 2. SpscShardRing (DropNewest / OverwriteOldest)

- [x] 2.1 Create `include/nexigen/logging/detail/spsc_shard_ring.hpp` with `SpscShardRing<T, Capacity>`, `OverflowPolicy` template/runtime parameter, `try_push`/`try_pop`, `dropped_count_`, full/empty detection. Verify: `static_assert`s for lock-free atomics, power-of-two capacity, TSan-clean producer/consumer handoff.
- [x] 2.2 Header-only (inline `to_string`); no cold-path cpp needed. Verify: no linker errors.
- [x] 2.3 Add unit tests in `tests/logging/logging_test.cpp` (project convention: one TU per module): `DropNewest` never blocks producer, `OverwriteOldest` is TSan-clean and increments `dropped_count_`, ring-full returns immediately, `try_pop` FIFO order. Verify: passes in Debug, Release, TSan, ASan.

## 3. SpscShardPool (runtime sizing, lifecycle)

- [x] 3.1 Create `include/nexigen/logging/detail/spsc_shard_pool.hpp` with `SpscShardPool`: pre-allocated shards, `thread_local` slot registration, `next_slot` fetch_add, `unregistered_thread_drops_`, `Draining` state, free-slot stack, pool return. Verify: zero steady-state allocation, no `abort` on pool exhaustion.
- [x] 3.2 Add unit tests in `tests/logging/logging_test.cpp` (project convention: one TU per module): `max_producer_threads + 1` threads → `unregistered_thread_drops_` incremented, thread exit → `Draining` → pool return → reusable by new thread, records not lost. Verify: passes in Debug, TSan, ASan.

## 4. LogRecord 256 bytes + dangling-pointer-safe packing

- [x] 4.1 Rework `include/nexigen/logging/log_record.hpp`: rename `args_` → `arg_payload_` for the inline argument blob; `std::string_view`/`const char*` copied via `memcpy` with truncation flag (already in `pack_one`); `to_string()`/`std::formatter`/`to_json`/`from_json` present. Verify: `static_assert` for `alignof(LogRecord) == 64` and `std::is_trivially_copyable_v<LogRecord>`.
- [x] 4.2 Update `include/nexigen/logging/detail/arg_pack.hpp` pack/unpack for `arg_payload_` layout; `render_impl` reads from record-owned blob. Verify: existing `ConsoleLogger`/`FileLogger` still compile against new record.
- [x] 4.3 Add unit tests in `tests/logging/logging_test.cpp`: stack `string_view` destroyed before drain → consumer reads intact copy (ASan-clean), truncation flag on oversized string, fields survive handoff. Verify: passes under ASan.

## 5. Hot-path anti-spam and Producer integration

- [x] 5.1 Create `include/nexigen/logging/detail/spam_protection.hpp` with `SpamProtection` (repeat suppression + per-shard token bucket + `bypass_min_level` + `message_hash`). Verify: wait-free checks (no mutex/spin/CAS loop), bypass level always passes.
- [x] 5.2 Rework `include/nexigen/logging/detail/producer.hpp` to compose `SpamProtection` + `SpamShardState`; emit path: level filter → TLS slot → stamp → throttle → pack → repeat suppression + token bucket → ring push; `set_spam_protection_config` for runtime reconfiguration. Verify: `static_assert(concepts::Logger<...>)` still holds for adapted loggers.
- [x] 5.3 Add unit tests in `tests/logging/logging_test.cpp`: identical messages collapsed, `[SUPPRESSED]` summary on transition, token bucket drops below `bypass_min_level` only, `set_spam_protection_config` changes quotas without stalling. Verify: passes in Debug, Release, TSan, ASan.

## 6. Integration, documentation, and integration gate

- [x] 6.1 `LoggerOptions` struct exists and is fully tested (task 1.3); existing `Config`/`FileLogger_config` remain as thin wrappers. Full `LoggerOptions`-based constructors deferred to Part 2 (I/O integration). Verify: existing examples and tests still build.
- [x] 6.2 Latency benchmark already exists in `test_latency()` gated by `NEXIGEN_LOGGING_STRICT_LATENCY` (≤ 20 ns median accepted call, Release only). Verify: compiles out under Debug/sanitizers, passes under Release.
- [x] 6.3 Update `README.md` with `LoggerOptions` usage snippet and `SpamProtectionConfig` note. Verify: documented command runs and exits 0.
- [x] 6.4 Integration gate: Debug (656 checks, 0 failed), Release (657, 0), TSan (655, 0), ASan (656, 0) — all `nexigen_logging_tests` pass in every mode.
- [x] 6.5 Validate change coherence: `openspec validate "rework-logging-hot-path-engine" --type change --strict` passes; `openspec validate --specs` 36/36 green.

## Workflow follow-up

- Archive the change after review (`openspec archive rework-logging-hot-path-engine`), which applies the `logging/logger-options`, `logging/spsc-shard-ring`, `logging/spsc-shard-pool`, `logging/spam-protection` new specs and the modified `logging/log-record`, `logging/multi-producer` to `openspec/specs/`.
- Verify archived result: new spec files exist under `openspec/specs/logging/`, modified specs contain the updated requirements, change directory moves under `openspec/changes/archive/`.
