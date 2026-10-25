# nexigen-cpp ⚡

The backbone of the **nexigen** ecosystem — an ultra-high performance C++20 core library designed for hard real-time systems and hardware simulator platforms. Purpose-built for minimum latency execution under **Ubuntu 26.04 LTS** (and modern Linux environments).

## 🚀 Key Architectural Features
* **Real-Time Ready:** Strictly zero-lock (lock-free / wait-free primitives) implementation within critical execution data paths (hot paths).
* **Deep OS Integration:** Direct interaction with low-level Linux system calls (`epoll`, `io_uring`, POSIX signals, and high-resolution timers).
* **Zero-Cost Abstractions:** O3, native architecture deployment (`-march=native`), and Link-Time Optimization (LTO) for bare-metal execution speed.

## 🛠️ Core Components
* Asynchronous lock-free ring-buffer Logger.
* Atomic-based Metrics collection and high-resolution Tracer.
* Base `AppClass` framework handling OS signal intercepts and predictable application lifecycles.

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

## 📡 `nexigen::core` API

Header-only, C++23, no exceptions/RTTI, no heap allocation on hot paths. Link the
`nexigen::core` (alias of `nexigen_core`) INTERFACE target and include what you use:

* **`<nexigen/core/result.hpp>`** — the error model. `ErrorCode` (`Ok`, `BufferFull`,
  `BufferEmpty`, `InvalidArgument`, `SystemError`, `NotInitialized`), `constexpr to_string`,
  and `Result<T, E = ErrorCode>` / `Status` over `std::expected`. Branch on `has_value()`
  and read via `*res` / `.error()` — never `value()` (it aborts under `-fno-exceptions`).
* **`<nexigen/core/timestamp.hpp>`** — `Timestamp`, a trivially copyable 8-byte
  nanosecond instant from `clock_gettime(CLOCK_MONOTONIC_RAW)`. Total ordering,
  saturating subtraction, deadline arithmetic. `Timestamp::now()` returns
  `Result<Timestamp>` — a failed clock read is `SystemError`, never a silent zero.
* **`<nexigen/core/spsc_ring_buffer.hpp>`** — `SpscRingBuffer<T, Capacity>`, a
  wait-free bounded SPSC queue. **Contract:** exactly one producer thread calling
  `try_push` and exactly one consumer thread calling `try_pop`; any other topology
  is outside the contract. Capacity is compile-time and a positive power of two
  (rejected otherwise). `try_push`/`try_pop` each complete in a bounded number of
  steps (1–2 loads + 1 store, no CAS loops) — neither end can stall the other.
  `size()`/`is_empty()`/`is_full()` are *snapshots* for diagnostics, not
  synchronization. The queue is non-copyable and non-movable: give it static or
  long-lived object storage.

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

## 📡 `nexigen::logging` API

Zero-virtual logging built on the `concepts::Logger` structural contract (`set_level`/`level`,
`log(LogLevel, std::source_location, std::string_view) noexcept -> Status`, the six
conveniences with the defaulted `std::source_location::current()`, and `flush()`).
Link `nexigen::logging` (alias of `nexigen_logging`) and include what you use:

* **`<nexigen/logging/log_level.hpp>`** — `LogLevel` (`Verbose..Fatal, Off`, declared in
  severity order so relational `<` *is* the ordering) and constexpr lowercase `to_string`.
  The runtime threshold is one relaxed-atomic cell per logger: `level()` is a snapshot, and
  a filtered call ends in ≈ 1 ns without touching the ring.
* **`<nexigen/logging/log_record.hpp>`** — `LogRecord<Message_capacity = 128>`: the fixed
  192-byte `alignas(64)` trivially-copyable payload crossing the SPSC boundary — calibrated
  monotonic-ns stamp, cached `SYS_gettid` thread id, extracted source-location fields, and
  an inline message buffer (truncation flagged, never spilled to the heap).
