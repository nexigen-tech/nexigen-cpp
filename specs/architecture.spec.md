# NEXIGEN-CPP: Global Architecture Specification

## 1. Project Overview & Purpose
`nexigen-cpp` is an ultra-high-performance, cross-platform C++20 core library engineered for hard/firm real-time systems and hardware-in-the-loop (HIL) simulator platforms. 
The primary target environment is **Ubuntu 26.04 LTS** (Linux x86_64/ARM64), with strict architectural readiness for cross-platform compilation.

## 2. Namespace Hierarchy
All code must strictly reside within the following root and sub-namespaces:
- `nexigen::core` — Foundational primitives (lock-free containers, memory management, high-resolution timers, OS signals, base application lifecycle, error handling).
- `nexigen::logging` — Asynchronous, zero-allocation ring-buffer logging subsystem.
- `nexigen::metrics` — Wait-free atomic telemetry, counters, gauges, and histograms.

Directory mapping must match namespaces:
- Public Headers: `include/nexigen/<module>/<component>.hpp`
- Implementation: `src/<module>/<component>.cpp`
- Unit Tests: `tests/<module>/<component>_test.cpp`


## 3. Language Standard & Toolchain Constraints
- **Standard:** C++23 (`-std=c++23`). Compiler-specific extensions are strictly forbidden (`CMAKE_CXX_EXTENSIONS OFF`).
- **Compilers:** GCC 14+ (GCC 15.2+ recommended on Ubuntu 26.04) or Clang 18+.
- **Build System:** CMake 3.25+ with Ninja generator.

## 4. Real-Time & Performance Mandates (CRITICAL FOR AI AGENTS)
Every piece of code generated for the **Hot Path** (simulation loop, logging call site, metric update, event handling) MUST obey the following rules:

### 4.1. Zero Dynamic Allocation on Hot Paths
- `new`, `delete`, `malloc`, `free`, `std::make_shared`, and resizing of standard containers (`std::vector::push_back` without reserve, `std::string` heap allocations) are **strictly forbidden** during runtime execution loops.
- All buffers, queues, and memory pools must be pre-allocated during the initialization phase (`init()`).

### 4.2. Lock-Free & Wait-Free Concurrency
- `std::mutex`, `std::lock_guard`, `std::unique_lock`, and OS-level blocking primitives are **forbidden** on hot paths.
- Use `std::atomic` with explicit `std::memory_order` (`relaxed`, `acquire`, `release`). Avoid default `memory_order_seq_cst` unless mathematically required.
- All shared atomic variables modified by different threads MUST be aligned to cache lines to prevent False Sharing:
  ```cpp
  alignas(64) std::atomic<uint64_t> write_index_{0};
  ```

### 4.3. No Exceptions & No RTTI
- C++ Exceptions (`throw`, `try`, `catch`) are **disabled** (`-fno-exceptions`).
- Run-Time Type Information (`dynamic_cast`, `typeid`) is **disabled** (`-fno-rtti`).
- Error handling must use C++23 `std::expected<T, E>` (aliased as `nexigen::core::Result<T, E>`). Always check `.has_value()` and dereference via `*res` instead of calling `.value()`.


### 4.4. Zero-Cost Abstractions
- Prefer compile-time polymorphism (`templates`, `concepts`, `constexpr`) over runtime virtual functions (`virtual`) in performance-critical components.
- Use `std::string_view` or `std::span<const std::byte>` instead of passing `const std::string&` or `const std::vector<uint8_t>&`.

## 5. OS & Hardware Integration (Linux Focus)
- **Timers:** Use `clock_gettime(CLOCK_MONOTONIC_RAW)` or hardware `rdtsc` for nanosecond-precision timestamps. Avoid `std::chrono::system_clock` in hot paths.
- **I/O & Events:** Prefer Linux `epoll` or `io_uring` for non-blocking asynchronous event loops.
- **Thread Affinity:** Background worker threads (e.g., log writer, metric flusher) must support CPU core pinning (`pthread_setaffinity_np`) to avoid preempting real-time simulation threads.

## 6. Coding Style & Quality Standards
- **Naming Conventions:**
  - Classes, Structs, Concepts, Enums: `PascalCase` (e.g., `RingBuffer`, `LogLevel`).
  - Functions, Methods, Variables: `snake_case` (e.g., `try_push`, `read_index`).
  - Private member variables: `snake_case_` with a trailing underscore (e.g., `capacity_`).
  - Constants & Macros: `SCREAMING_SNAKE_CASE` (avoid macros where `constexpr` suffices).
- **Header Hygiene:** Use `#pragma once`. Minimize `#include` bloat inside `.hpp` files; use forward declarations wherever possible.
- **Safety & Attributes:** Annotate functions with `noexcept`, `constexpr`, and `[[nodiscard]]` aggressively.

## 7. Verification & Testing Rules
- Every component must have a corresponding unit test in `tests/`.
- All lock-free structures must pass compilation and execution under **ThreadSanitizer (TSan)** and **AddressSanitizer (ASan)** with zero warnings.
