# Design

## Context

`nexigen::core` is empty (`include/nexigen/core/`, `src/core/`, `tests/core/` hold only `.gitkeep`) and `CMakeLists.txt` currently defines a single `nexigen_options` INTERFACE target plus `cmake/{CompilerWarnings,Optimizations,Sanitizers}.cmake`. There is no `enable_testing()`, no test target, and no `add_subdirectory(tests)`. See proposal.md - Why for motivation.

Constraints that shape every decision below (enforced today by `cmake/Optimizations.cmake` and `cmake/CompilerWarnings.cmake`, not merely stylistic):

- `-std=c++23` with `CMAKE_CXX_EXTENSIONS OFF` (strict ISO mode, so no GNU extensions may be relied upon).
- `-fno-exceptions -fno-rtti` globally, so nothing in a header may require a throw path.
- `-Werror` with a hard warning list that includes `-Wold-style-cast`, `-Wconversion`, `-Wsign-conversion`, `-Wshadow`, `-Wdouble-promotion`, `-Wcast-align` - the nanosecond arithmetic below is full of signed/unsigned boundaries that these flags punish.
- Release/RelWithDebInfo add `-O3 -march=native` plus LTO; sanitizers are configured by `-DNEXIGEN_ENABLE_TSAN=ON` / `-DNEXIGEN_ENABLE_ASAN=ON` (mutually exclusive, `FATAL_ERROR` if both).
- Naming: PascalCase types, snake_case functions, `snake_case_` private members, aggressive `noexcept`/`constexpr`/`[[nodiscard]]`.

Verified on this machine (GCC 15.2.0, Clang 21.1.8, CMake 4.2.3, Ninja, glibc on x86-64) by compiling probes under exactly the flag set above: `std::expected` including `std::expected<void, E>` and `std::unexpected` compiles and runs with `-fno-exceptions -fno-rtti`; `clock_gettime(CLOCK_MONOTONIC_RAW, ...)` and `struct timespec` are declared under plain `-std=c++23` with no feature-test macro; linking needs no `-lrt`; `alignas(64)` around `std::atomic<std::size_t>` members compiles clean; `-fsanitize=thread` builds and runs.

## Goals / Non-Goals

**Goals:**
- Three self-contained headers that compile as the *only* includes in a `-fno-exceptions` TU, each usable on a hot path with zero allocation and zero locking.
- A single wait-free SPSC algorithm whose happens-before edges are explicit enough to be reviewed line-by-line and checked by ThreadSanitizer.
- A test harness with no third-party dependency that fails the build (non-zero CTest exit) on any check failure, and that is meaningful under both `-O3 -flto` and TSan.
- A CMake shape that `nexigen::logging` and `nexigen::metrics` can copy for their own targets.

**Non-Goals:**
- No MPMC or multi-producer queue, no blocking/waiting `push`/`pop` variants (no `wait_push` that spins on a condition variable), no overflow policies such as drop-oldest or overwrite.
- No wall-clock (`CLOCK_REALTIME`), calendar, or timezone conversion in `Timestamp`; no `std::chrono` clock integration in this change.
- No install/export/package-config (`nexigenConfig.cmake`) work, no packaging, no CI workflow.
- No error-message/detail payload attached to `ErrorCode` (see Open Questions).
- No Windows/macOS portability layer - Linux/glibc only, matching the mandated `CLOCK_MONOTONIC_RAW`.

## Decisions

### D1. `Result`/`Status` are thin aliases over `std::expected`, not a bespoke type

```cpp
namespace nexigen::core {
enum class ErrorCode : std::uint8_t { Ok, BufferFull, BufferEmpty, InvalidArgument, SystemError, NotInitialized };

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept;

template <class T, class E = ErrorCode>
using Result = std::expected<T, E>;

using Status = std::expected<void, ErrorCode>;
}
```

