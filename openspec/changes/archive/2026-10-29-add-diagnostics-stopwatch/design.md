# Design

## Context

The `nexigen::diagnostics` module is the last module slot declared in the project's namespace/directory mapping without an implementation. `nexigen::chrono` already provides the stamp surface the timer needs: `Timestamp::now()` (~20–25 ns vDSO, `CLOCK_MONOTONIC_RAW`) and `Stamp_source::sample()` (~3–5 ns calibrated TSC, single-producer owned, monotone per object). Serialization contracts (constexpr enum names, non-throwing nlohmann hooks, `std::formatter` specializations) are established by `log_level.hpp` / `span_enums.hpp` / `policy_enums.hpp`, and the test harness style (one TU per module, zero-dependency, Release-only latency gates behind a `NEXIGEN_*_STRICT_LATENCY` define) is established by the four existing module test TUs. Everything else relevant is a consequence of the real-time mandates: `-fno-exceptions`, zero hot-path allocation, no locks, no `std::string`/JSON on hot paths.

**Recorded assumptions** (kept minor per the propose workflow; each is a Decision below):

- "…and integer microseconds" → an additional integer whole-microsecond query named `elapsed_us_count()` alongside the requested `elapsed_us() -> double`.
- `LapSnapshot` (named only in the serialization mandate) → a trivially copyable snapshot of the latest lap, materialized by a non-mutating `Stopwatch::lap_snapshot()` query.
- `lap()` while not `Running` is a no-op returning 0 (no phantom laps, ever).
- "Exceeded" budget means strictly greater than the threshold (`elapsed > budget`).
- `min_lap_ns()` reports `UINT64_MAX` ("no lap") before the first lap, so a minimum is distinguishable from an empty set.
- Enum spellings follow the tracing/metrics convention: PascalCase matching the enumerator (`"Idle"`, `"Running"`, `"Stopped"`), not `log_level`'s lowercase.

## Goals / Non-Goals

**Goals:**

- A sampling interval timer whose every hot op (start/stop/reset/restart/lap/elapsed/range queries, guard construct/destroy) is one clock sample plus register work: no allocation, no lock, no atomic, no formatting, no exit path that can throw.
- A single 64-byte `Stopwatch` value type with a fully statically pinned layout, trivially copyable, embeddable in hot loop structures without false-sharing or hot-state split.
- Exact module conventions: enums carry the universal serialization contract; every public type carries cold-path `to_string`; CMake target and CTest wiring mirror the existing five modules.
- A general-purpose `ScopedStopwatch` that reports to a raw reference or an arbitrary non-throwing sink without type erasure.

**Non-Goals:**

