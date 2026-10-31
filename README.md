# nexigen-cpp ⚡

The backbone of the **nexigen** ecosystem — an ultra-high performance C++23 core library designed for hard real-time systems and hardware simulator platforms. Purpose-built for minimum latency execution under **Ubuntu 26.04 LTS** (and modern Linux environments).

## 🚀 Key Architectural Features
* **Real-Time Ready:** Strictly zero-lock (lock-free / wait-free primitives) implementation within critical execution data paths (hot paths).
* **Deep OS Integration:** Direct interaction with low-level Linux system calls (`epoll`, `io_uring`, POSIX signals, and high-resolution timers).
* **Zero-Cost Abstractions:** O3, native architecture deployment (`-march=native`), and Link-Time Optimization (LTO) for bare-metal execution speed.

## 🛠️ Core Components
* Asynchronous lock-free ring-buffer Logger.
* Atomic-based Metrics collection and high-resolution Tracer.
* Heap-only `nexigen::app::Infrastructure` service handling infrastructure bootstrap, OS signal ownership and predictable application lifecycles.

### Running the `nexigen::core` unit tests

Zero-dependency harness (`tests/core/core_test.cpp`, no GTest/Catch2). Tests build by default;
pass `-DNEXIGEN_BUILD_TESTS=OFF` at configure time to skip them entirely.

Release (production flags: `-O3 -march=native` + LTO, `-Werror`):

```bash
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

ThreadSanitizer (data-race check for lock-free queues and atomics):

```bash
cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON
cmake --build build/tsan
ctest --test-dir build/tsan --output-on-failure
```

AddressSanitizer/UBSan (leaks and out-of-bounds check):

```bash
cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

All three modes must report `100% tests passed`. TSan and ASan are mutually exclusive —
enabling both at once is a configure-time error.

### Coverage target

`NEXIGEN_ENABLE_COVERAGE=ON` instruments every target with `--coverage` and adds a
`coverage` custom target (GCC/Clang, unit tests only, no gcovr/lcov required):

```bash
cmake -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_COVERAGE=ON
cmake --build build/coverage --target coverage
```

The target cleans old `.gcda` counters, runs the seven `nexigen_*_tests` binaries and
prints line coverage per module. Add `-DNEXIGEN_COVERAGE_FAIL_UNDER=85` to fail the
target below the requested percentage. Coverage cannot be combined with ASan/TSan.

Current unit-test line coverage is **96.1%** (6 143 / 6 393): core and diagnostics
100%, app 96.2%, logging 96.3%, metrics 96.2%, tracing 95.5%, chrono 91.6%. The
project expectation is **≥95%**, enforced by passing
`-DNEXIGEN_COVERAGE_FAIL_UNDER=95` in CI.

## 📡 `nexigen::core` API

Header-only, C++23, no exceptions/RTTI, no heap allocation on hot paths. Link the
`nexigen::core` (alias of `nexigen_core`) INTERFACE target and include what you use:

* **`<nexigen/core/result.hpp>`** — the error model. `ErrorCode` (`Ok`, `BufferFull`,
  `BufferEmpty`, `InvalidArgument`, `SystemError`, `NotInitialized`, `RateLimited`, `Conflict`),
  `constexpr to_string`/`from_string`, `to_json`/`from_json` (nlohmann::json, non-throwing
  under `JSON_NOEXCEPTION=1`) and `std::formatter<ErrorCode>`, plus
  `Result<T, E = ErrorCode>` / `Status` over `std::expected`. Branch on `has_value()`
  and read via `*res` / `.error()` — never `value()` (it aborts under `-fno-exceptions`).
* **`<nexigen/core/spsc_ring_buffer.hpp>`** — `SpscRingBuffer<T, Capacity>`, a
  wait-free bounded SPSC queue. **Contract:** exactly one producer thread calling
  `try_push` and exactly one consumer thread calling `try_pop`; any other topology
  is outside the contract. Capacity is compile-time and a positive power of two
  (rejected otherwise). `try_push`/`try_pop` each complete in a bounded number of
  steps (1–2 loads + 1 store, no CAS loops) — neither end can stall the other.
  `size()`/`is_empty()`/`is_full()` are *snapshots* for diagnostics, not
  synchronization, and `to_string()` renders a cold-path capacity/occupancy dump. The
  queue is non-copyable and non-movable: give it static or long-lived object storage.

**Serialization contract (all modules):** every public type exposes a cold-path
`std::string to_string() const` + free `to_string(const T&)`; every enum class exposes
constexpr `to_string`/`from_string`, ADL `to_json`/`from_json` and a `std::formatter`
specialization. The build resolves nlohmann::json via `find_package(nlohmann_json QUIET)`
with a FetchContent fallback (pinned v3.12.0) and propagates `JSON_NOEXCEPTION=1` publicly,
so the JSON vocabulary compiles everywhere under `-fno-exceptions`. `std::string` and
nlohmann::json remain strictly cold-path tools — never on a producer or worker hot path.

Usage example (the same setup as the 1M-element stress test in `tests/core/core_test.cpp`):

```cpp
#include <cstdint>
#include <thread>

#include <nexigen/core/spsc_ring_buffer.hpp>

int main() {
    static nexigen::core::SpscRingBuffer<std::uint64_t, 8192> queue{};
    static constexpr std::uint64_t k_count = 1'000'000;
    std::uint64_t sent = 0;
    std::uint64_t received = 0;
    std::uint64_t order_violations = 0;
    std::uint64_t producer_retries = 0;
    std::uint64_t consumer_retries = 0;

    std::jthread producer{[&] {
        while (sent < k_count) {
            if (queue.try_push(sent).has_value()) {
                ++sent;
            } else {
                ++producer_retries; // BufferFull: wait-free, retry immediately
            }
        }
    }};
    std::jthread consumer{[&] {
        std::uint64_t expected = 0;
        while (expected < k_count) {
            std::uint64_t got = 0;
            if (queue.try_pop(got).has_value()) {
                if (got != expected) { ++order_violations; } // strictly increasing, no gaps
                ++expected;
            } else {
                ++consumer_retries; // BufferEmpty: wait-free, retry immediately
            }
        }
        received = expected;
    }};
    producer.join();
    consumer.join();
    return (sent == k_count && received == k_count && order_violations == 0) ? 0 : 1;
}
```

