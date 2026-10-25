# Design

## Context

`nexigen::core` is complete and archived: `Result<T,E>`/`Status` over `std::expected` with `ErrorCode` (six enumerators today, `RateLimited` to be appended by this change), `Timestamp` (8-byte CLOCK_MONOTONIC_RAW nanoseconds via `Result<Timestamp> now()`), and `SpscRingBuffer<T, Capacity>` — a wait-free single-producer/single-consumer queue whose contract forbids any other thread topology, with `try_push`/`try_pop` returning `BufferFull`/`BufferEmpty` without blocking. `include/nexigen/logging/`, `src/logging/`, `tests/logging/` are empty; the build has only the `nexigen_options` INTERFACE target and the header-only `nexigen_core`. See proposal.md - Why for motivation.

Constraints that shape every decision below (enforced by `cmake/{Optimizations,CompilerWarnings,Sanitizers}.cmake`, not merely stylistic):

- `-std=c++23`, `CMAKE_CXX_EXTENSIONS OFF`; `-fno-exceptions -fno-rtti` globally; `-Werror` with `-Wold-style-cast -Wconversion -Wsign-conversion -Wshadow -Wcast-align`; Release adds `-O3 -march=native -flto`.
- Hot paths: no locks, no heap, explicit `std::memory_order`, `alignas(64)` false-sharing discipline, `clock_gettime(CLOCK_MONOTONIC_RAW)` as the time source.
- `nexigen_core` exports `_POSIX_C_SOURCE=200809L`; `cpu_set_t`/`pthread_setaffinity_np`/`syscall(SYS_gettid)` additionally need `_GNU_SOURCE`, so the logging targets must define it (a Linux-only glibc feature-test macro, not a language extension; glibc's `_GNU_SOURCE` implies and overrides `_POSIX_C_SOURCE`, and the shared struct layouts (`timespec`, atomics) are identical in both modes on the target glibc).
- A 20 ns median hot-path budget is *smaller than one vDSO `clock_gettime` call* on older parts (≈ 20–25 ns measured on prior-gen hardware), which is why requirement 6 explicitly allows "or calibrated TSC" — see D6.
- Verified toolchain assumptions carried over from the core change: TSan and ASan+UBSan builds work (mutually exclusive options); GCC 15.2/Clang 18+ and glibc x86-64; `std::jthread`/`std::stop_token` available under `-fno-exceptions`.

## Goals / Non-Goals

**Goals:**
- A seven-header, zero-virtual logging API that satisfies `concepts::Logger` for every concrete type, with a single shared hot-path pipeline (level filter → stamp → tid cache → fixed-buffer copy → `try_push`) that the benchmark proves ≤ 20 ns median.
- All I/O, formatting, rotation, and drop accounting isolated on one pinned `std::jthread` per asynchronous logger, so no producer ever observes filesystem or terminal latency.
- Memory layout and every atomic edge specified precisely enough to review line-by-line and feed to TSan.
- A `src/logging/` compiled unit pattern that `nexigen::metrics` can copy.

**Non-Goals:**
- No multi-producer (MPSC/MPMC) support: one producer thread per async logger instance, inherited from the SPSC contract (Risks R1 lists the mitigation). No blocking `wait_push`, no drop-oldest overwrite policy.
- No syslog/network sinks, no log-level-to-color tty styling, no structured key-value payloads beyond the fixed JSON schema.
- No Windows/macOS portability layer (POSIX/Linux only this change; the TSC fast path degrades to `clock_gettime` elsewhere).
- No runtime reconfiguration of ring capacity, message buffer size, or file paths — everything fixed at `initialize`.
- No log truncation policy beyond the inline-buffer cap (a flag records truncation; nothing is spilled to the heap).

## Decisions

### D1. Layering: header-inline hot path + a thin compiled cold core

`nexigen_logging` is a STATIC library (aliased `nexigen::logging`) with:

- **Headers** (`include/nexigen/logging/`): `log_level.hpp`, `concepts/logger_concept.hpp`, `log_record.hpp`, `null_logger.hpp`, `console_logger.hpp`, `file_logger.hpp`, `composite_logger.hpp`, plus `detail/level_control.hpp` and `detail/producer_hot_path.hpp`. Every function that can run on the producer path — level filtering, stamping, tid caching, record assembly, `try_push`, throttle state machine, summary synthesis — is `inline`/template, because the 20 ns budget requires inlining into the caller.
- **Sources** (`src/logging/`): consumer-thread-only units — `time_format.cpp` (civil-from-days + ISO-8601 nanosecond formatting), `json_format.cpp` (escape/format into caller-owned buffers), `clock_calibration.cpp` (TSC↔monotonic and realtime↔monotonic sampling), `affinity.cpp` (pin/query wrapper), `rotation.cpp` (`mkdirat`/`openat` path switching). These are never called by a producer thread, so out-of-line calls cost the subsystem nothing.

Rejected: header-only everything (matches `nexigen_core` but drags syscall-heavy formatting code into every consumer's compile unit and invites ODR/static-init complexity); a `virtual Logger` base (prohibited by requirement 3). Two-phase lifecycle instead of constructors that can fail: trivial/`constexpr` constructors, `initialize(Config) noexcept -> core::Status` that validates the config, snapshots calibration, and starts the `std::jthread`, rejecting re-`initialize` with `InvalidArgument`. Chosen because the ring is non-movable (`SpscRingBuffer` deletes moves), so a `Result<Logger>` factory by value is not viable without elision luck; the loggers are large objects living in caller-chosen storage (static, global, or caller arena).

### D2. `LogLevel` and the shared level-control cell

```cpp
enum class LogLevel : std::uint8_t { Verbose, Debug, Info, Warning, Error, Fatal, Off };
[[nodiscard]] constexpr std::string_view to_string(LogLevel) noexcept; // "verbose".."off", "Unknown" default arm
```

Declared exactly in that order so relational `<` *is* severity order (D requirement in `logging/log-level`). `to_string` mirrors the `ErrorCode` pattern: constexpr switch + `default:` "Unknown" arm, every name pinned by `static_assert` in the test TU.

```cpp
namespace nexigen::logging::detail {
class alignas(64) Level_control {                    // one owner-padded cache line
    struct alignas(64) Cell {
        std::atomic<LogLevel> level_{LogLevel::Info}; // relaxed load/store only
        std::uint8_t pad_[63];
    } cell_;
public:
    void set_level(LogLevel l) noexcept { cell_.level_.store(l, std::memory_order_relaxed); }
    [[nodiscard]] LogLevel level() const noexcept { return cell_.level_.load(std::memory_order_relaxed); };
};
}
```

Every logger composes one `Level_control` (`set_level`/`level` forward to it). The filter edge is `load(relaxed) → compare ≥ active → return {}` — one dependent load, nothing else; this is the ≈ 1 ns path the `~1ns` guarantee refers to. Relaxed is correct because level values are independent scalars: there is no data whose visibility must be ordered by the level store (each record is self-contained; a thread seeing a stale level for a few instructions just filters the way the previous level did). Third-thread `level()` reads the atomic directly. `pad_[63]` keeps neighbouring members (throttle cells, producer staging) off the line; `pad_` written never — padding, not state, so no TSan concern.

### D3. The concept

```cpp
namespace nexigen::logging::concepts {
template <class T>
concept Logger = requires(T t, LogLevel lv, const T ct,
                          std::source_location loc, std::string_view msg) {
    { t.set_level(lv) } noexcept -> std::same_as<void>;
    { ct.level() } noexcept -> std::same_as<LogLevel>;
    { t.log(lv, loc, msg) } noexcept -> std::same_as<nexigen::core::Status>;
    { t.verbose(msg) } noexcept -> std::same_as<nexigen::core::Status>;   // + debug/info/warning/error/fatal
    { t.flush() } noexcept -> std::same_as<nexigen::core::Status>;
};
}
```

Convenience methods are constrained *with only the message argument* so the defaulted `std::source_location loc = std::source_location::current()` parameter is part of the contract (a type whose `info` demanded an explicit location fails the concept). `CompositeLogger` instantiates over `concepts::Logger` members via its own `static_assert`. Rejected: a base-class `Logger` (virtual overhead, prohibited).

### D4. `LogRecord` memory layout

Fixed, trivially copyable, `alignas(64)`; default message capacity 128 → `sizeof(LogRecord) == 192` (pinned by `static_assert`):

| offset | field | size | notes |
|---|---|---|---|
| 0 | `core::Timestamp stamp` | 8 | monotonic ns (D6) |
| 8 | `std::uint32_t thread_id` | 4 | cached `SYS_gettid` (D5) |
| 12 | `LogLevel level` | 1 | `uint8_t` underlying |
| 13 | `std::uint8_t flags` | 1 | bit0 = message truncated |
| 14 | `std::uint16_t message_length` | 2 | bytes used of `message` |
| 16 | `const char* file_name` | 8 | `source_location::file_name()` — static storage |
| 24 | `const char* function_name` | 8 | likewise |
| 32 | `std::uint32_t line` | 4 | |
| 36 | pad | 4 | keeps `message` 8-aligned |
| 40 | `std::array<char,128> message` | 128 | inline payload, copy-truncated |

192 = 3 cache lines; the *slot* alignment in the ring is 64 via the buffer's own `alignas(64) std::array` (core D3), so no record shares its index cache line. The three location fields store the *extracted pointers*, not a `std::source_location` value: libstdc++ `source_location` wraps an internal impl pointer whose `file_name()/function_name()` accessors require indirection at consumption time; extracting once at production (3 loads) and storing plain static-storage `const char*` keeps the record trivially copyable and lets the consumer read without ever touching libstdc++ internals. The pointers reference program-static literals — never freed, allocation-proof. `Status` semantics: filtered/accepted → `{}`; ring full → `BufferFull`; throttled → `RateLimited`.

### D5. Cached thread identity

```cpp
[[nodiscard]] inline std::uint32_t cached_thread_id() noexcept {
    thread_local std::uint32_t tid = 0;          // 0 == unresolved
    if (tid == 0) tid = static_cast<std::uint32_t>(::syscall(SYS_gettid)); // first use only
    return tid;
}
```

One TLS load + compare on the hot path; the `SYS_gettid` syscall runs once per thread, never per record. Rejected: `pthread_self()` (unstable opaque pointer, not the OS tid the JSON schema needs); `gettid()` glibc wrapper (also fine on glibc ≥ 2.30, but the requirement names `syscall(SYS_gettid)` and it works under `_POSIX_C_SOURCE` once `_GNU_SOURCE` is on the target).

### D6. Stamping: calibrated TSC primary, vDSO clock fallback

A fresh `Timestamp::now()` per record spends the majority of the 20 ns budget. On x86-64 (and only there, gated by the predefined `__x86_64__` macro — not a language extension) the record stamp comes from `__rdtsc()` converted through a calibration snapshot owned by the producer cell:

```cpp
struct alignas(64) Stamp_source {
    std::uint64_t tsc_anchor;      // TSC sample aligned with mono_anchor
    std::uint64_t mono_anchor_ns;  // CLOCK_MONOTONIC_RAW at tsc_anchor
    std::uint64_t scale;           // ns per tick << 20, sampled at initialize()
    std::uint64_t reanchor_ticks;  // ~2^42: keeps the 64-bit product far from overflow
    std::uint64_t last_ns;         // monotonic clamp
};
```

`stamp = mono_anchor + ((tsc - tsc_anchor) * scale) >> 20`, then clamped `max(last_ns)` (non-decreasing per producer, spec'd in `logging/log-record`); every `reanchor_ticks` ticks (hours apart at 3 GHz, so a handful of `clock_gettime` calls per day/logger) the producer re-samples the anchor pair — a burst of two clock reads amortized to zero. `scale` is sampled once in `initialize()`: 1 ms sleep, then `scale = ((Δns << 20) / Δticks)` — initialization-only cost. On non-x86 builds the same fields compile away and `Timestamp::now()` is used directly, accepting that the 20 ns assertion is hardware-dependent (it is benchmark-gated anyway, R3). TSC validity (invariant constant-rate) is assumed for the target Ubuntu/x86-64 fleet and checked by the benchmark, not by cpuid parsing in this change (Open Questions).

Alternative rejected: storing raw TSC ticks in the record and converting on the consumer — cheaper on the producer (~5 ns saved) but breaks `Timestamp`'s unit-bearing domain and pushes per-record work to the formatter; the current split keeps producer arithmetic at three instructions (`sub`, `mul`, `lsr`-by-shr) and the record self-describing in the clock domain core already exports.

### D7. Producer hot path (shared by both async loggers)

`detail::Producer` holds the ring (template over capacity/message size), the `Level_control`, the throttle cell (D9), drop counters, and a `thread_local` staging `LogRecord` reused per call — the message is copied once into staging (bounded `memcpy` ≤ 128 B), filled fields around it (stamps, tid, location pointers), then `try_push(staging)` performs the trivially-copyable slot write. Two bounded copies of a 192-byte record with AVX stores ≈ 2–4 ns, inside budget. Order of operations: level load/compare (early return) → (throttle check if configured, D9) → stamp → `cached_thread_id()` → fill + `try_push` → `Status`. `std::source_location` accessors are called once each per record (libstdc++ function-local-static reads; verified cost included in the benchmark). The push's own ordering is inherited from `SpscRingBuffer` (release store of tail publishes the slot; acquire reload only on apparent-full) — this design adds **no new cross-thread edge on the producer path**.

### D8. Consumer loop and the flush handshake

Each async logger runs one `std::jthread` created in `initialize()`; the loop (cold path, allocation-free):

```
loop: drain up to 64 records (try_pop == BufferEmpty → stop batch)
      → sink them (console: format + ::write(1|2); file: rotate check → JSON → ::write)
      → idle: bounded spin (≤ ~64 pops-worth) then std::this_thread::sleep_for(≤ 1 ms)
      → stop_token stop_requested: final drain, fsync/commit, publish flush seq, exit
```

Idle sleeping is legal: the never-block rule binds only the producer (requirement 4); a consumer that burns a pinned core spinning on an empty ring violates requirement 7's purpose. Two atomics implement `flush()` (control-plane only): `producer_cell_.flush_seq` (plain uint64 bumped by the producer at flush request, published via release store) and `consumer_cell_.drained_seq` (release store after write+fsync of everything up to that flush_seq); `flush()` spins on acquire load until `drained_seq >= flush_seq`, sleeping between iterations (allowed off hot path). All four counters live in their own `alignas(64)` cells; the producer touches `drained_seq` never, the consumer touches `flush_seq` once per idle pass (acquire). Ordering table: level/throttle/drops: all relaxed; publication edges are exclusively the ring's own release/acquire index pairs — there is deliberately **no** seq_rel between record fields and ring slots because the slots are the payload. `NullLogger::flush()` returns `{}`; `ConsoleLogger` waits only for `drained_seq` (tty `::write` is not fsync-able — documented, not an error).

### D9. Rate limiting: windowed token count, lock-free

`Throttle_config { bool enabled; uint64_t max_records_per_window; uint64_t window_ns; }` enforced from a producer-owned `alignas(64)` cell (`window_start_ns`, `tokens_used`, all plain non-atomic — single producer) plus two `std::atomic<uint64_t>` drop counters in their own line (`dropped_rate_limited_`, `dropped_buffer_full_`, relaxed `fetch_add` only on drop paths, never on accepted calls). On each throttled-capable call: `if (stamp >= window_start + window_ns) { window_start += window_ns; tokens_used = 0; maybe_emit_summary(); }` then `if (tokens_used == max) { dropped_rate_limited_++, return RateLimited; } ++tokens_used`. No CAS, no locks, one monotonic domain (the record stamp itself is the clock — throttling costs zero extra syscalls). Ring-full drops return `BufferFull` and bump the other counter; both counts appear separately in summaries and in a `Dropped_stats dropped() const` accessor.

**Drop summary** (spec: automatic, exact counts, next window): `maybe_emit_summary()` runs when a new window opens *and* either counter is non-zero: it synthesizes a `Warning` record — source location of the internal marker file (`<nexigen>` pseudo-file, function `throttle_summary`), message `Dropped N record(s) in the last window (rate-limited: X, buffer-full: Y).` — `try_push`es it *before* the triggering user record, consumes no token allowance, and clears the counters only on push success (a `BufferFull` summary simply re-arms and retries on the next window transition). Producers stay wait-free (one extra bounded push, amortized by window length), and the line lands in the file in FIFO order like any record.

### D10. Clock calibration for UTC JSON timestamps

`clock_calibration` samples, at `initialize()`, the pair `(CLOCK_REALTIME ns, CLOCK_MONOTONIC_RAW ns)` and stores `utc_offset_ns = realtime − monotonic` (int64). The consumer formats `utc_ns = record.stamp + utc_offset_ns` (monotonic ns from D6/D7 is the single record time domain). The offset is **re-sampled at every rotation boundary and every `flush()`** (all consumer-side, NTP-step tolerant) so multi-day runs cannot drift the file's wall time; the re-sample is 2 clock reads, invisible to producers. Breaking the offset backwards across a step may momentarily repeat/skip wall time — accepted, recorded in the line itself with the unchanged monotonic order (spec scenario keeps file order == monotonic order).

### D11. Directory rotation on the consumer thread

`Rotation_config { enum Kind { None, Hourly, Daily, Custom } kind; uint64_t custom_ns; }`. For every drained record the consumer computes a period key from its UTC time: civil `(Y,M,D,H)` → `days*24+hour`, `days`, or `utc_ns / custom_ns` (epoch-anchored; epoch-aligned custom windows land on UTC-midnight multiples when `custom_ns` divides a day). The key is a plain integer compare against the active one — rotation work happens *only* when it changes: build the path `"<base_dir>/<label>/<file_name>"` into a fixed `char[512]` member buffer (label: `2026-07-23T23` hourly, `2026-07-23` daily, `epoch-<n>` custom, computed by `time_format`), `mkdirat` the directory, `openat(O_WRONLY|O_APPEND|O_CREAT)` the new descriptor, `fsync`+`close` the old, switch the member fd — all inside the consumer thread using pre-allocated buffers, so `logging/file-logger`'s "hitch-free" and "zero allocation during rotation" scenarios hold structurally. Formatting of the record that crossed the boundary happens after the switch (the record's own period owns its line). `time_format` implements civil-from-days (Hinnant algorithm, pure integer arithmetic — deliberately no `gmtime_r`, which touches process-wide tz state and can allocate).

### D12. JSON line formatting — exact schema, escaping, no allocation

Consumer writes `"{\"timestamp\":\"" + ts9 + "\",\"level\":\"" + to_string(level) + "\",\"app\":\"" + esc(app) + "\",\"thread_id\":" + u32 + ",\"message\":\"[" + basename(file) + ":" + u32 + "] (" + esc(function) + ") " + esc(message) + "]\"}\n"` into a fixed `alignas(64) char scratch[4096]` member (`LogRecord` message ≤ 128, location ≤ a few hundred, so no overflow — worst case the formatter returns a malformed-marker and the record is counted `malformed` and skipped; pinned by static bounds). `basename`: last `'/'+1` of `file_name`. Escape policy: `"` `\` → two-char form; `<0x20` → `\u00XX` (with `\b \t \n \f \r` short forms); all other bytes pass through unmodified, so UTF-8 Cyrillic (multi-byte ≥ 0x80) round-trips as raw bytes inside valid JSON. No `std::string`/`std::format`/`snprintf(%s)` on formatted payloads (`snprintf` into the fixed scratch is allowed — bounded, allocation-free; glibc's `snprintf` never mallocs for the formatted result unless locale wide-char conversion does, avoided by integer-only conversion specs).

### D13. ConsoleLogger sink details

Same `detail::Producer` + consumer loop with a different `Sink`: plain text line `"<ts> <level> <file basename>:<line> (<function>) <message>\n"` to fd 1 (Verbose/Debug/Info) or fd 2 (Warning+), via `::write` directly — deliberately *not* `std::cout`/`printf`, whose lazy init and buffering allocate. `ConsoleLogger::Config { LogLevel initial; std::size_t capacity template param; }`. Line buffer is a fixed member scratch like D12. Routing is per drained record on the consumer (requirement mapping: `logging/console-logger` - Stream routing by severity).

### D14. FileLogger configuration surface

```cpp
struct FileLogger_config {
    std::string_view app_name;        // copied into fixed char[64]
    std::string_view base_dir;        // copied into fixed char[512]; must be absolute or cwd-stable
    std::string_view file_name{"app.jsonl"};
    LogLevel initial_level{LogLevel::Info};
    Throttle_config throttle{};
    Rotation_config rotation{ Rotation_kind::Hourly };
    uint64_t core_mask{0};            // 0 == do not pin; else cpu_set_t mask, D15
};
template <std::size_t Ring_capacity = 8192, std::size_t Message_capacity = 128>
class FileLogger; // initialize(config) -> Status; dropped(); writer_affinity() snapshot
```

Validation in `initialize()` (fixed-buffer overflow, non-power-of-two capacities, `custom_ns == 0`, conflicting re-init) returns `InvalidArgument`/`SystemError` without starting the thread. Default footprint: 8192 × 192 B ≈ 1.5 MiB inline ring per `FileLogger`, resident from `initialize()` to destructor — the one-time initialization allocation budget; nothing after.

### D15. Affinity: `pthread_setaffinity_np` in `affinity.cpp`

The worker's first act inside the thread body (self-pinning, before the loop): `CPU_ZERO/CPU_SET` a `cpu_set_t`, call `pthread_setaffinity_np(pthread_self(), ...)`. Failure is not fatal to logging: it is stored into a `std::atomic<Status code>` snapshot cell (relaxed store) which `initialize()`'s return remains OK for, but `flush()`/`writer_affinity()` surface the pin failure code so tests/ops can assert. Why self-pinning inside the thread rather than from the creating thread with `native_handle()`: `std::jthread::native_handle` is available after start, but the thread may execute on a wrong core in the race window; self-pinning on entry is deterministic and needs no cross-thread timing. The verification scenario uses `pthread_getaffinity_np` — the test reads the snapshot the worker took of its *own* post-pin mask via `pthread_getaffinity_np` after pinning (one query, worker thread, init-time only) instead of reaching into `std::jthread` handles.

### D16. CompositeLogger

```cpp
template <concepts::Logger... Loggers>
requires (sizeof...(Loggers) >= 1 || true) // empty pack allowed: valid no-op composite
class CompositeLogger final {
    std::tuple<Loggers...> members_;
    detail::Level_control level_;
    template <std::size_t I> [[nodiscard]] auto& get() noexcept;               // index form
    template <class T>       [[nodiscard]] auto& get() noexcept;               // type form (unique-T static_assert)
public:
    // No initialize(): members are owned and already-initialized by the caller;
    // the composite only forwards. It satisfies the concept through forwarding.
    void set_level(LogLevel) noexcept;   // store own relaxed level, then (members_.set_level(lv), ...) fold
    Status log(LogLevel lv, source_location loc, std::string_view msg) noexcept {
        if (lv < level_.level()) return {};                      // composite short-circuit (fold never runs)
        (std::get<I>(members_).log(lv, loc, msg), ...);          // each member re-applies its own filter
        return {};
    }
    // verbose..fatal forward to log(lv, loc, msg) with the same defaulted location trick; flush: fold, last error wins
};
```

Why check the composite's *own* level and still let members filter: the composite level is the "set the whole tree" convenience (spec), while per-member levels configured through `get<I>()`/`get<T>()` are honored because each async logger's own ~1 ns filter runs on its own `Level_control` — the composite does not replicate per-member thresholds. Rejected: per-member level mirroring inside the composite (stale after direct sub-config). Type-form `get<T>()` static_asserts the type appears exactly once (duplicates must use the index form). `static_assert(concepts::Logger<CompositeLogger<ConsoleLogger<>, FileLogger<>>>>` in the test TU pins the composition.

### D17. CMake and test wiring

- `add_library(nexigen_logging STATIC src/logging/*.cpp)` with `include/` as `PUBLIC` include dir, `target_link_libraries(nexigen_logging PUBLIC nexigen_core PRIVATE Threads::Threads)` (`find_package(Threads REQUIRED)`; glibc ≥ 2.34 makes `pthread_*` libc symbols but the flag is harmless and portable), and `target_compile_definitions(nexigen_logging PRIVATE $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>)` for the syscall/affinity headers; public headers stay usable without `_GNU_SOURCE` except `file_logger.hpp`'s `SYS_gettid` inline path, which is covered by giving the * INTERFACE* define too (consumers compiling producers get it; core's own `_POSIX_C_SOURCE` export keeps applying and `_GNU_SOURCE` simply widens it).
- `tests/CMakeLists.txt`: `add_executable(nexigen_logging_tests logging/logging_test.cpp)` linked to `nexigen_logging`; `add_test`, `TIMEOUT 600` like core (spinning threads are slow under TSan). ASan/TSan options already global via `nexigen_options`.

### D18. Verification strategy (requirement 11)

`tests/logging/logging_test.cpp` reuses the core harness pattern (single TU, `NX_CHECK`, `main` returns failure count) plus:
- **Static claims:** `static_assert(concepts::Logger<...>)` × 4 composites, record layout (`sizeof == 192`, trivially copyable, `alignof == 64`), level naming, throttle math.
- **Allocation windows:** both the arena-`operator new` counter (catches C++ allocations) *and* `mallinfo2()` `hblkhd`-delta snapshots (catches raw `malloc` from libc paths) taken around steady-state windows on the *process* while the worker drains — asserting zero change. (A `write`-only consumer never calls malloc; the check proves it.)
- **Throttle:** configure `max=3/window`, push 10 during one window, assert 3 OK + 7 `RateLimited`, advance the window (sleep > window duration), log one, then `flush()` + scan the file for the `Dropped 7 record(s)...` line.
- **Rotation:** hourly + custom-`250 ms` schedules, producers streaming throughout, assert two period directories, complete lines, and that enqueue-latency samples across the boundary stay within the benchmark gate.
- **Affinity:** pick the *first core currently permitted to the process* (`sched_getaffinity` probe → first set bit) as the pin target so CI/container taskset limits don't flake; assert the worker snapshot equals it via `pthread_getaffinity_np`.
- **JSON:** fixed inputs, byte-exact compare against the schema (field order included), plus a Cyrillic UTF-8 payload round-trip and quote/backslash/control-character escape fixtures.
- **Latency:** Release-only (`#if defined(NDEBUG) && !__SANITIZE_ADDRESS__ && !__SANITIZE_THREAD__` — Clang's macro handled via detection of `-fsanitize=thread` define too, or simpler: a `NEXIGEN_LOGGING_STRICT_LATENCY` compile definition set by CMake only in Release configs), per-call `__rdtsc` deltas into a fixed 10 000-element array, insertion-sort median ≤ 20 ns; under Debug/TSan/ASan the suite still runs the correctness assertions with the bound relaxed to "finite & non-blocking" so the gate never tests the hardware it is not measuring.

## Risks / Trade-offs

- **R1 — SPSC means one producer thread per async logger instance.** The requirement names `SpscRingBuffer` explicitly, so the whole async pipeline inherits its contract; a second producer thread on the same instance is undefined-use, not a supported degraded mode. → Mitigation: documented contract on both headers (spec scenario in `logging/console-logger`, mirrored on file), and a *debug-only* producer-bind check (`NEXIGEN_LOGGING_BIND_PRODUCER`): first call stores the producer tid into the cell; mismatched tid returns `InvalidArgument` without touching the ring (off in the benchmark build, ≈ 0 ns). Multi-thread deployments today: one logger per producer thread, joined by `CompositeLogger` fan-out where each source logs to its own instances. MPMC ring is a future core change, not this one.
- **R2 — 20 ns includes real clock and TLS variance.** Hyperthread neighbors, frequency drops, and cold caches inflate medians on loaded CI. → Mitigation: the gate runs in Release only, on the machine that configures it, and measures the median (not p99) of 10k calls with a short warmup; a failed gate on slow hardware is a machine report, not an API failure. Budget arithmetic (≈ 1 level + 3 stamp + 1 tid + 6 loc + 2–4 copy + 1–3 push ≈ 15–19 ns) is documented so regressions are attributable.
- **R3 — TSC assumptions.** Constant/invariant TSC is universal on the target x86-64 fleet but unverified via CPUID here. → Mitigation: the D6 product-overflow re-anchor also makes a mid-run frequency change self-healing (re-sampling on re-anchor bounds error to microseconds); the `Timestamp::now()` fallback branch keeps non-x86 platforms correct, if slower.
- **R4 — Drop summaries consume ring slots.** A pathological drop storm during ring-full windows re-arms summaries repeatedly. → Mitigation: one summary per window transition max, `BufferFull` on the summary suppresses it until the next transition (D9), so overhead is O(windows), not O(drops).
- **R5 — Consumer starvation → silent record loss up to ring capacity.** Wait-free producers guarantee `BufferFull`+count, never delivery. → Mitigation (already specified): `dropped()` counters, summaries in the file, and `RateLimited` backpressure visible to the caller; choosing capacity/limits per deployment is an operational decision documented on the config type.
- **R6 — NTP steps distort file timestamps** (monotonic-domain records + offset snapshot). → Mitigation: offset re-sampling at rotation/flush (D10); record *order* is never affected; accepted trade-off, standard for monotonic-domain loggers.
- **R7 — libstdc++ `source_location` accessors** are function-local static reads; a pathological impl could add guard-variable checks per accessor. → Mitigation: measured once inside the D18 benchmark; if it shows cost, the location pointer extraction moves to `current()`'s impl struct directly (an implementation swap, no spec change).
- **R8 — `_GNU_SOURCE` widened to consumers** (INTERFACE) could alter glibc feature exposure for downstream TUs including `nexigen/logging` headers. → Mitigation: it is additive (widens, never narrows) and matches what HIL platforms already build with; `nexigen_core` consumers that include only core headers are untouched.

## Migration Plan

Additive release: new headers/lib/test target; one appended `ErrorCode` enumerator (`RateLimited`, at the end — existing values unchanged, so re-link without recompile is safe for existing consumers). No API moves, no data migration, no config changes for existing `core` users. Rollback = drop the `nexigen_logging` target and the delta to `result.hpp`; nothing else references it yet.

## Open Questions

- Should `ConsoleLogger` also expose the optional `core_mask` pin (currently FileLogger-only per requirement 7)? Deferred — the knob is trivially added later without changing any spec scenario.
- A dedicated `malformed_line_count` counter surfaced through `dropped()`/stats (D12 guard) — worth exposing, or is the impossible-by-construction bound enough? Deferrable to implementation review.
- Demangling function names in the JSON `message` for heavily-templated callers (`__PRETTY_FUNCTION__`-style strings get long against the 128 B message cap) — the truncation flag already covers it; a prettifier would need libiberty, out of scope.
