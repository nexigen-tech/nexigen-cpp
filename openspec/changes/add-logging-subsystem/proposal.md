# Proposal

## Why

`nexigen::core` now ships the three hot-path primitives (`Result`/`Status`, `Timestamp`, `SpscRingBuffer`) but `include/nexigen/logging/` is still an empty skeleton, so every HIL real-time simulation component would otherwise hand-roll its own logging — with the usual failure modes: blocking I/O on the 1 kHz control loop, heap churn, mutex convoying, and lost records silently swallowed on overflow. A real-time platform needs a logger whose hot path is a wait-free, allocation-free, bounded-cycle enqueue (≤ 20 ns median), with all disk I/O, JSON formatting, rotation, and drop accounting pushed onto a dedicated, CPU-pinned background thread.

## What Changes

- Add `include/nexigen/logging/log_level.hpp`: `LogLevel` enum class (`Verbose, Debug, Info, Warning, Error, Fatal, Off`) with `constexpr to_string`, and a shared atomic (`std::memory_order_relaxed`, cache-line padded) level-control policy: `set_level`/`level` plus the ≤ ~1 ns filtered short-circuit every logger must provide.
- Add `include/nexigen/logging/concepts/logger_concept.hpp`: C++23 concept `nexigen::logging::concepts::Logger` in namespace `nexigen::logging::concepts` — requires `set_level`/`level`, `log(LogLevel, std::source_location, std::string_view) noexcept -> core::Status`, the six convenience methods with defaulted `std::source_location::current()`, and `flush()`. Every concrete logger is pinned by `static_assert(concepts::Logger<T>)`.
- Add `include/nexigen/logging/log_record.hpp`: fixed-size, `alignas(64)`, trivially-copyable `LogRecord` (monotonic `Timestamp`, `LogLevel`, cached `SYS_gettid` thread id, `std::source_location` name/line/function fields, inline fixed char message buffer) and the hot-path record-creation pipeline that enqueues via `SpscRingBuffer::try_push`.
- Add four concrete loggers, all zero-virtual-overhead:
  - `null_logger.hpp` — `NullLogger`, a constexpr no-op stub compiling away to nothing.
  - `console_logger.hpp` — `ConsoleLogger`, async wait-free over `core::SpscRingBuffer` with a `std::jthread` writer to stdout (below Warning) / stderr (Warning and above).
  - `file_logger.hpp` — `FileLogger`, async wait-free JSON-lines writer with CPU-core pinning (`pthread_setaffinity_np`), lock-free token-bucket rate limiting with dropped-record counters and automatic drop-summary emission, hitch-free scheduled directory rotation (Hourly/Daily/custom), and nanosecond-accurate UTC ISO-8601 timestamps calibrated `CLOCK_REALTIME`↔`CLOCK_MONOTONIC_RAW` at init.
  - `composite_logger.hpp` — `CompositeLogger<Loggers...>`, variadic composition over `std::tuple` with fold expressions, `get<Index>()`/`get<LoggerType>()` sub-logger access, no virtual dispatch.
- **Modified `core/result`**: append `ErrorCode::RateLimited` to the error catalogue (and its `constexpr to_string` entry) so a throttled record is distinguishable from a ring-full `BufferFull` by the caller and in drop summaries. Purely additive: enumerator order, values, and all existing names are unchanged — **no breaking changes**.
- Add `tests/logging/logging_test.cpp`: zero-dependency (same `NX_CHECK` style as `core_test.cpp`) suite covering concept static_asserts for all four loggers, dynamic level changes, composite sub-configuration, affinity verification via `pthread_getaffinity_np`, rate-limit drop behavior + drop-summary appearance in the file, exact JSON schema round-trip (including Cyrillic UTF-8), zero-allocation windows, directory rotation with producers running, and a Release-only ≤ 20 ns median hot-path enqueue benchmark; suites pass under TSan and under ASan.
- Wire a new `nexigen_logging` library target (public headers + `src/logging/` non-template implementation units) and a `nexigen_logging_tests` CTest executable into CMake, linking `nexigen::core`.

## Capabilities

### New Capabilities

