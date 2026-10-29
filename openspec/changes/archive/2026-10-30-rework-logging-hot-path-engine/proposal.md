# Proposal

## Why

`nexigen::logging` already has a solid async multi-producer foundation (`detail::Producer` + `detail::Async_worker`, deferred formatting, SPSC sharding, `concepts::Logger`), but its hot-path engine is frozen behind compile-time template parameters (`ShardCapacity`, `MessageCapacity`, `MaxThreads = 32`) and offers only `DropNewest` overflow with no runtime reconfiguration, no anti-spam beyond a simple windowed throttle, and no shard lifecycle (slots never return to the pool). Part 1 of the logging modernization replaces the in-memory hot-path engine with runtime-tunable, cache-line-isolated components while keeping the `Producer → Async_worker` architecture and the `core::SpscRingBuffer` contract intact.

## What Changes

- **New capability `logging/logger-options`**: `LoggerOptions` (single runtime configuration: `log_root_directory`, `app_name`, `initial_log_level`, `max_file_size_bytes`, `spsc_ring_buffer_capacity` rounded up via `std::bit_ceil`, `max_producer_threads`, `overflow_policy`, `flush_cpu_affinity`, `worker_poll_interval_us`, `sync_flush_on_critical`, nested `SpamProtectionConfig`) plus wait-free runtime reconfiguration (`set_log_level`, `set_spam_protection_config`).
- **New capability `logging/spsc-shard-ring`**: `SpscShardRing` with per-slot sequence counters supporting `DropNewest` and `OverwriteOldest` overflow policies, TSan-clean producer/consumer handoff, `dropped_count_` accounting. `core::SpscRingBuffer` is not modified.
- **New capability `logging/spsc-shard-pool`**: `SpscShardPool` with pre-allocated `max_producer_threads` shards allocated once at initialization, `thread_local` slot registration, `Draining` state after thread exit, shard return to the free pool, `unregistered_thread_drops_` counter (no `abort`, no allocation when the pool is exhausted).
- **New capability `logging/spam-protection`**: hot-path Level 1 repeat suppression (message identity = `source_location` + format pointer + `arg_payload_` bytes, `[SUPPRESSED]` summary) and Level 2 per-shard token-bucket rate limiter (`max_messages_per_sec_per_shard`, `max_bytes_per_sec_per_shard`, `max_total_bytes_per_sec`, `bypass_min_level`).
- **Modified capability `logging/log-record`**: `LogRecord` grows to a fixed 256-byte `alignas(64)` layout with an inline `arg_payload_` blob; `std::string_view`/`const char*` arguments are `memcpy`-copied into record-owned storage with truncation flag so the consumer never reads a destroyed producer stack.
- **Modified capability `logging/multi-producer`**: slot exhaustion no longer silently drops via `next_slot` growth; pool exhaustion counts `unregistered_thread_drops_`; completed threads drain their shard (`Draining`) and return it to the pool.

## Capabilities

### New Capabilities

- `logging/logger-options`: unified runtime configuration and wait-free reconfiguration surface for the async logging engine.
- `logging/spsc-shard-ring`: wait-free single-producer/single-consumer ring with `DropNewest`/`OverwriteOldest` overflow policies and per-slot sequence counters.
- `logging/spsc-shard-pool`: pre-allocated per-thread shard pool with runtime sizing, `Draining` lifecycle, and pool-return after thread exit.
- `logging/spam-protection`: two-level hot-path anti-spam (repeat suppression + token-bucket rate limiting) with `bypass_min_level` escape hatch.

### Modified Capabilities

- `logging/log-record`: fixed 256-byte cache-line-aligned record, dangling-pointer-safe argument packing, cold-path `to_string()`/`std::formatter`/`to_json`/`from_json`.
- `logging/multi-producer`: shard pool exhaustion accounting, `Draining` state, pool return, `unregistered_thread_drops_`.

## Impact

- **Headers**: new `include/nexigen/logging/logger_options.hpp`, `include/nexigen/logging/detail/spsc_shard_ring.hpp`, `include/nexigen/logging/detail/spsc_shard_pool.hpp`, `include/nexigen/logging/detail/spam_protection.hpp`; `include/nexigen/logging/log_record.hpp` (256-byte layout); `include/nexigen/logging/detail/producer.hpp` (composes `SpscShardPool` instead of raw `std::array<SpscRingBuffer>`).
- **Sources**: `src/logging/logger_options_format.cpp`, `src/logging/spsc_shard_ring.cpp`, `src/logging/spsc_shard_pool.cpp`, `src/logging/spam_protection.cpp` (cold-path serialization and calibration).
- **Console/File loggers**: adapt `ConsoleLogger`/`FileLogger` to accept `LoggerOptions` (their existing `Config`/`FileLogger_config` remain as thin wrappers or are migrated in Part 2).
- **Tests**: new `tests/logging/` suites for shard ring overflow, pool lifecycle, spam protection, dangling-pointer-safe packing, serialization mandates; latency benchmark gated by `NEXIGEN_LOGGING_STRICT_LATENCY`.
- **Performance / latency / allocations**: zero hot-path impact on the accepted path (one relaxed load level filter, TLS slot lookup, per-shard ring push). `OverwriteOldest` adds one atomic RMW on the overflow path only. All pool memory is pre-allocated at initialization; registration and emit never allocate. `to_string`/`std::string`/`nlohmann::json` stay cold-path only.
- **Dependencies: none new; links `nexigen::core`, `nexigen::chrono`, `nlohmann_json` (FetchContent fallback v3.11.3), `Threads::Threads`.