- No thread-safety, no cross-process semantics, no synchronization vocabulary for a stopwatch — fresh instance per thread is the contract (single-thread measuring domain; `chrono` sources are already single-producer).
- No histogram/percentile machinery for lap distributions (`metrics` owns aggregates; a future change can feed laps into it via `LapSnapshot`/sinks).
- No new third-party dependencies; no `nih`-style vendoring; no simulator-facing scheduling primitives (deadline sleepers etc.) beyond measurement.
- No wall-clock/UTC reporting from timers (the raw monotonic domain is interval-true, UTC-stamping stays `chrono`'s offset snapshot story).

## Decisions

### D1 — Build shape: compiled STATIC target with a single cold-path unit

`nexigen_diagnostics` mirrors `nexigen_chrono`/`nexigen_metrics`: STATIC library, `file(GLOB ... src/diagnostics/*.cpp CONFIGURE_DEPENDS)`, PUBLIC `nexigen_core` + `nexigen_chrono` (headers carry `chrono::Timestamp`/`Stamp_source*` in their public surface), no `Threads::Threads`, no `_GNU_SOURCE` (no affinity, no `gettid`, no background worker in this module). Registered in the root `CMakeLists.txt` after `nexigen::tracing`.

All sampling state machines and the templated guard are inline in headers; only slow, allocation-bearing diagnostics rendering compiles: `src/diagnostics/stopwatch_format.cpp` defines `Stopwatch::to_string()` and `LapSnapshot::to_string()` (declared in the headers, defined out-of-line exactly like `metrics/snapshot_format.cpp`). `ScopedStopwatch::to_string()` stays inline in the header because the class is a template.

*Alternative considered*: fully INTERFACE (header-only) target like `nexigen_core` — rejected: the module declares `src/diagnostics/` and a compiling unit's diagnostics rendering is the natural cold-path resident; the STATIC target also gives downstream an explicit link-time dependency.

### D2 — `Stopwatch` layout: 64-byte, single cache line, zero atomics

```cpp
class alignas(64) Stopwatch {          // nexigen::diagnostics
    chrono::Timestamp   anchor_;        //   8: sample where the active run began (start/restart)
    chrono::Timestamp   lap_anchor_;    //   8: sample where the current lap segment began (start/restart/lap)
    std::uint64_t       accumulated_ns_; //  8: sum of closed intervals
    std::uint64_t       last_lap_ns_;    //  8
    std::uint64_t       min_lap_ns_;     //  8: UINT64_MAX sentinel == "no lap"
    std::uint64_t       max_lap_ns_;     //  8
    chrono::Stamp_source* source_;      //  8: borrowed sampler; nullptr = default clock
    std::uint32_t       lap_count_;      //  4: wraps ~never (documented; 64K laps/s for 3 days)
    StopwatchState      state_;          //  1
};                                      // 61 -> 64 natural/padded -> 64 under alignas(64)
static_assert(sizeof(Stopwatch) == 64);
static_assert(alignof(Stopwatch) == 64);
static_assert(std::is_trivially_copyable_v<Stopwatch>);
```

Rationale:

- `alignas(64)` although the type has no atomics: it is embedded in real-time loop objects that do share cache lines with hot neighbors; a self-contained line guarantees no partial-line sharing and lets the `design` rule's cache isolation hold without downstream thought. There are **no `std::atomic` members and therefore no `std::memory_order` choices** — the measuring domain is single-threaded (Decision D3); this is the explicit concurrency analysis for the concurrent-component rule.
- 8-byte fields first, sub-4-byte fields last keeps natural packing at 53 bytes (`sizeof` 56 unaligned), so `alignas(64)` costs at most one line of padding.
- Trivially copyable: copying a mid-run stopwatch yields a consistent independent snapshot (anchors + accumulated copied together); assigned nothing dynamic — no destructor, no move semantics needed. Copies are rare by design but legal and useful (snapshot copies for cold-side logging).
- `lap_count_` is `uint32_t`: 4.29×10⁹ laps is unreachable at realistic lap rates; the win is a one-line `Stopwatch`.

*Alternative considered*: `uint64_t lap_count` with `alignas(128)` two-line object — rejected: larger type, wider cache footprint, no benefit.

### D3 — Sampling and the clock-failure policy

A single inline helper drives all sampling:

```cpp
[[nodiscard]] std::uint64_t sample_ns_() const noexcept {
    return source_ != nullptr ? source_->sample().nanoseconds() : Timestamp_now_ns_();
}
```

Both paths are monotone by contract (`CLOCK_MONOTONIC_RAW` never decreases; `Stamp_source::sample()` clamps per object), so `Stopwatch` keeps **no** second monotonic clamp field — elapsed arithmetic is plain unsigned subtraction and can never wrap backwards.

Error mapping (`Timestamp::now()` is `Result`-based; failure is a real but practically-impossible class — one `clock_gettime` EINVAL path, no spurious errors observed in the wild). Under `noexcept void` state-control signatures the transition is **not committed** on a failed read, which keeps every observable honest and never fabricates data:

- `start()`: read fails → state stays whatever it was (`is_running()` reports truthfully; the caller sees `Idle`/`Stopped` and can retry).
- `stop()`: read fails → interval stays open, state stays `Running`; a later successful `stop()` closes the full interval; an elapsed query meanwhile keeps returning the opening total.
- `lap()`: read fails → returns 0, anchor not advanced, no counter touched (no phantom lap).
- `restart()`: read fails → total and state untouched (still running with the old anchor); the return does not fabricate a "closed" value.
- elapsed query while `Running`: read fails → returns the accumulated total alone (reading freezes until the clock recovers; monotone work accounting is never violated).

`Stamp_source::sample()` never fails (it self-falls back internally), so a calibrated-source stopwatch has no failure branch taken at all on x86-64.

*Alternative considered*: a hidden `last_valid_stamp_ns_` clamp/fallback field (the `Stamp_source` pattern) — rejected: it forces a 128-byte class to hold honest fallback semantics, and reusing stale stamps would fabricate a *plausible-but-wrong* reading, which is worse than a frozen observable state. The uncommitted-transition policy surfaces failure instead of encoding it.

### D4 — Control-path arithmetic (the exact operations)

All in `noexcept`, 2–5 instructions past the sample:

- `start()`: if `state_ == Running` → no-op (idempotent). Sample `s`; `anchor_ = s; lap_anchor_ = s`; `state_ = Running`. (`Stopped` resume keeps `accumulated_ns_`; `Idle` starts from 0.)
- `stop()`: if `state_ != Running` → no-op (idempotent, `[[maybe_unused]]` void). Sample `s`; `accumulated_ns_ += s − anchor_`; `state_ = Stopped`. **Lap statistics are left untouched** — the final partial interval after the last `lap()` is deliberately not a lap (Decision: laps are explicit only; recorded in the spec's lap-statistics scenarios).
- `reset()`: zero `accumulated_ns_`, `last_lap_ns_`, `min_lap_ns_ = UINT64_MAX`, `max_lap_ns_`, `lap_count_`; `anchor_` stale value overwritten at next `start()`; `state_ = Idle`.
- `restart()`: commit path when `Running`: sample `s`; `total = accumulated_ns_ + (s − anchor_)`; `accumulated_ns_ = 0`; `anchor_ = s; lap_anchor_ = s` — one sample serves both close and re-open, which is precisely the zero-gap property. `Stopped` path: report `accumulated_ns_`, zero it, then sample-and-run as a fresh interval. `Idle` path: report 0, sample-and-run. Always ends `Running`.
- `lap()`: `Running` only: sample `s`; `dur = s − lap_anchor_`; `lap_anchor_ = s`; update `last/min/max` + `lap_count_` (first lap initializes min from the `UINT64_MAX` sentinel via the ordinary `dur < min_` branch). Two anchors by design: `anchor_` keeps the whole-run reading (elapsed stays the sum of stage laps plus the live tail, per the examples spec scenario), while `lap_anchor_` isolates each split segment.

Elapsed composition: `Running` → `accumulated_ns_ + (sample − anchor_)`; `Stopped` → `accumulated_ns_`; `Idle` → 0. Double conversions divide the same `uint64_t` once (`/1000.0`, `/1e6`, `/1e9`) — the µs double and the truncated integer µs (`elapsed_ns() / 1000`) both derive from the single ns total, per the multi-unit consistency requirement.

### D5 — Enum contract: `stopwatch_state.hpp`, PascalCase spellings, exact pattern copy

`include/nexigen/diagnostics/stopwatch_state.hpp` is a structural copy of the `span_enums.hpp` pattern: `enum class StopwatchState : std::uint8_t`; `[[nodiscard]] constexpr std::string_view to_string(StopwatchState) noexcept` (switch + `default: return "Unknown";` for garbage-cast safety); `[[nodiscard]] constexpr bool from_string(std::string_view, StopwatchState&) noexcept` (exact-match, out-param untouched on failure); ADL `to_json` writing `std::string(to_string(v))` and non-throwing `from_json` checking `get_ptr<const nlohmann::json::string_t*>()` then delegating to `from_string` (concrete under `JSON_NOEXCEPTION=1`, which `nexigen_core` already defines PUBLIC); `std::formatter<StopwatchState> : std::formatter<std::string_view>` in `namespace std` with a templated context (libstdc++ 15 formattability quirk, same workaround as `log_level.hpp`).

*Alternatives considered*: `log_level`-style lowercase names (`"idle"`) — rejected: the tracing/metrics modules (the majority recent convention) render PascalCase matching the enumerator, and the identical spelling keeps `from_string` a pure inverse.

### D6 — `LapSnapshot`: value struct, state-first layout, 24 bytes

```cpp
struct LapSnapshot {
    StopwatchState state;        //   1
    std::uint32_t  lap_number;   //   4 (padded 1–4)
    std::uint64_t  lap_ns;       //  16
    std::uint64_t  total_ns;     //  24
};
static_assert(sizeof(LapSnapshot) == 24);
static_assert(std::is_trivially_copyable_v<LapSnapshot>);
```

`Stopwatch::lap_snapshot() const noexcept` materializes it from the current counters without sampling or mutating (pure read). `lap_number` is one-based (`lap_count_`); with no laps yet both are 0 and `lap_ns` is 0. A defaulted `operator==` accompanies the struct (value semantics for tests and snapshots; trivially copyable preserved). Serialization: member + free cold-path `to_string` in `stopwatch_format.cpp` (per mandate 9 — structs need no JSON/formatter contract; no `from_json` since there is nothing to restore).

### D7 — `ScopedStopwatch`: single class template + CTAD, sink stored inline

```cpp
template <class Sink>
class ScopedStopwatch {
    // Sampled at construction; on destruction: elapsed = sample() - anchor_, delivered once.
};
```

One class covers both destination kinds, avoiding type-erased `std::function` (which would allocate) while keeping the user-facing spelling dead simple via deduction guides:

- `ScopedStopwatch(std::uint64_t& out_ns)` → CTAD binds `Sink = detail::OutRefSink` (a tiny internal writer storing `std::uint64_t*`).
- `ScopedStopwatch{lambda}` → CTAD binds the lambda type; the sink is stored **by value inline** — zero allocation, zero indirection.
- Optional leading `chrono::Stamp_source*` and trailing `std::uint64_t budget_ns` (stored default; `0` = none) combine in each flavor: the six-constructor overload matrix `(source*, dst [, budget])` where `dst ∈ {uint64_t&, Sink&&}`.
- Disambiguation rules: the non-template `uint64_t&` constructors win exact matches over the templated sink constructor; the templated sink constructor `static_assert`s `!std::is_integral_v<std::remove_cvref_t<Sink>>`, so `ScopedStopwatch{out, budget}` can never bind `budget` to a sink slot.
- Sink contract: `std::is_invocable_v<Sink&, std::uint64_t>`; destruction calls `invoke(elapsed)` — under the build's `-fno-exceptions` nothing can throw; a separate `is_nothrow_invocable` gate is deliberately NOT imposed (it would reject annotated-not lambdas needlessly).
- Guard is non-copyable/non-movable: it owns exactly one destination and one measurement; nothing to hand off.

Member layout per instance is `(anchor_ 8, source_ 8, default_budget_ 8, sink N, bool)` ≈ 40–56 B with the common reference-capturing sink (`N = 8`). **No `alignas(64)` on the guard**: it is an ephemeral per-scope stack object with no shared-line surface and no atomics (single-thread domain — the explicit concurrency analysis); the 64-byte isolation guarantee belongs to the long-lived `Stopwatch`, not to a temporary.

Destruction semantics: sample once; compute elapsed; deliver to the destination exactly once; no flag churn beyond a `bool reported_` (destructor runs once anyway per object lifetime, so the flag is only a belt for the `noexcept` analysis, not a double-delivery risk).

### D8 — Guard budget check

`has_exceeded_budget(std::uint64_t budget_ns) const noexcept`: sample now, `return (sample − anchor_) > budget_ns;` — strictly greater, matching "exceeded". The no-argument overload tests against the stored `default_budget_` and returns `false` when `0` (none configured). Mid-scope checks never mutate the guard and never deliver early. Cost: one sample (~5 ns calibrated).

### D9 — Guard diagnostics: partial `std::formatter` specialization for the template

Member `to_string()` defined inline in the header (template — cannot leave the header), reporting active status, elapsed-so-far and the pending default budget; free `to_string(const ScopedStopwatch<S>&)` mirrors it; `template <class Sink> struct std::formatter<nexigen::diagnostics::ScopedStopwatch<Sink>> : std::formatter<std::string_view>` delegates to the rendered text — the same partial-specialization shape the tracing module established for `ScopedSpan<SC, MT>`. No `from_json` (a live guard is not restorable state — mirrors `ScopedSpan`).

### D10 — Test TU, latency harness, and the Release overhead benchmark

`tests/diagnostics/diagnostics_test.cpp`: one TU on the zero-dependency harness (CHECK-style macros, static_assert blocks), registered as `nexigen_diagnostics_tests` with `TIMEOUT 600` and the Release/RelWithDebInfo-only gate `NEXIGEN_DIAGNOSTICS_STRICT_LATENCY` via `$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>>` — identical mechanically to the four existing gates (`NEXIGEN_LOGGING_STRICT_LATENCY` etc.).

Latency methodology (the "start() + stop() overhead benchmark"): batches of ≥1 000 measured pairs on a warmed-up loop; each batch wall-timed by an outer calibrated source; per-op nanoseconds = batch time / ops with the skeleton cost bounded by construction; track the **median** batch (robust vs. scheduler spikes). Gates: `start()+stop()` ≤ 100 ns (default clock) and ≤ 20 ns (`Stamp_source`); `restart()` and `mid-run elapsed` ≤ 20 ns calibrated; guard construct+destruct ≤ 20 ns calibrated. Gates compile out in Debug/sanitizers (correctness-only there). Allocation freedom asserts via the same pre-arm/post-check counting hook the other module tests use.

### D11 — Example and documentation

`examples/diagnostics/ex_stopwatch.cpp` demonstrates the three spec behaviors: per-tick budget checks with `ScopedStopwatch` (prints tick elapsed vs. budget, flags overruns, prints a run summary), multi-stage `lap()` profiling (per-stage durations + min/max/count + total), and a `restart()` round (accumulated total, then a fresh accumulation) plus an out-param guard writing `std::uint64_t&`. Registered via `nexigen_add_example(ex_stopwatch ex_stopwatch.cpp nexigen::diagnostics)` (`examples` label, 600 s timeout, exit 0). README: new `nexigen::diagnostics` API section (quickstart for `Stopwatch`/`ScopedStopwatch`, link notes for `nexigen::diagnostics`) and the Runnable Examples list naming eleven executables.

## Risks / Trade-offs

- [Clock-failure transitions are "silent" by-design under void signatures] → The policy is documented (Decision D3): state and counters never lie, `is_running()` and frozen readings make the failure observable; `Stamp_source` monitors never hit the branch on x86-64.
- [Release latency gates can flake under a noisy CI host] → Median-of-batches methodology with generous headroom (D10: 100 ns budget vs. ~50 ns expected; 20 ns vs. ~5–10 ns calibrated), gates compiled out outside Release, bounded-time (not ns-asserted) under sanitizers/Debug.
- [`ScopedStopwatch` CTAD makes the class a template] → Users of the plain out-param flavor never spell the parameter; constructor static_asserts produce clear diagnostics for the `ScopedStopwatch{out, budget}` integral-binding trap.
- [`lap_count_` as `uint32_t` can wrap] → Documented: ≥4.29×10⁹ laps is unreachable; `reset()` re-arms. Wrap behavior stays defined (plain increment), never UB.
- [Busy-wait-based elapsed tests can be theme-flaky on very slow emulators] → Plausibility windows (bounded, generous) rather than exact durations in time-dependent scenarios; deterministic formulas verified via arithmetic checks that don't depend on wall time.
- [New `.cpp` unit enables CMake glob changes] → Single `file(GLOB CONFIGURE_DEPENDS)` pattern already in use for all four modules; no root `CMakeLists.txt` restructuring needed beyond the new target block.
