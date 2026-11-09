# Design

## Context

The provider rebuild is in-place: `chrono::ChronoProvider` (thread + sink + sampling in one object, `src/chrono/chrono_provider.cpp`) is re-shaped into the threadless authority `chrono::Provider` plus the daemon wrapper `chrono::ChronoWorker`, with module vocabulary de-prefixed (`ChronoProviderConfig` → `ProviderConfig`, sink family renamed, `ClockLogLevel` deleted). Consumers inside the module (settings loader, sinks, tests, `ex_chrono_snapshot`) and the compile-keeping path in `app/infrastructure.hpp` move in this change; logging/metrics/tracing keep calling the free `sample_utc_offset()` until their own migrations. `core::Result`/`Status` error model, `core::ThreadSpec`/`ThreadApplyReport`, and type-erased `detail::SinkBinding`/`LogBinding` carry over untouched except the logger signature switching to `core::LogLevel` (from the companion `core/log-level` change).

## Goals / Non-Goals

**Goals:**
- One provider type for both modes: daemon presence = an active `ChronoWorker`; manual = bare `Provider`.
- Hot-path parity: an issued stamper's `sample()` compiles to the same machine sequence as today's `stamps_[slot].sample()` (`__rdtsc()` + anchor arithmetic + one local store; no atomic, no wall clock, no allocation).
- Single authority surface: all wall-clock sampling (`CLOCK_REALTIME`) funnels through the provider's cold API (`utc_offset()`, `utc_now_ns()`, snapshot construction).
- Deterministic testability: `concepts::Provider` + `concepts::Stamper` admit a fake provider/stamper without code hooks.

**Non-Goals:**
- No `app::Infrastructure` mode selection by `chrono::Settings::kind` (phase E change); infra keeps today's daemon-shaped behavior.
- No module migrations of logging/metrics/tracing (phases C/D); they still consume the free `sample_utc_offset()`, which stays public.
- No change to snapshot value shape, JSON wire format, thread-spec vocabulary, or any sink's I/O behavior.

## Decisions

**D1 — Two-level concept (variant D).** `concepts::Stamper` (per-source, hot) + `concepts::Provider` (authority, cold) live in `include/nexigen/chrono/concepts/`:

```cpp
template <class S>
concept Stamper = requires(S& s) {
    { s.sample() } noexcept -> std::same_as<Timestamp>;
};

template <class P>
concept Provider = requires(P& p) {
    typename P::Stamper;
    requires Stamper<typename P::Stamper>;
    { p.acquire() } noexcept -> std::same_as<core::Result<typename P::Stamper*>>;
    { p.utc_offset() } noexcept -> std::same_as<std::int64_t>;
    { p.utc_now_ns() } noexcept -> std::same_as<std::int64_t>;
    { p.snapshot_now() } noexcept -> std::same_as<core::Status>;
    { p.flush() } noexcept -> std::same_as<core::Status>;
    { p.to_string() } -> std::same_as<std::string>;
};
```

Alternatives rejected: a shared volatile-free provider method on one object would need either per-call vDSO reads (20-25 ns — the whole enqueue budget per `stamp_source.hpp`), atomics on the anchoring state (cache-line ping-pong across producers), or `thread_local` hiding global state (hidden implicit state the codebase avoids; broken test-injection shape). Issued per-source stampers keep the "everyone holds a provider reference" dream while the hot loop calls the stamped instrument, not the provider.

**D2 — Provider memory layout (cold init, hot stamping).** `template <std::size_t MaxSources = 64> class Provider` — bank capacity is a template parameter so steady state never reallocates and bank sizing is a per-instantiation, per-app decision:

```cpp
alignas(64) std::array<StampSource, MaxSources> bank_;   // one cache line per source cell
std::atomic<std::size_t>       next_free_{0};            // relaxed fetch_add: uniqueness, no ordering needed
std::atomic<bool>              prepared_{false};         // release store at prepare end / acquire load in acquire()
std::atomic<bool>              calibration_failed_{false}; // relaxed; retried by prepare()
ProviderConfig                 config_{};                // interval + worker spec + kind (worker fields idle in manual mode)
detail::SinkBinding            binding_{};               // unchanged type erasure (fn pointers only)
std::mutex                     snapshot_mutex_{};        // cold path only: serializes manual + worker snapshot delivery
```

- `prepare()`: `calibrate()` every source (cold thread; the calibration briefly sleeps — never call on an RT thread), then `prepared_.store(true, release)`.
- `acquire()`: `prepared_.load(acquire)` → `NotInitialized` if false; `next_free_.fetch_add(1, relaxed)` gives uniqueness; `>= MaxSources` → `ResourceExhausted` (first reader of the appended `ErrorCode`). Returns `StampSource&` from the bank cell — no lifetime question because the provider outlives all issuers by construction (module members hold provider references, mirroring infra ownership order).
- Snapshot stamping reuses a module-internal source approach as today: one `Timestamp::now()` (monotonic-raw vDSO) + one `clock_gettime(CLOCK_REALTIME)` pair under `snapshot_mutex_`; the mutex is cold-path-only (snapshot cadence), keeping conduct honest with the no-locking hot-path mandate.
- `utc_offset()` = fresh `sample_utc_offset()` pair each call (documented cold semantics: two clock reads, no allocation). `utc_now_ns()` = the same pair composed through `utc_from_mono_ns`. Both are plain non-inline cold helpers; `snapshot_now()` reuses them internally so snapshots bake exactly the authority's offset.

