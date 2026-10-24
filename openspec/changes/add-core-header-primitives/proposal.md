# Proposal

## Why

`nexigen::core` is currently an empty skeleton (`include/nexigen/core/`, `src/core/`, `tests/core/` contain only `.gitkeep`), so `nexigen::logging` and `nexigen::metrics` have nothing to build on. Every downstream hot path needs the same three primitives — error propagation without exceptions, monotonic nanosecond time, and wait-free thread hand-off — and each needs a canonical, header-only implementation before any real-time component can be written.

## What Changes

- Add `include/nexigen/core/result.hpp`: an `ErrorCode` enum class (`Ok`, `BufferFull`, `BufferEmpty`, `InvalidArgument`, `SystemError`, `NotInitialized`), a `constexpr` `to_string(ErrorCode) -> std::string_view`, and the `Result<T, E = ErrorCode>` / `Status` aliases over C++23 `std::expected`.
- Add `include/nexigen/core/timestamp.hpp`: a `Timestamp` trivially-copyable value type wrapping `std::uint64_t` nanoseconds, sourced from `clock_gettime(CLOCK_MONOTONIC_RAW)`.
- Add `include/nexigen/core/spsc_ring_buffer.hpp`: a wait-free `SpscRingBuffer<T, Capacity>` for exactly one producer and one consumer, power-of-two capacity, `alignas(64)`-separated atomic indices, and cached local index mirrors to minimise atomic traffic.
- Add `tests/core/core_test.cpp`: a zero-dependency (no GTest/Catch2) test translation unit covering all three headers, including a 1,000,000-element multithreaded producer/consumer stress test.
- Wire tests into the build: a `nexigen_core` INTERFACE header library (aliased `nexigen::core`), `enable_testing()`, a `nexigen_core_tests` executable target linked against it, and a registered CTest test.
- No existing behaviour changes; nothing is removed or renamed (**no breaking changes** — this is the first public API of the library).

## Capabilities

### New Capabilities
- `core/result`: Exception-free error modelling — the `ErrorCode` catalogue, its `constexpr` name mapping, and the `Result`/`Status` aliases that all fallible `nexigen` APIs return.
- `core/timestamp`: Monotonic nanosecond time source — acquisition, representation, comparison, and arithmetic for real-time deadlines and interval measurement.
- `core/spsc-ring-buffer`: Wait-free single-producer/single-consumer bounded queue — capacity semantics, memory-ordering guarantees, and thread-safety contract.

### Modified Capabilities
_None — no specs exist yet (`openspec list --specs` reports "No specs found")._

## Impact

- **Code**: three new public headers under `include/nexigen/core/` (the entire initial surface of `nexigen::core`); `src/core/` stays empty because all primitives are header-only; one new test file under `tests/core/`; `CMakeLists.txt` gains test wiring.
- **Dependencies**: none added — standard library only (`<expected>`, `<atomic>`, `<ctime>`, `<thread>`) plus the already-vendored CMake modules in `cmake/`.
- **Build**: the existing `nexigen_options` INTERFACE target remains the single source of compiler flags; `nexigen_core` and the test target consume it, so `-fno-exceptions -fno-rtti` and the full `-Werror` warning set apply to the new code unchanged.
- **Performance impact**: additive and inert at runtime for existing code (there is none). All three primitives are designed for hot-path use: `Timestamp` costs one `clock_gettime` call, expected to be served by the kernel vDSO without a trap on Linux/x86-64, with no allocation; `Result`/`Status` are value types no larger than `sizeof(T)` (or nothing) plus `sizeof(ErrorCode)` and padding, with no heap and no exception tables; `SpscRingBuffer` storage is a fixed `alignas(64)` inline array, so a queue is allocation-free at construction and its `try_push`/`try_pop` issue one release store plus a relaxed load of the owner's own index, touching the peer's index only when a cached mirror says the queue is full or empty.
- **Memory-allocation impact**: zero heap allocations anywhere in the new code — verified by static requirements (no `new`/`delete`/`malloc`/dynamic containers) and by the capacity being a compile-time template parameter. The only new footprint is the ring-buffer storage itself, `Capacity * sizeof(T)` rounded up to cache-line alignment and resident for the object's lifetime (a `SpscRingBuffer<std::uint64_t, 1024>` is ~8.3 KiB: 8 KiB of slots plus two 64-byte index cells).
- **Verified toolchain assumptions** (probes under the project's exact flag set): GCC 15.2.0 compiles `std::expected` with `-fno-exceptions -fno-rtti -Werror` and the full warning list; `CLOCK_MONOTONIC_RAW` and `clock_gettime` are visible under strict `-std=c++23` (no `_GNU_SOURCE` needed) and link without `-lrt` on this glibc; `-fsanitize=thread` builds and runs clean; Clang 21.1.8 and Ninja are both present.