## 📡 `nexigen::chrono` API

Shared time primitives for the whole ecosystem (`include/nexigen/chrono/`, `src/chrono/`,
`tests/chrono/`). Link `nexigen::chrono` (alias of `nexigen_chrono`; it links `nexigen::core`
transitively — the error model stays in core). Hot paths remain inline, allocation-free and
lock-free; the compiled units are cold-path only.

* **`<nexigen/chrono/timestamp.hpp>`** — `Timestamp`, a trivially copyable 8-byte
  nanosecond instant from `clock_gettime(CLOCK_MONOTONIC_RAW)`. Total ordering,
  saturating subtraction, deadline arithmetic. `Timestamp::now()` returns
  `core::Result<Timestamp>` — a failed clock read is `SystemError`, never a silent zero.
* **`<nexigen/chrono/stamp_source.hpp>`** — `Stamp_source`: a producer-owned,
  single-cache-line monotonic nanosecond clock. `calibrate()` (cold, sleeps ~2 ms) samples
  the TSC↔nanosecond scale once on x86-64; the inline `sample()` hot path then costs a
  multiply-shift — no syscall — with a per-source clamp that never lets stamps regress and
  a `Timestamp::now()` fallback on other targets. `sample_utc_offset()` snapshots the
  `CLOCK_REALTIME − CLOCK_MONOTONIC_RAW` offset on cold paths; `utc_from_mono_ns()`
  converts a record stamp to UTC ns on the consumer thread (never `CLOCK_REALTIME` on
  the hot path).
* **`<nexigen/chrono/time_format.hpp>`** — allocation-free UTC civil math: `Civil_time`,
  `days_from_utc_ns`, `civil_from_utc_ns` (Howard Hinnant's civil_from_days, correct for
  the pre-epoch negative domain), and `format_utc_iso8601_ns` rendering exactly
  `YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ` (30 chars + NUL into a caller-owned `char (&)[31]`,
  years clamped so the buffer can never overflow). No `gmtime_r`, no timezone or locale
  state.

## 📡 `nexigen::logging` API

Unified runtime configuration (`LoggerOptions` + `SpamProtectionConfig`) and wait-free anti-spam live in `<nexigen/logging/logger_options.hpp>`; see the API section below for per-sink usage. Hot-path anti-spam (repeat suppression + per-shard token-bucket rate limiting) is opt-in via `set_spam_protection_config()`.

Zero-virtual logging built on the `concepts::Logger` structural contract (`set_level`/`level`,
the variadic `log(LogLevel, FormatWithLocation<Args...>, Args&&...) noexcept -> void` entry
point, the six convenience methods (`verbose`/`debug`/`info`/`warning`/`error`/`fatal`) with
implicit source-location capture, and `flush() -> Status`). Link `nexigen::logging`
(alias of `nexigen_logging`) and include what you use:

* **`<nexigen/logging/format_with_location.hpp>`** — `FormatWithLocation<Args...>`: the
  `consteval` wrapper pairing a compile-time-checked `std::format_string<Args...>` literal
  with the `std::source_location` of the *caller's* statement. Passing a bare literal is the
  normal shape; format/argument mismatches are build errors, not runtime paths.
* **`<nexigen/logging/log_level.hpp>`** — `LogLevel` (`Verbose..Fatal, Off`, declared in
  severity order so relational `<` *is* the ordering), constexpr lowercase `to_string`,
  `from_string`, `to_json`/`from_json` (non-throwing under `JSON_NOEXCEPTION=1`) and a
  `std::formatter<LogLevel>` so `std::format("{}", LogLevel::Warning)` → `"warning"`.
  The runtime threshold is one relaxed-atomic cell per logger: `level()` is a snapshot, and
  a filtered call ends in ≈ 1 ns without touching the ring.
* **`<nexigen/logging/log_record.hpp>`** — `LogRecord<Message_capacity = 128>`: the fixed
  192-byte `alignas(64)` trivially-copyable payload crossing the producer/consumer boundary —
  calibrated monotonic-ns stamp, cached `SYS_gettid` thread id, extracted source-location
  fields, and a **deferred format pack**: the raw literal pointer, a type-erased render
  callback, and serialized arguments in an inline blob (oversized string copies truncate,
  flagged; rendering happens on the consumer thread, never the producer).
* **`<nexigen/logging/logging_settings.hpp>`** — cold-path global/local settings loader:
  `Settings_paths` (explicit file paths; empty disables a source), merged
  `Logging_settings` (`LoggerOptions` + load flags + explicit-capacity flags +
  `Settings_diagnostic` list), `load_logging_settings`, `apply_to(...)` for
  `FileLogger_config`/`ConsoleLogger_config`, and `check_capacity<...>`. The loader
  never runs on a producer path; diagnostics are data, not log calls.
* **`<nexigen/logging/null_logger.hpp>`** — `NullLogger`: the zero-cost stub; at `-O1+`
  nothing survives into the emitted code (objdump-pinned by the tests).
* **`<nexigen/logging/console_logger.hpp>`** — `ConsoleLogger<Shard_capacity = 128, Message_capacity = 128, Max_threads = 32>`:
  asynchronous wait-free plain-text sink; Verbose/Debug/Info → stdout, Warning and above →
  stderr, via raw `::write` (never `std::cout`/`printf`, which allocate). The consumer renders
  each deferred pack into a pre-allocated line scratch.
