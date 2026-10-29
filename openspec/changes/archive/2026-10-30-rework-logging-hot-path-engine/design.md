# Design

## Context

See `proposal.md` — Why. Current state: `nexigen::logging` already ships `detail::Producer` (per-thread SPSC shards over `core::SpscRingBuffer`), `detail::Async_worker` (background `std::jthread` drain), `ConsoleLogger`/`FileLogger`/`NullLogger`/`CompositeLogger`, `LogRecord<MessageCapacity>` (192 bytes default), `Level_control`, and `Throttle_config`. All sizes are compile-time template parameters; overflow is `DropNewest`-only; shard slots never return to the pool; anti-spam is a single windowed throttle in `FileLogger`. The new requirements (runtime `LoggerOptions`, 256-byte `LogRecord`, `OverwriteOldest`, shard lifecycle, two-level anti-spam) cannot be satisfied by editing constants alone — they need new component types and modifications to `Producer`/`LogRecord` without breaking `core::SpscRingBuffer` or the existing `concepts::Logger` surface.

## Goals / Non-Goals

**Goals:**
- Runtime-tunable engine: `LoggerOptions` + `SpamProtectionConfig`, wait-free `set_log_level` / `set_spam_protection_config`.
- `SpscShardRing` with `DropNewest` / `OverwriteOldest`; `core::SpscRingBuffer` untouched.
- `SpscShardPool` with pre-allocated shards, `Draining` lifecycle, pool return, `unregistered_thread_drops_`.
- `LogRecord` 256 bytes, `alignas(64)`, dangling-pointer-safe argument packing (`memcpy` of `string_view`/`const char*` into record-owned `arg_payload_`).
- Two-level anti-spam: repeat suppression + per-shard token bucket + `bypass_min_level`.
- Latency gate `NEXIGEN_LOGGING_STRICT_LATENCY` (≤ 20 ns median accepted call in Release).
- Full test matrix: Debug, Release (`-O3 -march=native -flto`), TSan, ASan — 100% tests passed.

**Non-Goals:**
- Part 2 file-I/O changes (rotation by size, `max_file_size_bytes` enforcement in `File_sink`) — deferred.
- Removing `ConsoleLogger::Config` / `FileLogger_config` in this change; they remain as thin wrappers migrated to `LoggerOptions` in Part 2.
- New sinks, OTLP, network, Windows/macOS.
- Changing `concepts::Logger` signatures (record methods keep returning `void`).

## Decisions

### D1 — `LoggerOptions` + `SpamProtectionConfig` as plain aggregates

Both are plain C++ aggregates with documented defaults; construction is cold path. Validation (power-of-two check for capacity, non-empty `base_dir`, etc.) happens at `initialize()`, not on the hot path. Wait-free reconfiguration uses relaxed atomics: `std::atomic<LogLevel> level_` and `std::atomic<SpamProtectionConfig> spam_` (the struct is trivially copyable, so `store`/`load` are lock-free on x86-64). *Alternative: mutex-guarded reconfiguration — rejected, would block producers on the control plane.*

### D2 — `SpscShardRing` with per-slot sequence counters (Vyukov-style)

Each slot carries `std::atomic<std::uint32_t> seq` plus the `LogRecord` value. Producer writes: load seq with acquire, compare to expected write cursor, `memcpy` record, store seq with release. Consumer reads symmetrically. `DropNewest`: if seq mismatch means full, increment `dropped_count_` and return. `OverwriteOldest`: if full, atomically advance the read index (release) and overwrite, incrementing `dropped_count_`. All index transitions use release/acquire pairs on per-slot seq cells — TSan-clean, no shared write index. `core::SpscRingBuffer` is not modified; its spec and consumers stay unchanged. *Alternative: extend `core::SpscRingBuffer` with overwrite mode — rejected, would break the pure SPSC contract for all core consumers.*

### D3 — `SpscShardPool` pre-allocates at initialization

`SpscShardPool::initialize(max_producer_threads, shard_capacity)` allocates one contiguous block (or `std::vector`-equivalent fixed storage) for all shard rings and their seq cells — cold path, allocation allowed. Steady state: zero allocation. Slot registration: `thread_local` cache of `(logger_ptr, slot)`; first call does one relaxed `fetch_add` on `next_slot` and publishes `active` with release. Pool exhaustion → `unregistered_thread_drops_` (relaxed atomic), record dropped, return immediately. Thread exit: `thread_local` destructor marks shard `Draining`; consumer's drain loop skips `Draining` shards and returns them to a free-slot stack (lock-free Treiber stack on freed indices). *Alternative: never reuse slots (current behavior) — rejected, leaks pool capacity in long-running services with thread churn.*

### D4 — `LogRecord` 256 bytes, `alignas(64)`

New fixed layout: `stamp` (8), `thread_id` (4), `level` (1), `flags` (1), `arg_length` (2), `file_name*` (8), `function_name*` (8), `line` (4), pad (4), `fmt*` (8), `render_fn` (8), `arg_payload_[256 - 56 = 200]` (8-aligned) = 256 bytes total. `std::string_view`/`const char*` slots reserve `2 + inline_cap` bytes (length + inline copy); copy is `memcpy` with truncation flag. Render callback reads from record-owned blob — no aliasing of caller memory. `to_string()`/`std::formatter`/`to_json`/`from_json` per mandates 9–11; no `from_json` for factory-bound types (pinned in tests). *Alternative: keep template `LogRecord<N>` — rejected, runtime capacity from `LoggerOptions` needs a single fixed size; 256 covers the common packs (float + string_view) while staying in 4 cache lines.*