**D3 — ChronoWorker carve-out.** `template <concepts::Provider P> class ChronoWorker` holds `P& provider_` plus everything that left the provider: `std::jthread`, the fixed-rate loop (`wake_cv_.wait_until` against `steady_clock` deadlines, no backlog chase), `std::mutex`/`condition_variable_any`, and the startup control cells:

```cpp
alignas(64) std::atomic<std::uint64_t> worker_mask_{0};
alignas(64) std::atomic<core::ErrorCode> startup_error_{Ok};
core::ThreadApplyReport worker_thread_report_{};
std::atomic<bool> startup_published_{false};
```

Memory ordering carried verbatim from the old loop: `startup_published_` is a release store from the worker and acquire loads in `start()`/report readers; `worker_mask_` stays relaxed (diagnostic only); `Required` attribute failure aborts the loop before any snapshot, joins, and fails `start()` — old semantics byte-for-byte. `start()`/`start(logger)`/`startup_status()`/`worker_thread_report()`/`worker_mask()`/`to_string()` all move here; the logger-binding contract (bind before start, `Conflict` on running rebinding, TSan-clean) carries over with `log(core::LogLevel, std::string_view)`.

**D4 — Destruction contract.** Provider destructor: flush only (no snapshot — removes the old hidden final snapshot). Worker destructor: stop → join → one final `provider_.snapshot_now()` → flush. In `app::Infrastructure` teardown the destroy order becomes worker → provider → sink, preserving today's observable "final snapshot + flush on shutdown".

**D5 — `kind` plumbing.** `ProviderKind` (`daemon`, `manual`) is a `uint8_t` scoped enum in its own `provider_kind.hpp` with the standard conversion set (constexpr `to_string`/`from_string` over `core::EnumTraits`, ADL json via `core::mandate`, `std::formatter`). `Settings`/`apply_to`/config JSON transfer it; manual+configured-worker attributes produce one `SettingsDiagnostic` (ignored fields) while values remain stored for diagnostics. Infrastructure ignores `kind` this change (daemon shape always) — selection is the phase E change.

**D6 — Renames.** Mechanical, file-per-entity aligned: `chrono_provider.hpp` → `provider.hpp` (+ `worker.hpp`, `provider_config.hpp`, `provider_kind.hpp`), sources in `src/chrono/` follow; sink family → `null_sink.hpp`, `console_sink.hpp`, `json_file_sink.hpp`, `composite_sink.hpp`; `concepts/chrono_sink_concept.hpp` → `sink_concept.hpp`; new `concepts/provider_concept.hpp`, `concepts/stamper_concept.hpp`. `ClockLogLevel`/`clock_log_level.hpp` deleted; `LogBinding` signature becomes `(void*, core::LogLevel, std::string_view)`; `detail::ChronoLoggerBridge` in `app/infrastructure.hpp` keeps its switch (level-to-Logger-method mapping), now over `core::LogLevel`. `ChronoProviderConfig` → `ProviderConfig` with `to_string`/`to_json`/`from_json`/formatter ported and `kind` added.

**D7 — Serialization mandates.** Every renamed/new type in this change ports or gains the regime conversion set (member+free `to_string`, constexpr enum names, ADL json, `std::formatter`): `ProviderConfig` (round-trip with kind), `ProviderKind` (canonical lowercase `daemon`/`manual`), `Provider` (state rendering: interval, kind, capacity, issued count), `ChronoWorker` (interval, thread spec, startup state).

## Risks / Trade-offs

- [Breaking renames ripple to every TU touching chrono] → all in-tree consumers (settings loader, infra, examples, tests) move in the same change; the proposal marks them BREAKING; CMake file list updated together with the headers so nothing links half-renamed.
- [Bank capacity must cover every future issuer] → default `MaxSources = 64` comfortably holds today's needs (logging ≤ 32 producers + tracing ≤ 32 slots fill an 8 KiB `.bss`-resident array only when instantiated at that width); apps pick their own capacity; overflow is a modeled `ResourceExhausted`, never silent growth.
- [acquire() on an RT thread would sleep] → contract is cold-path issuance computed at init like today's per-slot calibration; the spec pins `acquire()` as cold, and tests assert issuance allocates nothing and never blocks after `prepare()`.
- [`kind` fields idle until phase E] → stored, validated, serialized with diagnostics this change; no silent behavior difference for daemon deployments (default `daemon` matches today).
- [Worker-required-failure semantics regressions] → loop, startup cells, and report flow carried over verbatim plus a parity test matrix in the tasks.

## Migration Plan

1. Companion change `move-log-level-to-core lands first (enumerators, traits, JSON, formatter re-homed; `logging::LogLevel` shifted onto `core`).
2. This change lands as one compiling unit: renames + provider/worker split + `ResourceExhausted` + kind plumbing + test/example updates.
3. Rollback = revert the single change (no wire-format or data-format effects; JSON files gain nothing new at runtime since `kind` defaults to `daemon`).
4. Phases C/D/E land after, each migrating one consumer group.

## Open Questions

None blocking. Post-change tuning (bank default width, worker template vs concrete bind for infra ergonomics) stays executable inside the phase E change without touching these specs.