* **`<nexigen/logging/file_logger.hpp>`** — `FileLogger<Shard_capacity = 128, Message_capacity = 128, Max_threads = 32>`:
  asynchronous wait-free JSON-lines writer with a CPU-pinned worker (`core_mask`), lock-free
  windowed throttling with per-cause drop counters and automatic drop-summary records, and
  hitch-free Hourly/Daily/Custom directory rotation (`Rotation_schedule` carries the full
  enum vocabulary: `to_string`/`from_string`/JSON/`std::formatter`). `max_file_size_bytes`
  adds optional size-based rotation: once the active file would exceed the limit, the
  writer continues in a numbered segment in the same period directory (`app.jsonl` →
  `app.1.jsonl` → `app.2.jsonl`); `0` keeps the previous single-file behaviour. Wall time
  is UTC ISO-8601 with nanoseconds, `CLOCK_REALTIME`↔`CLOCK_MONOTONIC_RAW` calibrated at
  init and re-sampled on rotation/flush.
* **`<nexigen/logging/composite_logger.hpp>`** — `CompositeLogger<Loggers...>`: variadic
  fan-out over a `std::tuple` with fold expressions, `get<Index>()`/`get<LoggerType>()`
  sub-logger access, no virtual dispatch; composites nest and the empty pack is valid.

**Hot-path rules:** each asynchronous logger pre-allocates a pool of `Max_threads` (default 32)
per-thread SPSC shards; **up to 32 distinct producer threads** may log to one instance, and the
first call on a thread registers its shard once (thread-local slot; steady state never touches
a shared write index). An accepted enqueue — filter, stamp, argument pack, shard `try_push` —
measures ≤ 20 ns median with zero allocations (Release-gated; the bench carries float +
`string_view` arguments); a filtered call ≈ 1 ns. Producers never block, spin, lock, or
format, and never notice the consumer or another thread's stall. Threads past the pool capacity
are dropped and counted. Cross-thread `set_level`/`level`/`dropped`/`pending`/`writer_*`
are relaxed **snapshots** — diagnostics and configuration, never synchronization.
`flush()` is a control-plane barrier **outside** the never-block guarantee: it waits until
every record accepted before the call is written across all shards (and `fsync`-ed for the
file sink).

Drop semantics — record methods return `void`; losses surface through the `dropped()`
snapshot and the in-stream drop summary:

| Counter | Meaning |
|---|---|
| `dropped().buffer_full` | the calling thread's shard ring was full; the record was not enqueued |
| `dropped().rate_limited` | throttle budget spent for the window (file sink); the next window opens with a drop-summary record reporting exact per-cause counts |
| `dropped().slot_exhausted` | more distinct producer threads than `Max_threads`; extra threads' records are dropped (never blocking, never corrupting a registered shard) |

Every public type carries the project serialization contract too: cold-path
`std::string to_string() const` + free `to_string(const T&)` for diagnostics (`Timestamp`,
`Stamp_source`, `Civil_time`, `LogRecord`, `Dropped_stats`, the config structs, every logger,
and the ring itself).

Usage example (console + file in a composite, pinned writer, deferred-format calls from two
threads):

```cpp
#include <nexigen/logging/composite_logger.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/log_level.hpp>

#include <thread>

int main() {
    using namespace nexigen::logging;

    // One static composite owns both sinks (static storage: the shard pools are
    // non-movable). initialize() is the pre-real-time phase: it calibrates
    // clocks and starts each logger's background writer thread.
    static CompositeLogger<ConsoleLogger<>, FileLogger<>> app{};

    FileLogger_config file_cfg{};
    file_cfg.app_name = "hil-demo";
    file_cfg.base_dir = "/tmp/hil-logs";
    file_cfg.file_name = "sim.jsonl";
    file_cfg.initial_level = LogLevel::Info;
    file_cfg.rotation.kind = Rotation_schedule::Hourly;
    file_cfg.core_mask = 0x1ULL; // pin the JSON writer thread to core 0

    if (!app.get<ConsoleLogger<>>().initialize({}).has_value()) { return 1; }
    if (!app.get<FileLogger<>>().initialize(file_cfg).has_value()) { return 1; }

    // One set_level call re-thresholds the whole tree...
    app.set_level(LogLevel::Info);
    // ...and per-sink policy stays available through the typed accessor:
    app.get<FileLogger<>>().set_level(LogLevel::Warning); // file keeps Warning+

    // Formats are checked at compile time; args pack into the record here and
    // render on the writer thread — never on this one:
    std::thread sim {[]{
        for (int tick = 0; tick < 1000; ++tick) {
            app.debug("Прямой вызов метода debug. Давление: {:.4f}", 0.0025);
            app.info("Level is {}", LogLevel::Warning); // enums format directly
        }
    }};
    app.error("actuator position drift"); // console + file

    sim.join();

    // Control plane: wait until everything accepted so far is durable
    // (never call this from the real-time producer path).
    if (!app.flush().has_value()) { return 1; }
    return 0;
}
```

### Running the `nexigen::logging` unit tests

All four build modes must report `100% tests passed` (the numeric ≤ 20 ns latency gate is
Release-only; correctness runs everywhere).

Debug (development: `-g`, assertions, no numeric gates):

```bash
cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Release (production flags: `-O3 -march=native` + LTO; enables the latency gates):

```bash
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

ThreadSanitizer (data-race check for the relaxed level/throttle/flush patterns):

```bash
cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON
cmake --build build/tsan
ctest --test-dir build/tsan --output-on-failure
```

AddressSanitizer/UBSan (zero-allocation and fixed-buffer safety proof):

```bash
cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

### `LoggerOptions` and anti-spam configuration

`LoggerOptions` replaces compile-time template parameters with a single runtime configuration structure. Anti-spam is opt-in:

```cpp
#include <nexigen/logging/logger_options.hpp>