- `ErrorCode` gets an explicit `std::uint8_t` underlying type so `Result<T>` stays small (`Result<std::uint64_t>` is 16 bytes: 8 + 1 + padding), and so a future array-of-codes is cheap.
- `Status` is `std::expected<void, ErrorCode>`: `Status{}` means success. Chosen over a plain `ErrorCode`-returning convention so that "no value" and "value" share one error-handling shape, and over `Result<std::monostate>` for size and readability.
- **Rejected: a hand-rolled tagged union.** It would duplicate `std::expected`'s hardening and lose `.error()`/monadic consistency; the probe confirms it is available and usable under `-fno-exceptions` on the target compilers.
- **Rejected: `Result` carrying a source location or message.** Doubles the type's size on every return; deferred (Open Questions).

**Hard usage rule carried into every header and the tests:** never call `.value()` (it would `std::abort()` under `-fno-exceptions`); branch on `has_value()` and use `*res` / `res->` / `.error()`. Every function returning `Result`/`Status` is marked `[[nodiscard]]` so a dropped outcome is a `-Werror` failure.

`to_string` is a `constexpr` `switch` over all six enumerators returning string literals, with a `default:` arm returning `"Unknown"`; the `default` is deliberate - it makes a garbage-cast enum value well-defined instead of falling off the end of the function, and the enumerator list is still checked by a `static_assert` per code in the test TU (`static_assert(to_string(ErrorCode::BufferFull) == "BufferFull")`) so the loss of `-Wswitch` coverage is compensated.

### D2. `Timestamp` is a 8-byte value type; `now()` returns `Result<Timestamp>`

```cpp
namespace nexigen::core {
class Timestamp {
public:
    constexpr Timestamp() noexcept = default;
    explicit constexpr Timestamp(std::uint64_t nanoseconds) noexcept;

    [[nodiscard]] static Result<Timestamp> now() noexcept;

    [[nodiscard]] constexpr std::uint64_t nanoseconds() const noexcept;

    [[nodiscard]] constexpr auto operator<=>(const Timestamp&) const = default;

    [[nodiscard]] constexpr Timestamp operator+(std::uint64_t offset) const noexcept;  // deadline arithmetic
    [[nodiscard]] constexpr Timestamp operator-(std::uint64_t offset) const noexcept;  // deadline arithmetic, saturating at 0
    [[nodiscard]] constexpr Timestamp operator-(const Timestamp& earlier) const noexcept; // elapsed, saturating at 0

private:
    std::uint64_t ns_{};
};
}
```

- Field is private with an accessor rather than a public aggregate member, to honour the `snake_case_` private-field mandate and to keep the invariant (a nanosecond count, not a unit-free integer) from being silently reinterpreted. Still trivially copyable, still 8 bytes, still passed in a register.
- `now()` returns `Result<Timestamp>` so a `clock_gettime` failure surfaces as `ErrorCode::SystemError` instead of a silent zero - the zero-Timestamp sentinel was rejected because it is indistinguishable from a legitimate clock value at the epoch of the monotonic domain and would let failures propagate silently into deadline math. Cost: one branch, no allocation.
- Defaulted `operator<=>` (C++23) provides `==` and total ordering from the single `ns_` member; used for deadline comparisons with no floating point anywhere.
- Both subtraction forms **saturate at 0** rather than wrapping: `Timestamp - Timestamp` (an elapsed `Timestamp`-valued delta) when `b > a`, and `Timestamp - offset` when `offset > ns_`. Unsigned wrap-around would otherwise hand the caller a ~584-year interval from a clock-ordering bug instead of a detectable zero, and `Timestamp{0} - large_offset` must not yield `UINT64_MAX`. Both are documented at the declarations and pinned by the timestamp test suite (`core_test.cpp:199` for the offset form).
- Conversion math, written to survive `-Wsign-conversion`/`-Wconversion`/`-Wold-style-cast`:

```cpp
const ::timespec ts{};
if (::clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
    return std::unexpected(ErrorCode::SystemError);
}
return Timestamp{static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
                 static_cast<std::uint64_t>(ts.tv_nsec)};
```

  (`tv_sec` is signed `time_t`, hence the explicit `static_cast`; `1'000'000'000ULL` keeps the product in `uint64_t` and avoids `double` promotion.)