* **`<nexigen/logging/null_logger.hpp>`** — `NullLogger`: the zero-cost stub; at `-O1+`
  nothing survives into the emitted code (objdump-pinned by the tests).
* **`<nexigen/logging/console_logger.hpp>`** — `ConsoleLogger<Capacity = 4096, Message_capacity = 128>`:
  asynchronous wait-free plain-text sink; Verbose/Debug/Info → stdout, Warning and above →
  stderr, via raw `::write` (never `std::cout`/`printf`, which allocate).
* **`<nexigen/logging/file_logger.hpp>`** — `FileLogger<Ring_capacity = 8192, Message_capacity = 128>`:
  asynchronous wait-free JSON-lines writer with a CPU-pinned worker (`core_mask`), lock-free
  windowed throttling with per-cause drop counters and automatic drop-summary records, and
  hitch-free Hourly/Daily/Custom directory rotation. Wall time is UTC ISO-8601 with
  nanoseconds, `CLOCK_REALTIME`↔`CLOCK_MONOTONIC_RAW` calibrated at init and re-sampled on
  rotation/flush.
* **`<nexigen/logging/composite_logger.hpp>`** — `CompositeLogger<Loggers...>`: variadic
  fan-out over a `std::tuple` with fold expressions, `get<Index>()`/`get<LoggerType>()`
  sub-logger access, no virtual dispatch; composites nest and the empty pack is valid.

**Hot-path rules:** exactly **one producer thread per logger instance** (the SPSC contract
of `core::SpscRingBuffer` — a second producer is undefined use, not a degraded mode; deploy
one logger per producer thread and join sinks with `CompositeLogger`). An accepted enqueue
measures ≤ 20 ns median (Release-gated); a filtered call ≈ 1 ns. Neither ever blocks,
spins, locks, or allocates, and neither notices the consumer: a full ring answers
`BufferFull` immediately. Cross-thread `set_level`/`level`/`dropped`/`pending`/`writer_*`
are relaxed **snapshots** — diagnostics and configuration, never synchronization.
`flush()` is a control-plane barrier **outside** the never-block guarantee: it waits until
every record accepted before the call is written (and `fsync`-ed for the file sink).

Drop semantics (the `Status` returned by `log()` and the conveniences):

| Result | Meaning |
|---|---|
| `OK` (`{}`) | record accepted into the ring — **or** filtered below the active level (filtering is normal operation, never a loss) |
| `BufferFull` | ring full: the record was not enqueued; counted in `dropped().buffer_full` |
| `RateLimited` | throttle budget spent for the window; counted in `dropped().rate_limited`; the next window opens with a drop-summary record reporting exact per-cause counts |

Usage example (console + file in a composite, pinned writer, level changes):

```cpp
#include <nexigen/logging/composite_logger.hpp>
#include <nexigen/logging/console_logger.hpp>
#include <nexigen/logging/file_logger.hpp>
#include <nexigen/logging/log_level.hpp>

int main() {
    using namespace nexigen::logging;

    // One static composite owns both sinks (static storage: the rings are
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

    // Single producer thread = the simulation loop calling through `app`:
    static_cast<void>(app.info("1 kHz loop heartbeat"));     // console only
    static_cast<void>(app.error("actuator position drift")); // console + file

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

## 📄 License
Distributed under the **MIT License**. Created by Vasily Kovrigin.


У вас появилось 3 режима сборки для разных задач:
1. Боевая сборка для Real-Time (Максимальная скорость):bash
cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
Используйте код с осторожностью.
2. Проверка lock-free очередей и атомиков на гонки (ThreadSanitizer):bash
cmake -B build/tsan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEXIGEN_ENABLE_TSAN=ON
cmake --build build/tsan
Используйте код с осторожностью.
3. Проверка утечек памяти и выхода за границы массивов (AddressSanitizer):bash
cmake -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DNEXIGEN_ENABLE_ASAN=ON
cmake --build build/asan