### D5 — Two-level anti-spam

Level 1 (repeat suppression): shard-owned `last_identity_` (hash of `source_location` + fmt pointer + `arg_payload_` bytes) + `repeat_count_` + `repeat_window_start_ns`. On match: increment counter, no ring push. On mismatch or window expiry: push `[SUPPRESSED] Last message repeated N times` if `repeat_count_ > 0`, then the new message. Level 2 (token bucket): shard-owned `tokens_used_` + `window_start_ns`; one relaxed compare per call; `max_messages_per_sec_per_shard == 0` disables. `bypass_min_level` checked before the bucket: records at or above it always pass. Global `max_total_bytes_per_sec` tracked via a shared relaxed counter updated by the consumer; producers read it relaxed (best-effort, never block). *Alternative: per-record lock for global quota — rejected, violates wait-free hot path.*

### D6 — Integration into `Producer`

`detail::Producer` is reworked to compose `SpscShardPool` + `SpamProtection` instead of `std::array<SpscRingBuffer>`. The emit path order stays: level filter → TLS slot → stamp → repeat-suppression check → token-bucket check → pack → ring push. `Dropped_stats` gains `unregistered_thread_drops_` and `throttled_count_`. `ConsoleLogger`/`FileLogger` constructors accept `LoggerOptions` (existing `Config`/`FileLogger_config` map into it). *Alternative: keep `Producer` unchanged and add a parallel engine — rejected, would duplicate the consumer/sink machinery.*

### D7 — Serialization mandates

Every new public type (`LoggerOptions`, `SpamProtectionConfig`, `OverflowPolicy`, `ShardState`, `ShardStats`) implements: member + free `to_string()`, `from_string()` for enums, non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, `std::formatter` specialization. Factory-bound types (logger, pool, ring) get `to_json` only, no `from_json` — pinned by `static_assert` in test TU. *Alternative: full `from_json` for everything — rejected, reconstruction is factory/I/O-bound by design.*

### D8 — Latency gate and benchmark

`NEXIGEN_LOGGING_STRICT_LATENCY` define (Release only) gates a benchmark in `tests/logging/` measuring median accepted-call latency ≤ 20 ns (`-O3 -march=native`). The benchmark excludes consumer work (draining, rendering, file I/O) per the "budget excludes background work" scenario. Debug and sanitizer builds compile out the gate (correctness-only). *Alternative: always-on gate — rejected, sanitizer instrumentation inflates latency and would flake.*

### D9 — Thread exit / Draining lifecycle

`SpscShardPool` registers a `thread_local` destructor per (logger, thread). On thread exit it atomically marks the shard `Draining` (release store on a `ShardState::state_` cell: `Active`/`Draining`/`Free`). The consumer round-robin checks `state_`: `Draining` shards are drained to empty, then transitioned to `Free` and pushed onto a lock-free free-slot stack. A later registration pops from the free stack first, then bumps `next_slot`. `unregistered_thread_drops_` counts threads that never registered because the pool was full. *Alternative: epoch-based reclamation — rejected, over-engineered for thread exit which is already a rare cold event.*

### D10 — Backward compatibility

`core::SpscRingBuffer` and its spec are unchanged. `ConsoleLogger<ShardCapacity, MessageCapacity, MaxThreads>` and `FileLogger<...>` keep their template signatures for existing users; new constructors accepting `LoggerOptions` are added. The old `drop-only` overflow remains the default (`OverflowPolicy::DropNewest`). Existing specs (`logging/multi-producer`, `logging/log-record`) receive delta updates; no requirement is removed.

## Risks / Trade-offs

- [`OverwriteOldest` advances consumer index] → producer touching consumer state breaks pure SPSC; mitigated by per-slot seq counters in `SpscShardRing` (Vyukov-style), TSan-clean by construction.
- [`thread_local` destructor runs after main() exits] → shard may never drain; mitigated by `std::jthread` join in logger destructor (control plane) and documenting that threads should exit before logger destruction.
- [`std::atomic<SpamProtectionConfig>` lock-free] — struct must be trivially copyable and ≤ 16 bytes for lock-free `std::atomic` on x86-64; if larger, fall back to per-field atomics or a seqlock. Verified with `static_assert(std::atomic<SpamProtectionConfig>::is_always_lock_free)`.
- [`max_total_bytes_per_sec` global quota] — best-effort relaxed read by producers; consumer enforces strictly. May briefly exceed under burst; acceptable for a diagnostic quota.
- [`256-byte LogRecord` doubles memory vs 192] — pool memory doubles per shard; pre-allocated at init, acceptable for real-time determinism.

## Migration Plan

Additive where possible: new headers, new types, new `LoggerOptions`-based constructors. Existing `ConsoleLogger::Config` / `FileLogger_config` keep working as thin wrappers until Part 2 migrates them fully. `core::SpscRingBuffer` untouched. Rollback: delete the new headers/sources and revert `Producer`/`LogRecord` edits; no persisted data or on-disk format changes.

## Open Questions

None — all material decisions (in-place engine rework, new shard ring type, pool lifecycle) resolved in design.