nexigen::logging::LoggerOptions opts{};
opts.app_name = "hil-demo";
opts.log_root_directory = "/var/log/hil";
opts.max_producer_threads = 64;
opts.spsc_ring_buffer_capacity = 65536; // rounded up via std::bit_ceil
opts.overflow_policy = nexigen::logging::OverflowPolicy::DropNewest;
opts.spam_protection.enable_repeat_suppression = true;
opts.spam_protection.max_messages_per_sec_per_shard = 100000;
opts.spam_protection.bypass_min_level = nexigen::logging::LogLevel::Fatal;

// to_string / to_json / from_json / std::formatter for all types (mandates 9-11):
std::string diag = opts.to_string();
```

Wait-free runtime reconfiguration: `set_log_level(LogLevel)` and `set_spam_protection_config(SpamProtectionConfig)` publish new thresholds/quotas through relaxed atomics without blocking producers.

### Global and local settings

`<nexigen/logging/logging_settings.hpp>` adds a cold-path loader that turns explicitly
supplied deployment files into `LoggerOptions`. Precedence is deterministic:
built-in defaults → global JSON → local JSON → environment. A field absent from a
higher-priority source inherits the lower-priority value; `app_name` is read only from
the local source (the global file's `app_name` is ignored).

```cpp
#include <nexigen/logging/logging_settings.hpp>

nexigen::logging::Settings_paths paths{
    .global_file = "/etc/nexigen/logging.json",   // optional; empty disables
    .local_file = "hil-runner.json"};              // optional; empty disables

nexigen::logging::Logging_settings settings{};
const nexigen::core::Status loaded =
    nexigen::logging::load_logging_settings(paths, settings);

// Best effort: `settings` is always populated; diagnostics name every problem.
for (const auto& diagnostic : settings.diagnostics) {
    std::fputs(diagnostic.to_string().c_str(), stderr);
}
if (!loaded.has_value()) { /* decide whether to continue */ }

// Apply at initialization (also available: initialize(settings) overloads).
if (!logger.initialize(settings).has_value()) { return 1; }
```

Environment overrides: `NEXIGEN_LOG_LEVEL`, `NEXIGEN_LOG_DIR`, `NEXIGEN_LOG_APP_NAME`,
`NEXIGEN_LOG_MAX_FILE_SIZE_BYTES`, `NEXIGEN_LOG_CPU_AFFINITY` (`0,2-3`),
`NEXIGEN_LOG_RING_CAPACITY`, `NEXIGEN_LOG_MAX_PRODUCER_THREADS`,
`NEXIGEN_LOG_OVERFLOW_POLICY`. Invalid values become diagnostics and leave the previous
value intact; missing files are not errors. Explicitly configured
`spsc_ring_buffer_capacity`/`max_producer_threads` are checked against the logger's
compile-time limits; defaults are never checked. `worker_poll_interval_us`,
`sync_flush_on_critical`, `overflow_policy`, and the repeat-suppression/byte-budget part
of `SpamProtectionConfig` are parsed and merged but not yet enforced by the current
engine — they land with the runtime-sized `Producer` follow-up.

## 📡 `nexigen::metrics` API

Header-only hot paths + compiled cold paths, C++23, no exceptions/RTTI, zero heap
allocation on recording and harvest paths. Link the `nexigen::metrics` (alias of
`nexigen_metrics`) STATIC target — it brings `nexigen_core` and `nexigen_chrono`
transitively — and include what you use. Deep dive with copy-paste recipes:
[`docs/metrics-guide.md`](docs/metrics-guide.md); design context:
[`docs/architecture.md`](docs/architecture.md).

* **`<nexigen/metrics/policy_enums.hpp>`** — the policy vocabulary every instrument
  is templated on: `ConcurrencyPolicy` (`SingleThreaded` / `MultiThreaded`),
  `AccessPolicy` (`Readable` / `WriteOnly`), `ResetPolicy` (`Cumulative` /
  `ResetOnRead`) and `MetricKind`; `std::uint8_t`-backed with the standard enum
  trio (constexpr `to_string`/`from_string`, non-throwing `to_json`/`from_json`,
  `std::formatter`).
* **`<nexigen/metrics/concepts/metric_concept.hpp>`** — `concepts::Metric`
  (`static constexpr kind/concurrency/access/reset` + cold-path `to_string()`) and
  `concepts::ReadableMetric`. Read access is a compile-time member constraint
  (`requires (Access == AccessPolicy::Readable)`): a `WriteOnly` instrument has no
  `read()`/`snapshot()` — only the provider's harvester can observe it.
* **`<nexigen/metrics/counter.hpp>`, `<nexigen/metrics/gauge.hpp>`** — `inc()/add()`
  and `set()/add()/sub()` for `std::int64_t`/`double`. **Contract:** `SingleThreaded`
  uses plain storage and exactly one recording thread; `MultiThreaded` uses 33
  `alignas(64)` cells (32 exclusive `thread_local`-registered shards + one shared
  fallback) with relaxed `fetch_add` — wait-free, exact at any thread count.
* **`<nexigen/metrics/descriptive_statistics.hpp>`** — O(1), allocation-free online
  Welford moments (`count/min/max/mean/std`). `ResetOnRead` windows swap
  double-buffered banks per shard under a seq handshake: every `record()` lands in
  exactly one window. Past 32 recording threads the shared-fallback records are
  dropped and counted in `dropped_samples` — visible, never silent.
* **`<nexigen/metrics/explicit_histogram.hpp>`, `<nexigen/metrics/exponential_histogram.hpp>`** —
  `ExplicitBucketHistogram` (≤ 64 caller bounds, binary-search buckets) and
  `ExponentialHistogram` (scale 0–20, geometric base `2^(2^-scale)`, O(1) bitwise
  bucket mapping, loss-free coarsening for wide ranges). Both report
  `count/sum/min/max` and interpolated `p50/p90/p99/p99.9` snapshots.
* **`<nexigen/metrics/meter.hpp>`, `<nexigen/metrics/metric_provider.hpp>`** — the only
  creation path: `MetricProvider(config, sink)` → `get_meter(name, version)` →
  `create_counter/create_gauge/create_statistics/create_explicit_histogram/create_exponential_histogram`,
  each returning `core::Result<T*>` valid until the provider is destroyed. All
  storage is pre-allocated at construction (one aligned block; zero allocation
  during creation and harvesting). `MetricProvider_config` carries
  `collect_interval` (ns; `0` disables the background thread), `core_mask`
  (0 = inherit; the harvester pins itself with `pthread_setaffinity_np`) and the
  per-kind capacities. Control plane: `collect_now()`, `flush()`,
  `startup_status()`.
* **`<nexigen/metrics/null_metric_sink.hpp>`, `console_metric_sink.hpp`,
  `json_file_metric_sink.hpp`, `composite_metric_sink.hpp>`** — the sink contract
  (`concepts::MetricSink`: `on_batch(const MetricBatchSnapshot&) noexcept` +
  `flush() noexcept`, both returning `core::Status`, no virtuals). `NullMetricSink`,
  `ConsoleMetricSink` (raw `::write` to a fd), `JsonFileMetricSink` (JSONL + UTC
  ISO-8601 stamps) and `CompositeMetricSink<Sinks...>` (tuple fan-out without
  short-circuit; construct with configured members, access via `get<Index>()` /
  `get<SinkType>()`).
* **`<nexigen/metrics/metric_snapshot.hpp>`** — fixed-size, trivially copyable
  snapshot values (`MetricIdentity`, `ScalarValue`, `StatisticsSnapshot`,
  `HistogramSnapshot`, `MetricRecord`, `MetricBatchSnapshot`) with inline
  `char[64]` names — the provider hands these to sinks by pointer, never
  allocating.

Usage example (provider → meter → instruments → windows → snapshot):

```cpp
#include <chrono>
#include <cstdio>
#include <nexigen/metrics/metric_provider.hpp>
#include <nexigen/metrics/null_metric_sink.hpp>   // or any sink

