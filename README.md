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
