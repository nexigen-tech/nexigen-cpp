# Proposal

## Why

`ChronoProvider` is today a daemon file-recorder, not a time source: in-process modules never read its snapshots — logging stamps through its own per-slot `StampSource` array, and logging, metrics, and tracing each call the free `sample_utc_offset()` and cache UTC offsets on their own (four separate `CLOCK_REALTIME` call sites). There is no injectable concept for the clock, so tests cannot fake time and the "thin, no-thread provider" the module needs is impossible without carrying the daemon machinery. Rebuilding the provider in place as the program's sole time authority (authority + issuer of per-source stampers, zero hot-path cost) and carving the periodic thread into a separate optional wrapper gives one provider type for both daemon and manual modes and unlocks the later module migrations (`loggers/metrics/tracing` parameterized on `concepts::Provider`).

Companion changes planned around this one: a mechanical change moves `logging::LogLevel` to `nexigen::core` first (this change then deletes `ClockLogLevel` and switches the worker-logger vocabulary to `core::LogLevel`); later changes migrate logging, then metrics/tracing, then `app::Infrastructure` mode selection onto this provider.

## What Changes

- **BREAKING**: `chrono::ChronoProvider` rebuilt in place as the threadless `chrono::Provider` — the sole clock authority: nested `P::Stamper` (per-source single-writer `StampSource`), fixed pre-calibrated bank of sources with cold-path `acquire()` issuance (full bank reports the new `ResourceExhausted`), type-erased sink binding, `snapshot_now()` / `flush()`, and the cold authority API `utc_offset()` / `utc_now_ns()` (+ existing pure `utc_from_mono_ns`).
- **BREAKING**: periodic snapshot thread carved out of the provider into the new `chrono::ChronoWorker` owner class (jthread, thread-spec application, idempotent start, worker logger binding): daemon mode = active `ChronoWorker` wrapping a `Provider`; manual mode = bare `Provider`.
- **BREAKING**: chrono vocabulary de-prefixed per the module's own file-per-entity convention: `ChronoProviderConfig` → `ProviderConfig`, `NullChronoSink` → `NullSink`, `ConsoleChronoSink` → `ConsoleSink`, `JsonFileChronoSink(-Config)` → `JsonFileSink(-Config)`, `CompositeChronoSink` → `CompositeSink`, concept `Concepts::ChronoSink` → `Concepts::Sink`; headers renamed to match.
- `chrono::Settings` carries a new `kind` field (`ProviderKind`: `daemon`, `manual`), merged like other provider fields, serialized, and validated best-effort: worker attributes set while `kind` is `manual` are recorded as ignored-field diagnostics. Mode *consumption* by `app::Infrastructure` lands in a later change; this change only makes the field load/serialize correctly.
- `core::ErrorCode` gains the append-only enumerator `ResourceExhausted` (bank issuance rejected because every pretrained source is issued).
- The worker logger vocabulary becomes `core::LogLevel` (`ClockLogLevel` and its header are deleted; the module-local mini-level collapses into the shared one).
- `ChronoLoggerBridge` in `app::Infrastructure` is updated for the new worker API, but behavior stays daemon-shaped (worker always wrapped when the interval is non-zero); `kind`-driven selection is explicitly out of scope.
- The free `sample_utc_offset()` intentionally stays public this change: logging, metrics, and tracing still consume it until their migrations; the provider becomes the authority design-wise and removes the duplicate reads in its own paths.

## Capabilities

### New Capabilities

- `chrono/provider`: the threadless clock authority — one nested `Stamper` per issued source; bank capacity, `acquire()` issuance, calibration-on-prepare; cold authority API (UTC offset sampling and conversion, wall `utc_now_ns`, snapshot push to the bound sink, flush, diagnostics); provider configuration with `kind` and complete serialization/formatting contract.
- `chrono/worker`: the periodic snapshot daemon wrapper over a `Provider` — fixed-rate delivery loop, worker thread-spec application with `Required`/best-effort semantics, idempotent start, `core::LogLevel` logger binding with double-start rejection, zero-interval no-op, and clean shutdown that stops the worker and delivers a final snapshot before joining.

### Modified Capabilities

- `chrono/chrono-provider`: all ten requirements are REMOVED — the capability retires; its behavior is superseded by `chrono/provider` (authority, sampling, config, diagnostics) and `chrono/worker` (periodic delivery, affinity, logger, shutdown).
- `chrono/chrono-settings`: ADDED provider-kind setting (typed, merged file-only, validated with ignored-field diagnostics, serialized) and MODIFIED apply/serialization requirements so merged settings transfer `kind` into `ProviderConfig`.
- `core/result`: MODIFIED error code catalogue — `ResourceExhausted` joins the append-only enumerator set with its own naming/serialization/format scenarios.

## Impact

- **Code**: `include/nexigen/chrono/` (provider/provider_config/new parent-kind headers, renamed sink headers), `src/chrono/` (`provider.cpp`, `worker.cpp`, `provider_config.cpp`, `settings.cpp`), `tests/chrono/`, `examples/chrono/ex_chrono_snapshot.cpp`, and minimal compile-keeping adjustments in `app/infrastructure.hpp` (`ChronoLoggerBridge`, worker construction).
- **APIs**: public chrono surface renamed and reshaped as above; `app::Infrastructure::chrono()` keeps returning the provider reference (its accessor surface and spec text change in the later Infrastructure change); free `sample_utc_offset()` stays.
- **Performance**: the issued-stamper hot path keeps byte-identical machine code to today (`__rdtsc()` + anchor arithmetic + one local store for the `last_ns_` clamp — no atomic, no allocation, no `CLOCK_REALTIME`, no call through the provider); cold authority calls are statically dispatched and inlined; the daemon's ns profile is unchanged (same fixed-rate loop, same thread-spec application, cache-line-separated control cells). Memory: steady state stays zero-allocation (fixed `alignas(64)` bank of `StampSource`s, no containers); the provider's base object shrinks by moving the worker's `std::jthread`, mutexes, condition variable, and startup atomics into `ChronoWorker`; bank capacity is a compile-time template parameter, so overdraft is a modeled `ResourceExhausted` outcome rather than hidden growth.
