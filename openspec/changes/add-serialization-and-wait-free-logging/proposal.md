# Proposal

## Why

Every public type in nexigen-cpp is currently invisible to diagnostics tooling: there is no uniform way to render a `Timestamp`, `LogRecord`, or config struct as text, no way to round-trip the three project enums (`ErrorCode`, `LogLevel`, `Rotation_schedule`) through JSON or `std::format`, and the logging call API accepts only pre-built `std::string_view` messages while each logger instance is contractually single-producer. Consumers (HIL supervisor tools, config files, log shippers) need machine-readable enum vocabulary and human-readable type dumps, and real deployments need several producer threads to log into one `ConsoleLogger`/`FileLogger` without locks or CAS contention. The project mandates (config context rules 5, 7, 9–11) already describe these contracts; this change brings the code to that mandate.

## What Changes

- **nlohmann::json build integration**: `find_package(nlohmann_json QUIET)` with a FetchContent fallback, linked INTERFACE into `nexigen::core`; `JSON_NOEXCEPTION=1` defined publicly so every TU that sees the JSON headers compiles under the project-wide `-fno-exceptions` mandate.
- **Universal serialization contracts (core, chrono, logging)**: every public class/struct (`SpscRingBuffer`, `Timestamp`, `Stamp_source`, `Civil_time`, `LogRecord`, `Dropped_stats`, `Throttle_config`, `Rotation_config`, `ConsoleLogger::Config`, `FileLogger_config`, `NullLogger`, `ConsoleLogger`, `FileLogger`, `CompositeLogger`) gains a cold-path `std::string to_string() const` plus a free `to_string(const T&)`. Every enum class (`ErrorCode`, `LogLevel`, `Rotation_schedule`) gains `constexpr to_string` (extended where missing), `from_string(std::string_view, Enum&) noexcept -> bool`, ADL `to_json`/`from_json` (strictly non-throwing under `JSON_NOEXCEPTION`), and a `std::formatter<Enum>` specialization so `std::format("{}", enum_val)` and logger format calls work directly.
- **BREAKING — variadic deferred-format logging API**: `concepts::Logger` and all four loggers (`NullLogger`, `ConsoleLogger`, `FileLogger`, `CompositeLogger`) change `log()`/`verbose()`/`debug()`/`info()`/`warning()`/`error()`/`fatal()` from `Status log(level, source_location, string_view)` to `void log(level, FormatWithLocation<Args...>, Args&&...) noexcept`, where `FormatWithLocation` is a `consteval` wrapper capturing `std::format_string<Args...>` + `std::source_location::current()`. Drop outcomes stop being return values and are reported through `dropped()` snapshots and the in-stream drop summary.
- **BREAKING — deferred type-erased formatting in LogRecord**: the producer packs the format-string pointer, a compile-time-generated type-erased format callback, and a byte blob of serialized arguments into the record's fixed inline storage (≤ 20 ns, zero heap); `std::format_to_n` runs on the consumer thread into pre-allocated scratch when writing console lines and JSON records.
- **BREAKING — wait-free multi-producer sharding**: the single-producer contract of every asynchronous logger is replaced by a pre-allocated array of `MaxThreads = 32` per-thread `SpscRingBuffer` shards; each producer thread binds its own shard once via `thread_local` slot registration (wait-free ≤ 20 ns steady-state writes, no shared write index, no CAS), and the background consumer round-robins all registered shards without ever blocking a producer.
- Comprehensive test coverage in the three existing test TUs for `to_string() const`, JSON round-trips (including invalid input under `noexcept`), `std::format("{}", enum)`, the exact call `logger.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);`, source-location capture, and a 4–8-thread producer stress test; verified green under Debug, Release (≤ 20 ns latency gate), TSan, and ASan.

### Performance, latency, and allocation impact

