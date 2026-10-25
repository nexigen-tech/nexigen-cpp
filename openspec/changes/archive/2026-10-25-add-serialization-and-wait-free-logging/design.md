# Design

## Context

See proposal.md – Why. Current state in code:

- `concepts::Logger` and all four loggers expose `log(LogLevel, std::source_location, std::string_view) -> core::Status` plus string_view conveniences; drop causes are surfaced as return codes (`logging/logger-concept`, `logging/file-logger` specs).
- Every async logger composes exactly one `detail::Producer` holding ONE `SpscRingBuffer<LogRecord, Capacity>`; a debug-build guard (`NEXIGEN_LOGGING_BIND_PRODUCER`) rejects foreign producers. `detail::Async_worker` drains that single ring.
- `LogRecord<MessageCapacity>` (192 B at 128) is eagerly filled: the producer formats nothing today but copies an already-rendered message; the consumer only concatenates fields into console/JSON lines.
- `ErrorCode`/`LogLevel` have `constexpr to_string`; `Rotation_kind` has no name helper at all. No `<format>` specializations, no nlohmann::json anywhere, no `from_string`.
- Test harnesses are zero-dependency (`NX_CHECK`), one TU per module, with a fixed-arena `operator new` allocation detector in `tests/core`.

Constraints: project mandates 1–11 in `openspec/config.yaml` (C++23, `-fno-exceptions`/`-fno-rtti`, `std::expected`, zero hot-path allocation on producer AND worker loops, per-thread SPSC sharding for multi-producer, `alignas(64)`, deferred variadic formatting, universal `to_string`/`from_string`/enum JSON/`std::formatter`).

## Goals / Non-Goals

**Goals:**

- One serialization contract across core/chrono/logging with zero hot-path cost (all string/JSON machinery cold).
- Variadic, implicitly located, compile-time-checked logging API that changes producer cost by ≤ a handful of nanoseconds.
- True wait-free multi-producer ingestion into each async logger with no shared write index and no CAS on the steady-state path.
- A build that finds nlohmann::json portably (system package or FetchContent) and keeps every TU exception-free.

**Non-Goals:**

- No JSON serialization of structs/classes (enums only, per the mandate); no `to_json` for `LogRecord`/`Timestamp`.
- No lock-free MPMC queue (sharding is the concurrency model), no change to rotation policy, throttling windows, flush handshake, or affinity machinery semantics.
- No `metrics` module coverage (empty today; future changes adopt the same contracts).
- No runtime format-string support (`std::vformat` stays banned: it allocates).

## Decisions

### D1. JSON dependency rides `nexigen::core`'s INTERFACE

`CMakeLists.txt`: `find_package(nlohmann_json QUIET)`; on miss, `FetchContent_Declare(nlohmann_json URL pinned release archive)` + `FetchContent_MakeAvailable`, then `target_link_libraries(nexigen_core INTERFACE nlohmann_json::nlohmann_json)` and `target_compile_definitions(nexigen_core INTERFACE JSON_NOEXCEPTION=1)` — PUBLIC/INTERFACE so chrono, logging, tests, and downstream TUs inherit the define before any `#include <nlohmann/json.hpp>`.
Alternatives: (a) define the macro only in nexigen targets — rejected: consumer TUs including enum headers would compile json with exceptions enabled and break `-fno-exceptions`; (b) isolate JSON in separate `*_json.hpp` headers — rejected: the mandate requires `to_json`/`from_json` in the enum's own namespace, best co-located; compile-time cost is accepted (header-only, cold path).

### D2. Enum vocabulary headers (ADL by co-location)

`to_json`/`from_json`/`from_string` live in the enum's declaring header, in its namespace: `include/nexigen/core/result.hpp` (`ErrorCode`), `include/nexigen/logging/log_level.hpp` (`LogLevel`), `include/nexigen/logging/detail/rotation.hpp` (`Rotation_kind`, publicly aliased `Rotation_schedule`), each also declaring `template <> struct std::formatter<E> : std::formatter<std::string_view>` with a `format` that forwards `to_string(e)`. `from_json` body shape (uniform, non-throwing):

```cpp
inline void from_json(const nlohmann::json& j, E& out) noexcept {
    const auto* s = j.get_ptr<const nlohmann::json::string_t*>();  // never throws under JSON_NOEXCEPTION
    static_cast<void>(from_string(std::string_view(*s), out));      // leaves out unchanged on unknown name
}
```