- **Rejected: `std::chrono::steady_clock`.** It is not `CLOCK_MONOTONIC_RAW` (it is NTP-disciplined `CLOCK_MONOTONIC`), and mandate 6 names the raw clock explicitly. Accepted consequence: `CLOCK_MONOTONIC_RAW` does not track frequency adjustments, so long intervals drift slightly against wall-clock time - fine for intervals and deadlines, wrong for absolute-time scheduling, which is a non-goal here.
- **Rejected: `rdtsc`/`__rdtsc`.** TSC-ness is not guaranteed to be invariant and it is an extension-flavoured choice; `clock_gettime` on Linux/x86-64 goes through the vDSO with no syscall trap.
- **Portability guard (belt-and-suspenders):** `nexigen_core` adds `target_compile_definitions(nexigen_core INTERFACE $<$<PLATFORM_ID:Linux>:_POSIX_C_SOURCE=200809L>)`. The probe shows glibc already exposes the declarations in strict `-std=c++23` mode, but that is not a guarantee across libcs or glibc versions, and the macro is the standards-correct way to ask for `clock_gettime`. Defining it inside the header before `<ctime>` was rejected - header include order makes such feature-test macros fragile. The Linux generator expression keeps it off non-Linux builds, and both compilers are exercised in Release and under TSan by the tasks.

### D3. SPSC ring buffer: monotonically increasing indices, power-of-two mask, one cache line per direction

```cpp
template <class T, std::size_t Capacity>
class alignas(64) SpscRingBuffer {
    static_assert(Capacity >= 1 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a positive power of two");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "index atomics must be lock-free");
    static_assert(std::is_default_constructible_v<T> && std::is_move_assignable_v<T>,
                  "T must be default-constructible and move-assignable");

    static constexpr std::size_t mask_ = Capacity - 1;

    struct alignas(64) Producer_cell {          // written only by the producer thread
        std::atomic<std::size_t> tail{0};      // one past the newest stored element
        std::size_t head_cache{0};             // producer's private mirror of consumer head
    } producer_{};                             // cache line 0

    struct alignas(64) Consumer_cell {         // written only by the consumer thread
        std::atomic<std::size_t> head{0};      // oldest unstolen element
        std::size_t tail_cache{0};             // consumer's private mirror of producer tail
    } consumer_{};                             // cache line 1

    alignas(64) std::array<T, Capacity> slots_{};  // cache lines 2..

    [[nodiscard]] static constexpr bool is_power_of_two(std::size_t value) noexcept; // public
};
```

Memory layout and ordering (the substance of the design, stated exactly as required for review):

| Member | Cache line | Written by | Read by | Order used |
|---|---|---|---|---|
| `producer_.tail` | 0 | producer | consumer | producer: `memory_order_relaxed` load (own), `memory_order_release` store; consumer: `memory_order_acquire` load |
| `producer_.head_cache` | 0 | producer only | producer only | plain non-atomic (single-thread ownership) |
| `consumer_.head` | 1 | consumer | producer | consumer: `relaxed` load (own), `release` store; producer: `acquire` load |
| `consumer_.tail_cache` | 1 | consumer only | consumer only | plain non-atomic |
| `slots_[i]` | 2+ | producer | consumer | plain stores/loads, ordered only by the index release/acquire pairs |

- Each thread writes exactly one cache line per element (its own), and each line is *owned* by one thread: `alignas(64)` on both cells plus `alignas(64)` slots keeps the producer's hot store, the consumer's hot store, and the payload array from sharing a line - no false sharing between the two ends.
- `head_cache`/`tail_cache` are the local index caches. `try_push` checks `tail - head_cache >= Capacity` against the *cached* peer index, so the common (not-full) path never touches the peer's cache line at all; only an apparently-full queue costs one `acquire` reload. Symmetric for `try_pop`. Without the caches, every element would pay a cross-core coherence read of the peer's line.
- Staleness is harmless because a stale-low cached peer index can only cause an apparently-full (or apparently-empty) result, and both operations refresh once and re-check before reporting `BufferFull`/`BufferEmpty` - so there is no spurious failure, and the operation is still a bounded number of steps.