namespace metrics = nexigen::metrics;

int main() {
    metrics::NullMetricSink sink;                  // cold path, may allocate
    metrics::MetricProvider_config config{};
    config.collect_interval = std::chrono::nanoseconds{50'000'000}; // 50 ms
    metrics::MetricProvider provider{config, sink};

    const auto meter = provider.get_meter("engine", "1.2.0");
    if (!meter) { return 1; }

    using metrics::ConcurrencyPolicy::MultiThreaded;
    using metrics::AccessPolicy::Readable;         // WriteOnly: no read()/snapshot()
    using metrics::ResetPolicy::ResetOnRead;

    const auto latency = (*meter)->create_statistics<MultiThreaded, Readable>(
        "latency");
    const auto ticks = (*meter)
        ->create_counter<MultiThreaded, Readable, ResetOnRead>("ticks");
    if (!latency || !ticks) { return 1; }

    // Wait-free, allocation-free hot path.
    for (int i = 0; i < 1000; ++i) {
        (*latency)->record(12.5 + (i % 100));
        (*ticks)->inc();
    }

    // Cold path: deterministic snapshots of the current window.
    const metrics::StatisticsSnapshot summary = (*latency)->read();
    const std::uint64_t delta = (*ticks)->read();   // window delta, resets
    std::printf("count=%llu mean=%.3f window_delta=%llu\n",
                static_cast<unsigned long long>(summary.count), summary.mean,
                static_cast<unsigned long long>(delta));
}
```

**Hot-path rules:** never call `to_string()`, `std::string`, `nlohmann::json` or
`std::format` from a recording path or a sink callback — they are diagnostics and
may allocate; branch on `Result` with `has_value()` / `*res` / `.error()`, never
`.value()`. Single-threaded instruments must not be harvested concurrently with
recording: use `collect_now()` on the recording thread or `MultiThreaded` (the
safe default when a background harvester runs). The Release build enables the
`NEXIGEN_METRICS_STRICT_LATENCY` median gates for the benchmark suites; Debug and
sanitizer builds are correctness-only.

## 📡 `nexigen::app` API

Application infrastructure service for real-time programs: one heap-owned object manages the
logger, the metric/tracing sinks and providers, loads per-subsystem settings, watches
`SIGINT`/`SIGTERM`, and flushes everything on the way out. Applications **own** it; they do
not inherit from it. Link `nexigen::app` (alias of `nexigen_app`) — it brings logging,
metrics, tracing and chrono transitively.

`Infrastructure<Policy, Tracer_capacity = 1024, Tracer_threads = 32>` is heap-only: the
constructor is private and `make_infrastructure<Policy>()` returns
`core::Result<std::unique_ptr<Infrastructure<...>>>`. A stateless `Policy` supplies the
component types and the in-place creation hooks:

```cpp
struct My_policy {
    using Logger = My_logger;
    using Metrics_sink = My_metrics_sink;
    using Trace_sink = My_trace_sink;

    static nexigen::core::Status emplace_logger(
        std::optional<Logger>&, const nexigen::logging::Logging_settings&) noexcept;
    static nexigen::core::Status emplace_metrics_sink(
        std::optional<Metrics_sink>&, const nexigen::app::Metrics_settings&) noexcept;
    static nexigen::core::Status emplace_trace_sink(
        std::optional<Trace_sink>&, const nexigen::app::Tracing_settings&) noexcept;
};
```

Lifecycle and accessors:

```cpp
nexigen::app::App_settings_paths paths{};   // logging / metrics / tracing: global + local files
nexigen::app::App_settings settings{};
if (!nexigen::app::load_app_settings(paths, settings).has_value()) { return 1; }