- Producer hot path stays ≤ 20 ns per record: level filter (~1 ns) + cached tid (~1 ns) + TSC stamp (~3 ns) + argument blob pack into inline record storage (a handful of bounded stores/memcpy, ~2–5 ns) + per-shard wait-free `try_push` (~3–5 ns). No new syscalls, no heap, no shared atomic write index; the only atomics a producer touches are inside its own private shard.
- First call per thread pays one-time slot registration (one relaxed `fetch_add` on a shared counter + TLS write) — cold, bounded, off the steady-state path.
- Consumer work grows: formatting moves from "copy already-formatted bytes" to "invoke type-erased callback + `std::format_to_n` into a 4 KiB pre-allocated scratch" per record; worker loops already batch-drain, so console/file write syscalls amortize it.
- Memory: each asynchronous logger grows from one ring to 32 shards. At `ShardCapacity = 128` records × `sizeof(LogRecord) == 192` that is ≈ 768 KiB of ring storage per async logger instance (vs ≈ 768 KiB today at `Capacity = 4096` for `FileLogger`'s default — 8192×192 ≈ 1.5 MiB today → ≤ 1.6 MiB across all 32 shards; per-instance footprint stays the same order and is compile-time adjustable). Scratch buffers add 4 KiB consumer-side, pre-allocated at start.
- nlohmann::json inclusion lengthens compile time of TUs that pull the enum headers (compile-time only; generated code unchanged — `to_json`/`from_json` are cold-path, never on a producer or worker hot path).

## Capabilities

### New Capabilities

- `core/serialization`: project-wide serialization contract — nlohmann::json build integration (`JSON_NOEXCEPTION=1`, FetchContent fallback), the universal `to_string` member/free-function shape for cold diagnostics, and the non-throwing enum `from_json` rule under `-fno-exceptions`.
- `logging/format-api`: the `FormatWithLocation<Args...>` compile-time format-string + source-location wrapper and the argument-packing contract it enables.
- `logging/multi-producer`: per-thread SPSC ring sharding, `thread_local` slot registration, shard-exhaustion drop policy, and lock-free/wait-free consumer drain across shards shared by all asynchronous loggers.

### Modified Capabilities

- `core/result`: `ErrorCode` gains `from_string`, `to_json`/`from_json`, `std::formatter<ErrorCode>` alongside the existing `to_string`.
- `core/spsc-ring-buffer`: `SpscRingBuffer` gains a cold `to_string() const` snapshot diagnostic.
- `chrono/timestamp`: `Timestamp` gains `to_string() const` / free `to_string`.
- `chrono/stamp-source`: `Stamp_source` gains `to_string() const` / free `to_string`.
- `chrono/time-format`: `Civil_time` gains `to_string() const` / free `to_string`.
- `logging/log-level`: `LogLevel` gains `from_string`, `to_json`/`from_json`, `std::formatter<LogLevel>`.
- `logging/logger-concept`: the contract surface changes to `void` variadic format methods with implicit `FormatWithLocation`; the thread-topology contract becomes multi-producer (one shard per producer thread).
- `logging/log-record`: the inline payload becomes a deferred format blob (format-string pointer, type-erased callback, serialized args) instead of an eagerly formatted message; `to_string() const` added.
- `logging/null-logger`: call shape updated to the void variadic API.
- `logging/console-logger`: void variadic API, consumer-side formatting of packed records, multi-shard drain, `Config` `to_string`.
- `logging/file-logger`: void variadic API, JSON lines rendered from packed records on the consumer, multi-shard drain, `Throttle_config`/`Rotation_config`/`FileLogger_config` `to_string`, `Rotation_schedule` `from_string`/JSON/formatter.
- `logging/composite-logger`: void variadic fold-forwarding, `to_string() const`.

## Impact

- **Build**: root `CMakeLists.txt` (nlohmann_json discovery/FetchContent, public `JSON_NOEXCEPTION=1`), `nexigen::core` becomes the carrier of the JSON dependency for all targets.
- **Headers core/chrono**: `include/nexigen/core/result.hpp`, `include/nexigen/core/spsc_ring_buffer.hpp`, `include/nexigen/chrono/{timestamp,stamp_source,time_format}.hpp` gain serialization/formatting declarations (new `<format>`, `<string>`, `<nlohmann/json.hpp>` includes on cold paths only).
- **Headers/src logging**: `log_level.hpp`, `log_record.hpp`, `concepts/logger_concept.hpp`, `null_logger.hpp`, `console_logger.hpp`, `file_logger.hpp`, `composite_logger.hpp`, `detail/producer.hpp` (→ multi-shard), `detail/async_logger.hpp` (drain all shards), `detail/json_writer.hpp` / `src/logging/json_format.cpp` (render packed args consumer-side); new `detail/format_with_location.hpp`.
- **Tests**: `tests/core/core_test.cpp`, `tests/chrono/chrono_test.cpp`, `tests/logging/logging_test.cpp` extended; Release latency gate re-centered on the pack+push path; 4–8-thread stress test added.
- **Downstream consumers**: any code calling `log()/verbose()/…/fatal()` and branching on the returned `Status` must migrate (drop signals move to `dropped()`); code logging from multiple threads to one logger instance is newly supported.
- **Unchanged**: SPSC ring internals themselves (shards *are* `SpscRingBuffer`s), timestamp/stamp calibration paths, rotation policy logic, sanitizer/optimization wiring.