- `logging/log-level`: the `LogLevel` catalogue, its compile-time naming, and the runtime atomic level control with the sub-2 ns short-circuit filtering guarantee.
- `logging/logger-concept`: the C++23 `Logger` concept defining the structural API contract every concrete logger must satisfy, plus the conformance-assertion rule.
- `logging/log-record`: the fixed-size trivially-copyable record layout, cached thread id, monotonic timestamping, and the ≤ 20 ns wait-free, allocation-free producer enqueue guarantee.
- `logging/null-logger`: the zero-cost no-op logger.
- `logging/console-logger`: the asynchronous wait-free stdout/stderr logger with a background `std::jthread` writer.
- `logging/file-logger`: the asynchronous wait-free JSON-lines file logger — core affinity, throttling with drop reporting, hitch-free directory rotation, strict JSON/UTC formatting, and flush semantics.
- `logging/composite-logger`: variadic, virtual-free composition of heterogeneous loggers with compile-time and runtime sub-logger configuration.

### Modified Capabilities

- `core/result`: the error-code catalogue requirement gains the `RateLimited` enumerator (and `to_string` name), needed to distinguish rate-limited drops from buffer-full drops in the logging hot path.

## Impact

- **Code**: seven new public headers under `include/nexigen/logging/` (incl. `concepts/` subdirectory), new `src/logging/*.cpp` non-template implementation units (JSON writer, clock calibration, affinity, rotation, ring drain), `include/nexigen/core/result.hpp` gains one enumerator, `tests/logging/logging_test.cpp` new, `CMakeLists.txt` + `tests/CMakeLists.txt` gain the `nexigen_logging` target and its test registration. `nexigen::metrics` and existing core code are untouched apart from the additive enum.
- **Dependencies**: none added — standard library plus Linux POSIX/GNU system calls already available in glibc (`pthread_setaffinity_np`, `syscall(SYS_gettid)`, `clock_gettime`, `mkdir`/`open`/`write`/`fsync`). The logging target defines `_GNU_SOURCE` (Linux-only) for `cpu_set_t`/`pthread_setaffinity_np`; core keeps `_POSIX_C_SOURCE=200809L`.
- **Build**: new compiled target (unlike header-only `nexigen_core`) because worker-thread bodies, syscalls and JSON formatting do not belong on the hot path; templates and hot-path enqueue functions stay header-inline. Sanitizer options already exist (`NEXIGEN_ENABLE_TSAN`/`NEXIGEN_ENABLE_ASAN`, mutually exclusive).
- **Performance impact**: nothing existing changes. The new hot path is bounded by design: a filtered call is one relaxed atomic load + compare + return (≈ 1 ns, never touching the ring); an accepted call is `clock_gettime(CLOCK_MONOTONIC_RAW)` (vDSO), a `thread_local` cached tid read, a bounded fixed-size `memcpy` of the message into the inline record buffer, and one `try_push` release-store — target median ≤ 20 ns, asserted in the Release benchmark. On overflow or throttling the call returns `BufferFull`/`RateLimited` immediately; it never blocks, spins, yields, sleeps, or takes a lock, even if the consumer thread is stalled, rotating directories, or pinned to another core. Background work (formatting, escaping, `write`/`fsync`) runs entirely on the consumer thread.
- **Memory-allocation impact**: zero runtime heap allocations in both producer and consumer paths — ring storage is the fixed inline `std::array` inside `SpscRingBuffer<LogRecord, N>` (e.g. N = 8192 × ~160 B ≈ 1.3 MiB per async logger, allocated once when the logger's storage is created during initialization, before the real-time phase); JSON scratchpad, path buffers, and the calibration snapshot are fixed-size members allocated at construction; file descriptors are plain ints. Construction may allocate (jthread stacks, ring memory); steady-state logging, draining, rotation, and drop accounting provably do not (asserted by the test TU's arena allocator + `mallinfo2` delta windows).
- **Verified assumptions** (from the current tree): `SpscRingBuffer` is strictly single-producer/single-consumer with `BufferFull`/`BufferEmpty` non-blocking returns; `Timestamp` is a `Result`-producing, 8-byte trivially-copyable CLOCK_MONOTONIC_RAW value; `-fno-exceptions -fno-rtti -Werror`, `alignas(64)` false-sharing discipline, and the zero-dependency `NX_CHECK` harness are the established house style.