auto created = nexigen::app::make_infrastructure<My_policy>();
if (!created.has_value()) { return 1; }      // SystemError on allocation failure
auto infra = std::move(*created);

if (!infra->initialize(settings).has_value()) { return 1; }
infra->logger().info("started");                     // configured logger
const auto meter = infra->meter("sim", "1.0");       // MetricProvider::get_meter
const auto tracer = infra->tracer_for("sim.loop");   // TracerProvider::get_tracer
const auto reason = infra->run([] { /* one tick */ });
static_cast<void>(infra->shutdown());                // logs reason, flushes all three
```

* `initialize(settings)` claims the **process-global signal ownership** first; a second
  initialized `Infrastructure` (any policy) receives `ErrorCode::Conflict`. It then blocks
  `SIGINT`/`SIGTERM`, runs the policy hooks, constructs the providers and starts the
  `signalfd` watcher.
* `run(tick)` loops until `stop_requested()`; a `bool` tick returning `false` requests
  `Infrastructure_stop_reason::Requested`.
* SIGINT/SIGTERM become `Infrastructure_stop_reason::Sigint`/`Sigterm`; shutdown logs exactly
  one `"shutdown reason: ..."` record.
* `shutdown()` is idempotent, flushes tracer → metrics → logger, restores the signal mask and
  releases signal ownership. The destructor calls it.
* There is no `current()` accessor: pass the `Infrastructure&` (or the logger/provider
  references) to code that needs it.
* Settings are explicit per-subsystem global/local pairs (`App_settings_paths`), merged
  defaults → global → local → environment with the same best-effort diagnostics as logging.

### Command-line settings

`include/nexigen/app/command_line.hpp` provides a cold-path, boost::program_options-style
parser for the two settings directories and the initial log level:

| Option | Meaning |
| --- | --- |
| `-gs, --global-settings <absolute-dir>` | global settings directory; must be an absolute path |
| `-ls, --local-settings <relative-dir>` | local settings directory; must be a relative path |
| `-lgl, --log-level <level>` | `verbose`, `debug`, `info`, `warning`, `error`, `fatal`, or `off` |
| `-h, --help` | print usage; short-circuits every other argument |

Both directories use the same fixed file names: `logging.json`, `metrics.json`,
`tracing.json`, and `chrono.json`. Omitting a directory disables that source; supplying an
empty value, a relative global directory, or an absolute local directory is
`ErrorCode::InvalidArgument`. **At least one** of `--global-settings` or `--local-settings`
must be supplied unless `--help` is present. If neither is given, parsing fails with a
diagnostic stating that one settings directory is required; the application prints the
diagnostic and usage text to stderr and exits with a non-zero status instead of loading
built-in defaults.

Accepted value forms are `--name value`, `--name=value`, `-name value`, `-namevalue`, and
`-name=value` (for example `-gs/etc/nexigen`, `-lgl=warning`). A standalone `--` ends option
parsing; positional arguments are rejected. Use `--name=value` when a value starts with `-`.

The CLI log level has the highest precedence: `apply_to` overwrites
`logging.options.initial_log_level` after the global → local → environment load. Without
`--log-level`, the loaded value is preserved.

Minimal startup flow:

```cpp
#include <cstdio>
#include <string_view>

#include <nexigen/app/command_line.hpp>
#include <nexigen/app/infrastructure.hpp>

int main(int argc, char** argv) {
    const auto parsed = nexigen::app::parse_command_line(argc, argv);
    if (!parsed.has_value()) {
        for (const auto& diagnostic : parsed.error().diagnostics) {
            std::fprintf(stderr, "%s: %s\n", diagnostic.option.c_str(),
                         diagnostic.message.c_str());
        }
        const std::string_view usage = nexigen::app::command_line_usage();
        std::fwrite(usage.data(), 1, usage.size(), stderr);
        return 2; // usage error
    }
    if (parsed->help) {
        const std::string_view usage = nexigen::app::command_line_usage();
        std::fwrite(usage.data(), 1, usage.size(), stdout);
        return 0;
    }

    auto created = nexigen::app::make_infrastructure<My_policy>();
    if (!created.has_value()) {
        return 1; // allocation failure
    }
    auto& infra = *created;

    nexigen::app::App_settings loaded{};
    if (!infra->initialize(*parsed, loaded).has_value()) {
        // Fail fast: loaded still holds best-effort settings and diagnostics.
        // Inspect them, or explicitly call infra->initialize(loaded) for a
        // degraded startup.
        return 1;
    }
    // ... run the application ...
    static_cast<void>(infra->shutdown());
    return 0;
}
```

`Infrastructure::initialize(options, loaded)` maps the directories, loads all four
subsystems, applies the CLI level, and delegates to the existing
`initialize(const App_settings&)` only when loading succeeds. A loader failure is fail-fast:
no signal ownership is claimed and no component is created. Help is rejected as an
initialization input.

The equivalent manual sequence is:

```cpp
const auto paths = nexigen::app::make_app_settings_paths(*parsed);
nexigen::app::App_settings settings{};
const auto load_status = nexigen::app::load_app_settings(paths, settings);
nexigen::app::apply_to(*parsed, settings); // CLI level wins even after a load error
```

`load_app_settings(*parsed, settings)` is a convenience that performs the same sequence and
rejects a manually built options value when both directories are empty.

## 📡 `nexigen::diagnostics` API

Interval timing for real-time loops: the cache-line-aligned `Stopwatch` (O(1)
lap/split tracking, zero-gap `restart()`, multi-unit elapsed queries) and the RAII
`ScopedStopwatch` scope guard with budget-overrun checks. Hot paths are `noexcept`,
zero-allocation, lock-free and atomic-free; only `to_string()` diagnostics compile
cold. Link the `nexigen::diagnostics` (alias of `nexigen_diagnostics`) STATIC
target — it brings `nexigen_core` and `nexigen_chrono` transitively — and include
what you use.

```cpp
#include <nexigen/diagnostics/scoped_stopwatch.hpp>
#include <nexigen/diagnostics/stopwatch.hpp>
#include <cstdio>