`try_push` and `try_pop` in full (the algorithm to implement):

```cpp
Status try_push(const T& value) noexcept {          // plus a T&& overload
    const std::size_t t = producer_.tail.load(std::memory_order_relaxed);
    if (t - producer_.head_cache >= Capacity) {
        producer_.head_cache = consumer_.head.load(std::memory_order_acquire);
        if (t - producer_.head_cache >= Capacity) {
            return std::unexpected(ErrorCode::BufferFull);
        }
    }
    slots_[t & mask_] = value;
    producer_.tail.store(t + 1, std::memory_order_release);   // publishes the slot write
    return {};
}

Status try_pop(T& out) noexcept {
    const std::size_t h = consumer_.head.load(std::memory_order_relaxed);
    if (h == consumer_.tail_cache) {
        consumer_.tail_cache = producer_.tail.load(std::memory_order_acquire);
        if (h == consumer_.tail_cache) {
            return std::unexpected(ErrorCode::BufferEmpty);
        }
    }
    out = std::move(slots_[h & mask_]);
    consumer_.head.store(h + 1, std::memory_order_release);    // releases the slot for reuse
    return {};
}
```

- Two release/acquire pairs carry all the synchronisation: producer `tail` release-store → consumer `tail` acquire-load makes the slot write visible before it is read (spec: cross-thread visibility); consumer `head` release-store → producer `head` acquire-load makes the consumer's slot read complete before the producer overwrites that slot on wrap-around. Both indices are single-writer, so no CAS or `fetch_add` is needed and each operation is 1-2 loads plus 1 store with no loop: **wait-free**.
- Cached-index validity argument (why the `acquire` load that fills a cache is also the edge that legitimises later plain reads): `tail_cache` is only used for slot `h` after some acquire-load of `tail` proved `tail > h`, i.e. that element `h` was already published by a release-store that precedes it.
- Monotonic (never-wrapped) `std::size_t` indices mean full and empty are distinguishable while using **all** `Capacity` slots: empty is `head == tail`, full is `tail - head == Capacity`.
- **Rejected: capacity-1-lost-slot ring buffer.** Wastes a slot and needs no power-of-two, but the mandate already fixes power-of-two for masking, and losing a slot on a real-time queue is pure cost.
- **Rejected: modulo (`% Capacity`) instead of `& mask_`.** Integer division per element on the hot path.
- **Rejected: `std::atomic<std::size_t>::fetch_add` ticketing.** Would allow producer/consumer to observe indices out of publication order and break FIFO.
- **Rejected: CAS-based queue (FreeList-style).** Support for more than one producer is not needed here; CAS retries would forfeit the wait-free guarantee the spec requires.
- **Rejected: raw `unsigned char` storage with placement `new`/manual destruction.** Its per-element placement `new` conflicts with the "no `new` on hot paths" mandate and adds lifetime bookkeeping. `std::array<T, Capacity>` default-constructs everything at object creation (initialisation time, allowed), and `try_pop` move-assigns out, leaving a valid moved-from object in the slot. Consequence: `T` need only be default-constructible and move-assignable (so move-only payloads work) but is **not** required to be trivially copyable, and a popped-from slot holds a moved-from value until overwritten.
- `size()`, `is_empty()`, `is_full()` are provided for control-plane/diagnostic use and are documented as *snapshots* (a third thread's observation, not a linearizable reading); they are not for use in the hot path. `capacity()` is `static constexpr`.
- The class is non-copyable and non-movable (atomics), so it is expected to live in static/global storage or as a member of a long-lived object; documented in the header.
- The header self-verifies its layout with internal `static_assert`s (cell `alignof == 64`, `sizeof` of each cell a multiple of 64, `alignof(SpscRingBuffer) >= 64`) so a future refactor that collapses the two index cells onto one line fails compilation instead of silently degrading.
- The compile-time rejection of non-power-of-two `Capacity` is a hard `static_assert`, which a test TU cannot "catch". To keep that spec scenario testable, `is_power_of_two` is exposed as a public `constexpr` predicate, tested directly, and the `static_assert` firing is confirmed once by a deliberate temporary instantiation (`Capacity = 1000`) during implementation.

### D4. Zero-dependency single-file test harness

`tests/core/core_test.cpp` is one translation unit, no GTest/Catch2, no external fetch (the repo must remain buildable offline), organised as:

- A minimal local macro pair (`NX_CHECK(...)` / suite counters) that records failures with file/line and prints one summary line per suite; `main` returns the failure count, so a non-zero return fails CTest. (A bespoke harness rather than a fetched dependency keeps the mandate "zero-dependency" literal and keeps `-Werror` scope small.)
- `static_assert` blocks proving the compile-time claims: `to_string` values, `Result`/`Status` alias identity with `std::expected`, `is_power_of_two(1000) == false` / `(1024 == true)`, `sizeof(Timestamp) == 8`, `std::is_trivially_copyable_v<Timestamp>`, `alignof(SpscRingBuffer<...>) == 64`.
- Single-threaded behavioural suites: full/empty transitions across exactly `Capacity` pushes and pops, `BufferFull`/`BufferEmpty` reporting, FIFO order across several wrap-arounds, `Timestamp::now()` monotonicity plus delta/`operator+`/saturating-subtraction, error-code round trip through `to_string`.
- Two-threaded suites using `std::jthread` (joins on scope exit, no detach): (1) 1,000,000-element stress test - producer pushes `0..999999`, retrying on `BufferFull`, consumer pops and asserts values arrive in strictly increasing order with no gaps, then the two counts are compared; (2) a visibility test carrying a padded multi-field payload struct (>= 64 bytes, every field derived from the sequence number) so a missing release/acquire edge shows up as a field mismatch rather than passing by luck.
- A "consumer idles first" variant that starts the consumer before the producer so the empty-retry path and the cached-index refresh paths are both exercised.
- `Capacity` for the stress queue is `8192` (64 KiB of `std::uint64_t` slots) - small enough to force thousands of wrap-arounds within 1M elements, small enough to stay in L2.
- No timing assertions (no latency budgets asserted), because a CI run under TSan would make any such threshold flaky. Only correctness properties are asserted.

### D5. CMake shape: one INTERFACE library per namespace module plus a `tests/` subdir

- Root `CMakeLists.txt` gains:
  - `option(NEXIGEN_BUILD_TESTS "Build nexigen unit tests" ON)`;
  - `add_library(nexigen_core INTERFACE)` + `add_library(nexigen::core ALIAS nexigen_core)`;
  - `target_include_directories(nexigen_core INTERFACE $<BUILD_INTERFACE:.../include> $<INSTALL_INTERFACE:include>)` and `target_compile_features(nexigen_core INTERFACE cxx_std_23)`;
  - `target_link_libraries(nexigen_core INTERFACE nexigen_options)` so flags stay single-sourced in `nexigen_options`;
  - the `_POSIX_C_SOURCE` definition from D2, guarded by `$<$<PLATFORM_ID:Linux>:>`;
  - `if(NEXIGEN_BUILD_TESTS) enable_testing() add_subdirectory(tests) endif()`.
- New `tests/CMakeLists.txt`: `add_executable(nexigen_core_tests core/core_test.cpp)`, `target_link_libraries(nexigen_core_tests PRIVATE nexigen_core)`, `add_test(NAME nexigen_core_tests COMMAND nexigen_core_tests)`.
- `BUILD_TESTING`/`include(CTest)` was rejected: it drags in `Experimental`/`Coverage` Nightly targets this project does not want. A plain option is smaller and matches the existing hand-written `cmake/` style.
- A separate compiled `src/core/*.cpp` static library was rejected - the primitives are `constexpr`/templates, and keeping `nexigen::core` header-only means downstream consumers get cross-TU inlining with no link step. `src/core/` therefore keeps its `.gitkeep`.

## Risks / Trade-offs

- **[Under-reading `std::expected` under `-fno-exceptions`]** `.value()`/`.error()` on a wrong branch calls `std::abort()` rather than throwing, which looks like a crash in production. → Never call them unchecked; `[[nodiscard]]` on every `Result`-returning function; the review gate is a textual check that `value()` appears nowhere in `include/` or `tests/`, plus the tests use `has_value()` only.
- **[Cached peer index reports a false full/empty]** A stale cache can *only* produce an apparently-full/apparently-empty reading. → Both operations reload the real atomic once and re-check before returning `BufferFull`/`BufferEmpty`, which the "fill then drain" and 1M-element scenarios pin down.
- **[Plain `slots_` accesses are a data race if the ordering ever regresses]** → The release/acquire pairs are the only synchronisation and are stated per-member above; the two-threaded 1M-element stress run under `-DNEXIGEN_ENABLE_TSAN=ON` is the detector, and the padded-struct visibility suite catches a weakened order that happens to pass for integers.
- **[TSan makes the 1M-element stress slow]** Sanitizer overhead is roughly an order of magnitude. → Accept the cost (the count is a stated requirement of this change): run under RelWithDebInfo TSan, keep the per-element work to a single `Status` check, print nothing inside the loop, and give the CTest a raised timeout rather than reducing the element count.
- **[`.gitignore`/build-dir hygiene vs `-march=native`]** Release tests are compiled with `-march=native`, so a test binary built on one host may be illegal on another. → No mitigation inside this change (pre-existing project-wide choice); flagged for whoever later adds CI, and the stress binary is never shipped.
- **[`_POSIX_C_SOURCE` on an INTERFACE target leaks into consumers' TU configuration]** → Applied only on Linux and verified by compiling with both GCC and Clang in Release and TSan modes; if a downstream build ever needs GNU-only extensions, `_GNU_SOURCE` on that target overrides it, which is the documented glibc precedence.
- **[`ErrorCode::Ok` is redundant inside `std::expected`]** A success is already the value state, so `Ok` can only appear as a bare `ErrorCode`. → Kept because the change request names it explicitly; documented as "the success enumerator, not expected to appear inside `Result`'s error arm", and `to_string` covers it.
- **[`std::array<T, Capacity>` default-constructs `Capacity` objects up front]** For a large `T` and 1M capacity this is a real initialisation cost. → Accepted: it is one-time construction cost, not hot-path cost; large-capacity queues are an application decision and the mandate requires pre-allocation at initialisation anyway.
- **Trade-off: out-parameter `try_pop(T&)` instead of `Result<T>` return.** Chosen to avoid a second move of a potentially large payload and to keep `Status` uniform with `try_push`; the cost is a less fluent API and a `T` that must be default-constructible at the call site. Documented at the declaration.

## Migration Plan

Purely additive - there is no existing code, API, or data to migrate, and nothing shipped depends on `nexigen::core` yet.

1. Add the three headers, then `nexigen_core` in CMake, then the test target (tasks.md order); each step compiles before the next.
2. Rollback is `git revert` of the change commit: no persistent state, no generated files committed (`build/` is already ignored), and `openspec/` artifacts stay in the repo for history.
3. No version bump: `project(... VERSION 0.1.0)` stays as-is; this is the first public surface of `0.1.0`.

## Open Questions

- Should errors later carry context (a message or a structured detail alongside `ErrorCode`)? Decide when `nexigen::logging` needs it; `Result<T, E>` is already parameterised on `E`, so answering later is non-breaking.
- Should `SpscRingBuffer` gain blocking/waiting variants (`wait_push` with `std::atomic::wait`) or an MPMC sibling? Only when a consumer needs them; the current API does not preclude it.
- Is 64 the right cache-line constant, or should it be a tunable (128 on some newer x86 and Arm cores)? The mandate fixes 64 today; a `NEXIGEN_CACHE_LINE_BYTES` knob is a follow-up that can be decided independently of the layout logic.
- Should `nexigen::core` be installed and exported (`CMakePackageConfigHelpers`)? Deferred with all packaging - no out-of-repo consumer exists yet.