(null-pointer guard: `s == nullptr` returns immediately; the enum keeps the caller's pre-set fallback.) `from_string` is a `constexpr` switch over exact `to_string` spellings (C++23 `string_view` comparisons are constexpr), returning `bool`.
Rotation_kind also gains the missing `constexpr to_string` here — `"none" / "hourly" / "daily" / "custom"`.

### D3. `FormatWithLocation<Args...>` (new `include/nexigen/logging/format_with_location.hpp`)

```cpp
template <typename... Args>
struct FormatWithLocation {
    std::format_string<Args...> fmt;
    std::source_location loc;
    template <std::size_t N>
    consteval FormatWithLocation(const char (&literal)[N],
                                 std::source_location l = std::source_location::current()) noexcept
        : fmt(literal), loc(l) {}
};
```

Applied amendment (compile-driven, semantics unchanged): the consteval constructor takes the
**character array literal directly**, not a `std::format_string<Args...>` — a
char*→format_string→wrapper chain would be two user-defined conversions, which implicit
conversion sequences reject. Public signatures route the member type through the non-deduced
identity alias `detail::format_param_t<Args...>`: bare `FormatWithLocation<Args...>` as the
first parameter is a deduced context where `const char[N]` mismatches the class-template head
and fails template-argument deduction outright (both findings pinned in the
`format_with_location.hpp` comments).

Public logger methods take it **by value** first, followed by `Args&&...`: deduction comes only from the trailing arguments (the non-deduced format parameter contributes no candidates and succeeds via implicit `consteval` conversion after pack deduction; an empty pack leaves `Args = {}`). Risk R1 pins this with tests for both `info("lit")` and `info("lit {}", x)`.
API shape (uniform across Null/Console/File/Composite and the concept):

```cpp
template <typename... Args>
void log(LogLevel, FormatWithLocation<Args...>, Args&&...) noexcept;
template <typename... Args> void debug(FormatWithLocation<Args...>, Args&&...) noexcept; // +5 more
```

`flush()` keeps returning `core::Status` (control plane). Record methods lose `[[nodiscard]]` (nothing to nodiscard on `void`).

### D4. Deferred-payload `LogRecord`: pinned 192 B layout

Template `LogRecord<MessageCapacity>` keeps `MessageCapacity = 128` default. New pinned layout (`sizeof == 192 == 3 × 64`, `alignas(64)`):

```
 0   stamp          chrono::Timestamp   u64   monotonic ns
 8   thread_id      u32                 cached SYS_gettid
12   level          LogLevel (u8)
13   flags          u8                  bit0 = args truncated (producer), bit1 = render truncated (consumer)
14   arg_length     u16                 bytes of args_ used
16   file_name      const char*         static storage
24   function_name  const char*         static storage
32   line           u32
36   (pad)          u32
40   fmt            const char*         points into fmt_ (std::format_string literal, static storage)
48   render         fn ptr              noexcept std::size_t(*)(const LogRecord&, char* out, std::size_t cap)
56   args_          array<char, N>      packed argument blob (8-aligned)
184  (tail pad to alignas(64)) → 192
```

Trivially copyable maintained (the callback is a plain function pointer to a template instantiation). The old `message` field disappears; "message_view()" becomes "render_view()/render(scratch)" on the consumer side. `to_string() const` (member + free) dumps the raw record (level, stamp, location, fmt text, arg length, flags) — std::string, cold path only.

### D5. Argument packing: fixed-offset trivially-copyable serialization

`detail::Arg_pack<Args...>` computes a compile-time offset table (8-byte alignment per slot). Packable set (concept `detail::Packable<T>`): integral/floating point, enum classes, `bool`, `char`, pointers, `std::string_view`, `Timestamp` — i.e. trivially copyable scalars/enums plus string-like views. `std::string_view` slots serialize as `{u16 len; inline bytes}` with per-string inline cap `MessageCapacity/2` (64 B): overflow truncates the *copy* and sets bit0 — producers never spill to heap. `static_assert`s reject non-packable argument types at the call site (compile error, no runtime path).
The type-erased callback is `&detail::render_impl<MessageCapacity, Args...>`: reconstructs the `std::tuple<Args...>` from the blob and renders via `std::vformat_to` through a `Bounded_sink` output adapter (applied amendment: libstdc++ 15 restricts the bounded checked `format_to_n`/`vformat_to` overloads to compile-time `format_string`s and its unchecked runtime overload takes only `(out, string_view, args)` — the sink restores the `cap` bound while still never allocating), returning the written length; overflow sets bit1 and truncates (never half-emitted: sinks cap at the returned length). `noexcept` is total under checked `std::format_string` (R2 documents the terminate-only residual).

### D6. `detail::Producer` becomes a shard array (`MaxThreads = 32` default, template parameter)

```cpp
template <std::size_t ShardCapacity, std::size_t MessageCapacity, std::size_t MaxThreads = 32>
class Producer {
    struct alignas(64) Registration_cell {
        std::atomic<std::uint32_t> next_slot{};   // relaxed fetch_add, ONE TIME per (thread, producer)
        std::atomic<std::uint32_t> active{};      // release CAS-max of (slot+1) after slot
                                                  // claim (atomic::fetch_max is C++26; the
                                                  // bounded release CAS loop is the C++23 route)
        std::uint8_t pad_[56]{};
    } registration_{};

    std::array<core::SpscRingBuffer<Record, ShardCapacity>, MaxThreads> shards_;  // each already alignas(64)

    struct alignas(64) Shard_stats {              // one line per shard: producer WRITES via relaxed
        std::atomic<std::uint64_t> rate_limited{};  //  stores (published from a local plain counter),
        std::atomic<std::uint64_t> buffer_full{};   //  consumers/diagnostics LOAD relaxed — no RMW,
        detail::Throttle_cell throttle{};           //  wait-free by construction.
        std::uint8_t pad_[..]{};                    //  (sizeof % 64 == 0, static_assert)
    } stats_[MaxThreads];

    alignas(64) std::atomic<std::uint64_t> slot_exhausted{};  // shared, relaxed fetch_add ONLY on the
    std::uint8_t pad_[56]{};                                   //  misconfigured 33rd+ thread path
};
```

Shard routing (hot path): a `thread_local` per-thread map `[{Producer*, slot} × 4]` (few loggers per thread; linear scan ≤ 4) resolves the slot — after first registration the steady path is: TLS read → `shards_[slot]` → that ring's own wait-free `try_push` (release store of the *private* tail). Registration (first call per thread per producer): `next_slot.fetch_add(1, relaxed)`; `>= MaxThreads` → drop + `slot_exhausted.fetch_add(1, relaxed)`; else publish `active` (release CAS-max to `slot+1`; D6 cell comment) so the consumer's acquire-load sees the fully constructed slot.
Per-shard throttle windows keep single-writer plain state; the staging record is a stack-local
`Record` (applied amendment: trivially constructible, bounded copies — equivalent
zero-allocation story, one fewer TLS object than the original thread_local staging reuse). Drop counters use **store-then-load relaxed** (not `fetch_add`) so no producer ever performs a contended RMW. `dropped()` sums `2 × MaxThreads` relaxed loads; `pending()` sums ring sizes.
`NEXIGEN_LOGGING_BIND_PRODUCER` is retired — multi-producer is now the contract; the alias compiles to nothing (kept defined-out to avoid churn elsewhere).
Sizing math: `ShardCapacity` defaults **128 for ConsoleLogger/FileLogger** — 32 × 128 records × 192 B = 786 KiB ring storage per async logger (Console's old default 4096 × 192 = 786 KiB; File's 8192 × 192 = 1.5 MiB shrinks). Aggregate capacity stays power-of-two-shaped; total footprint remains the same order and is compile-time tunable per instance.
Alternatives: one MPMC queue (rejected: CAS contention violates mandate 5); sharded queues with a *shared* index (rejected: same contention at the publish edge).

### D7. Consumer: round-robin multi-shard drain + scratch

`detail::Async_worker::run` replaces `while (producer_->pop(rec))` with: acquire-load `active`; for each shard `0..active-1` pop up to 16 records per shard per batch (bounded fairness slice, no starvation), render each record into sink-owned pre-allocated scratch (`Console_sink`: reuse 512 B line buffer + new 256 B render buffer; `File_sink`: 4 KiB JSON scratch unchanged), write, continue. Batch budget ≤ 64 records total as today; idle backoff (64 failed sweeps then 200 µs sleeps) unchanged in spirit. The flush handshake cell is untouched — its sequence semantics ("everything accepted before the request") still holds because producers publish into rings that the drain sweeps fully before `publish_sync`.

### D8. Sink/JSON rendering path

`format_json_line` gains the rendered message as input exactly like today — the *only* change is that `File_sink::handle`/`Console_sink::handle` first call `rec.render(rec, scratch, cap)` (D4 callback) to produce the user-message text, then escape it as before (Cyrillic pass-through already specified). `to_string(rec.level)` remains the JSON `"level"` spelling; the new vocabulary functions are never used here (json objects stay out of the worker loop).

### D9. Universal `to_string() const` inventory (cold diagnostics)

Member + free forms, each `std::string`, each documented cold-path-only:

- `core::SpscRingBuffer` — `SpscRingBuffer<T,Cap>(capacity=N, size=M, empty|full)` snapshot (const, any thread).
- `core::ErrorCode`, `logging::LogLevel`, `logging::Rotation_schedule`: enum forms per D2.
- `chrono::Timestamp` — `Timestamp(ns=...)`; `chrono::Stamp_source` — `Stamp_source(calibrated=…, mono_anchor_ns=…, last_ns=…)`; `chrono::Civil_time` — ISO-like `YYYY-MM-DD HH:MM:SS.nnnnnnnnn`.
- `logging::LogRecord` per D4; `logging::Dropped_stats` — per-cause counts + total; `Throttle_config`, `Rotation_config`, `FileLogger_config`, `ConsoleLogger::Config` — full field dumps (views printed inline, no NUL assumptions).
- `NullLogger` — `NullLogger(level=…)`; `ConsoleLogger`/`FileLogger` — `Name(level=…, shard_capacity=…, shards=32, pending=…, dropped=<Dropped_stats>)` from relaxed snapshots only; `CompositeLogger<...>` — own level, member count, and each member's `to_string()` via fold.

Assumption: existing spelling wins where the request used informal names — `Dropped_stats` (not "Drop_counters"), nested `ConsoleLogger::Config` (not "ConsoleLogger_config"); the contract, not the identifier, is what the mandate fixes.

### D10. Concept re-expression

`concepts::Logger` checks the literal call shapes in its requires body (`requires` expressions are unevaluated contexts, so `consteval` conversion from a literal works): `t.log(lv, "x")`, `t.info("x {}", 1)`-style requirements return `void`; the old Status-returning shape fails. Conformance `static_assert`s pin all four loggers as today.

## Risks / Trade-offs

- **R1 — empty-pack deduction edge cases**: `info("lit")` must select `FormatWithLocation<>` with an empty arg pack. Mitigation: spec-mandated compile tests for zero/one/many args; fallback is a non-template literal overload if a compiler quirk appears (GCC 15.2/Clang 18 both documented to accept this pattern).
- **R2 — `std::format` is not `noexcept`**: a checked `std::format_string` makes format-invalid runtime paths unreachable, but `format_to_n` can still in theory terminate under `-fno-exceptions` on allocator-independent internal faults. Accepted trade-off: same class of guarantee already granted to `std::expected` misuse rules; format strings are literals, fixed forever.
- **R3 — memory footprint**: 32 shards × `ShardCapacity` is a compile-time constant; a `FileLogger<8192,…>` mis-tuned would reserve tens of MiB. Mitigation: defaults at 128 per shard, documented sizing math, TSan/ASan test RSS kept under budget by small template instantiations in tests.
- **R4 — producer budget growth**: packing adds pointer/fat-arg stores (~4–8 ns) to reach the 20 ns cap. Mitigation: offset table is compile-time, pack is loop-free specialization expansion; Release latency gate stays the hard acceptance gate; float formatting no longer runs on the producer at all, offsetting previous eager-format costs of callers.
- **R5 — dangling string args**: passing `std::string_view` of an ephemeral buffer is a *lifetime* bug the old eager copy did not have. Mitigation: inline copy into the blob (the record owns the bytes) — the pack copies string content at emit, so no producer-time lifetime coupling remains; only pointer-valued args (raw `const char*`) carry caller responsibility, documented.
- **R6 — breaking API**: every existing caller that branched on `Status` from record methods must migrate; `dropped()`/drop-summary are the replacement signal. Mitigation: compile errors are loud and mechanical (nodiscard removal, arity change), Migration Plan below.
- **R7 — fetch-add registration races with `active` publication**: a late acquire-load may skip a just-registered shard for one sweep. Accepted: the next round-robin sweep drains it; records are never lost by skipping (only delayed); flush re-sweeps until every ring is momentarily empty.
- **Trade-off — json.hpp in enum headers**: +heavy include graph (compile time) for ADL simplicity; chosen over split headers (D1).

## Migration Plan

1. Land non-breaking layers first: D1 (CMake + define), D2 (enum vocabulary), D9 (`to_string` inventory) + their tests — existing API untouched, all suites still green.
2. Land the breaking surface atomically (single commit set): D3 `FormatWithLocation`, D4/D5 record + pack, D6 Producer sharding + D7 worker, D10 concept + all four loggers (the concept forces simultaneous conformance; no compatible intermediate state exists).
3. Retire the `NEXIGEN_LOGGING_BIND_PRODUCER` code path and SPSC-contract header comment blocks (replace with multi-producer wording).
4. Re-center the Release latency gate on the pack+push window; add the 4–8-thread stress test (must pass TSan/ASan).
5. Rollback story: steps 2–4 revert as a unit; step 1 is independent. Specs archive only after apply.