int main() {
    using namespace nexigen::diagnostics;

    // Split timing: lap() measures one stage without stopping the run,
    // the total keeps counting the whole loop.
    Stopwatch stages{};
    stages.start();
    // ... stage work ...
    const std::uint64_t stage_ns = stages.lap();

    // Scope budget: the guard reports the scope's elapsed exactly once at
    // destruction, has_exceeded_budget() checks mid-scope without reporting.
    std::uint64_t tick_ns = 0;
    {
        ScopedStopwatch guard{tick_ns, /*budget_ns=*/40'000};
        // ... bounded tick body ...
        if (guard.has_exceeded_budget()) { /* overrun: take corrective action */ }
    }
    std::printf("stage=%llu ns tick=%llu ns\n",
                static_cast<unsigned long long>(stage_ns),
                static_cast<unsigned long long>(tick_ns));
}
```

Both types take an optional `nexigen::chrono::Stamp_source*` for calibrated-TSC
sampling (~3–5 ns per sample vs ~20–25 ns for the default `Timestamp::now()`)
and render cold-path diagnostics through `to_string()` / `std::format`. The
Release build enables the `NEXIGEN_DIAGNOSTICS_STRICT_LATENCY` median gates
(`start()` + `stop()` ≤ 100 ns default clock, ≤ 20 ns calibrated; guard
construct + destruct ≤ 20 ns calibrated); Debug and sanitizer builds are
correctness-only.

## 🧪 Runnable Examples

Self-contained executables demonstrating the public API end to end. They build by
default (`NEXIGEN_BUILD_EXAMPLES=ON`); pass `-DNEXIGEN_BUILD_EXAMPLES=OFF` at configure
time to exclude them entirely.

```bash
cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
```

Run them directly from the build tree (the logging examples write to
`/tmp/nexigen-examples-logs/`, the settings loader examples to
`/tmp/nexigen-examples-logging-settings/` and
`/tmp/nexigen-examples-logging-settings-simple/`; the metrics pipeline example writes to
`/tmp/nexigen-examples-metrics/`; the tracing example writes to
`/tmp/nexigen-examples-tracing/`). The command-line settings example is the exception:
it writes `./nexigen-example-settings/` and `./nexigen-example-output/` relative to the
current working directory, so run it from the repository root (the runner script below
does this for you):

```bash
./build/debug/examples/core/ex_spsc_ring_buffer
./build/debug/examples/logging/ex_console_logger
./build/debug/examples/logging/ex_file_logger
./build/debug/examples/logging/ex_composite_logger
./build/debug/examples/logging/ex_logging_settings
./build/debug/examples/logging/ex_logging_settings_simple
./build/debug/examples/metrics/ex_counters_and_gauges
./build/debug/examples/metrics/ex_descriptive_statistics
./build/debug/examples/metrics/ex_histograms
./build/debug/examples/metrics/ex_metric_provider_and_sinks
./build/debug/examples/tracing/ex_nested_and_cross_thread_spans
./build/debug/examples/tracing/ex_perfetto_chrome_trace
./build/debug/examples/diagnostics/ex_stopwatch
./build/debug/examples/diagnostics/ex_stopwatch_histogram
./build/debug/examples/app/ex_app_lifecycle
./build/debug/examples/app/ex_app_cli_settings
```

* **`ex_spsc_ring_buffer`** — a two-thread telemetry pipeline over
  `SpscRingBuffer<TelemetryPacket, 1024>`: wait-free `try_push`/`try_pop` with classified
  `BufferFull`/`BufferEmpty` outcomes (`has_value()` / `.error()`, never `.value()`),
  direct `std::format` output of `ErrorCode`, and a final `to_string()` queue summary.
* **`ex_console_logger`** — `ConsoleLogger<>` with several `std::jthread` producers across
  all six severities, runtime `set_level()` filtering, direct `LogLevel` formatting,
  `flush()`, and `dropped()` / `to_string()` diagnostics.
* **`ex_file_logger`** — `FileLogger<>` writing JSON lines with hourly rotation, CPU core
  pinning, a throttle budget, and a UTF-8 Cyrillic message; the example then reads the
  generated JSONL file back and verifies it.
* **`ex_composite_logger`** — `CompositeLogger<ConsoleLogger<>, FileLogger<>, NullLogger>`
  with global `set_level()` fan-out, per-sink configuration through `get<Type>()` /
  `get<Index>()`, multi-threaded logging, and one unified `flush()`.
* **`ex_logging_settings`** — the global/local settings pipeline: writes a temporary
  global/local JSON pair, applies `NEXIGEN_LOG_LEVEL`/`NEXIGEN_LOG_CPU_AFFINITY`
  overrides, loads the merged `Logging_settings`, initializes a `FileLogger` through
  `initialize(settings)`, flushes, and verifies the produced JSONL (level, app name,
  record count) on the cold path.
* **`ex_logging_settings_simple`** — the minimal version of the same flow: writes one
  small global JSON and one small local JSON, loads them through explicit
  `Settings_paths` (no environment overrides), initializes a `FileLogger` from the
  merged settings, logs a couple of records, flushes, and exits 0. Failures print
  the loader diagnostics and return a non-zero status.
* **`ex_counters_and_gauges`** — `MetricProvider`/`Meter` factory hierarchy with
  `SingleThreaded` vs `MultiThreaded` `Counter` and `Gauge` (`std::int64_t` + `double`),
  `Readable` `read()`/`snapshot()` vs `WriteOnly` worker-updated instruments harvested by
  the provider, `Cumulative` vs `ResetOnRead` delta windows, and `to_string()` diagnostics.
* **`ex_descriptive_statistics`** — a sensor/control loop recording measurements into
  `DescriptiveStatistics`: the Welford fixture pinned exactly (count/min/max/mean/std),
  visible non-finite rejection, `Cumulative` all-time vs `ResetOnRead` windowed statistics
  polled between ticks with multiple producers (exactly-once windows), and `to_string()` /
  `std::format` output with policy enums.
* **`ex_histograms`** — simulated task durations measured per-producer with
  `nexigen::chrono::Stamp_source` and recorded into both families: an
  `ExplicitBucketHistogram` with latency bounds `{10, 25, 50, 100, 250, 500, 1000}` ns and
  an `ExponentialHistogram` with automatic base-2 mapping across wide dynamic ranges;
  reports `count`/`sum`/`min`/`max` and `p50`/`p90`/`p99`/`p99.9` for both.
* **`ex_metric_provider_and_sinks`** — an end-to-end HIL telemetry pipeline: a
  `MetricProvider` with a 50 ms `collect_interval` and a CPU `core_mask` pinning the
  background harvester, a `CompositeMetricSink<ConsoleMetricSink, JsonFileMetricSink,
  NullMetricSink>` over configured members, concurrent producers across every instrument
  kind, and cold-path read-back verification of the JSONL batches written to
  `/tmp/nexigen-examples-metrics/telemetry_<pid>.jsonl`.
* **`ex_nested_and_cross_thread_spans`** — a `TracerProvider` with a console trace
  sink recording nested same-thread spans, a live span's context handed to a
  `std::jthread` worker whose explicit-parent child (and its own local grandchild)
  link across the thread boundary, the drained timeline printed to the console, and
  self-verification of every parent-child edge before exit (a deliberately broken
  check path is available via `--fail-demo`).
* **`ex_perfetto_chrome_trace`** — a `TracerProvider` with a `ChromeTraceSink`
  recording a nested and concurrent span workload, flush plus shutdown drain, and a
  sanity-checked Perfetto-loadable trace file at
  `/tmp/nexigen-examples-tracing/trace_<pid>.json` — open it at
  [ui.perfetto.dev](https://ui.perfetto.dev) to visualize the execution timeline.
* **`ex_stopwatch`** — the diagnostic stopwatch end to end: per-tick budget checks
  with `ScopedStopwatch` and `has_exceeded_budget()` against a calibrated budget
  (with an over-budget run summary), multi-stage `lap()` profiling printing each
  stage's duration with `lap_count()` / `last_lap_ns()` and the min/max endpoints
  plus the total vs the sum of stage laps, a `restart()` round closing a measured
  interval into a fresh one, and an out-argument guard writing a scope's elapsed
  into a caller's `std::uint64_t&`.
* **`ex_stopwatch_histogram`** — the cross-module pipeline from measurement to
  telemetry: a calibrated `Stamp_source` clocks two `ScopedStopwatch` probes whose
  light/heavy envelope derives strictly increasing geometric bucket bounds, then
  every measured tick runs inside a `ScopedStopwatch` whose sink records the elapsed
  nanoseconds straight into an `ExplicitBucketHistogram` created through the
  `MetricProvider`/`Meter` factory (a whole-run `Stopwatch` covers the phase). The
  console report prints count/sum/min/max, `p50`/`p90`/`p99`/`p99.9`, a per-bucket
  table with ASCII bars, and the cold-path `HistogramSnapshot::to_string()`
  diagnostic; the example self-verifies one sample per tick, bucket counts summing
  to the total, ordered percentiles and samples inside the whole-run measurement.
* **`ex_app_lifecycle`** — the minimal `Infrastructure` flow in one direct `main`:
  an inline stateless `Simple_policy` (console logger, metric sink and trace sink),
  `make_infrastructure<Simple_policy>()`, `initialize(App_settings{})`, a log line,
  a counter created through `meter()`, a span through `tracer_for()`, three ticks,
  `request_stop(Requested)`, and `shutdown()` — no application class, no settings
  files and no signal helper.
* **`ex_app_cli_settings`** — the command-line settings flow: prepares
  `./nexigen-example-settings/` with `logging.json`, `metrics.json`, `tracing.json` and
  `chrono.json` and clears `./nexigen-example-output/` before parsing
  `-gs/--global-settings`, `-ls/--local-settings`, `-lgl/--log-level` and `-h/--help`,
  initializes `Infrastructure` through `initialize(options, loaded)` with file-backed
  sinks, runs a bounded tick loop (counter, gauge, span, periodic logs and collections),
  and verifies that `app.jsonl`, `metrics.jsonl`, `traces.jsonl` and `chrono.jsonl` are
  non-empty.

### Command-line settings example: runner and cleanup

`examples/app/run_ex_app_cli_settings.sh` locates (or builds) `ex_app_cli_settings`,
runs it from the repository root under a 60-second `timeout`, and forwards extra
arguments plus the example's exit status:

```bash
./examples/app/run_ex_app_cli_settings.sh
./examples/app/run_ex_app_cli_settings.sh --log-level debug
```

The example generates:

- `./nexigen-example-settings/logging.json`, `metrics.json`, `tracing.json` and
  `chrono.json` — local settings written before the command line is applied.
- `./nexigen-example-output/app.jsonl`, `metrics.jsonl`, `traces.jsonl` and
  `chrono.jsonl` — log records, metric batches, span records and chrono snapshots. The
  output directory is cleared on every run.

`examples/app/clean_ex_app_cli_settings.sh` removes both directories and succeeds when
they are already absent:

```bash
./examples/app/clean_ex_app_cli_settings.sh
```

Set `BUILD_DIR` to point the runner at a non-default build tree.

Every example is also registered as a CTest smoke test (label `examples`): each must run
to completion and exit with status 0.

```bash
ctest --test-dir build/debug -L examples --output-on-failure
```

## 📄 License
Distributed under the **MIT License**. Created by Vasily Kovrigin.
