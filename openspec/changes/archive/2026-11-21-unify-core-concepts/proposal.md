# Proposal

## Why

Concept vocabulary is scattered: `Message` lives in `com` (and `exec` must reach into `com` to
assert it), the minimal logger contract in `chrono`, counter/meter concepts in `metrics`, and sink
concepts in three modules with three different method names (`on_snapshot`, `on_batch`, `on_span`)
plus a fourth, duck-typed contract in `logging`. Modules that only need the shape cannot reuse it
without depending on the module that happens to own it.

## What Changes

- Add `core/concepts/` with the **minimal shared vocabulary needed to keep modules independent**:
  `Message`, `LevelLogger`, `Logger`, `Sink`, `LifecycleSink`, `CounterLike`, `MeterLike`; add
  `core/null_meter.hpp` and `core/sink_binding.hpp`.
- Unify sink delivery on `on_event(const Event&)` returning `core::Status` and `flush()`, with
  optional `start()`/`stop()`. Rename `chrono::on_snapshot`, `metrics::on_batch`,
  `tracing::on_span`, and `logging::handle`/`synchronize` accordingly. **BREAKING**.
- Replace the three `detail::SinkBinding` copies with one `core::SinkBinding<Event>`; providers
  keep type erasure and stay non-templated on the sink type.
- Give `logging` a formal record-sink concept instead of duck typing.
- Turn `com::concepts::Message`, `com::concepts::EventPayload`, and `chrono::concepts::Logger` into
  aliases of the core concepts.
- Keep domain-shaped concepts in their modules: `chrono::{Provider, Stamper}`, `metrics::{Metric,
  ReadableMetric}`, `tracing::{Tracer, SpanGuard}`. No domain type moves to `core`.
- Leave single-module concepts where they are. A concept moves to `core` only when it removes a
  dependency edge between modules; relocating a concept that already lives in `core` or is named by
  one module is churn, not decoupling.

**BREAKING**: sink method names change in `chrono`, `metrics`, `tracing`, and `logging`.

## Capabilities

### New Capabilities

- `core/sink-concepts`: the uniform sink delivery contract and the shared `SinkBinding<Event>`.
- `core/concepts`: the generic `Message`, `LevelLogger`, and full `Logger` concepts shared by lower
  layers.
- `core/meter-concepts`: `CounterLike`, `GaugeLike`, `StateGaugeLike`,
  `DescriptiveStatisticsLike`, `HistogramLike`, `MeterLike`, `InstrumentMeterLike`, and
  `NullMeter` (moved out of the recorder change so both changes share one definition).

### Modified Capabilities

- `com/publish-subscribe`: `Message` is an alias of the core concept.
- `chrono/chrono-sink`: sinks deliver through `on_event` and the shared binding.
- `chrono/worker`: the worker's logger concept is the core `LevelLogger`.
- `metrics/metric-sinks`: sinks deliver through `on_event` and the shared binding.
- `metrics/metric-provider`: `Meter` and its counters model the core concepts.
- `tracing/tracer-concepts`: `TraceSink` is expressed through the core sink concepts.
- `tracing/tracer-provider`: the provider binds through `core::SinkBinding<SpanRecord>`.
- `logging/multi-producer`: the consumer sink is a core `LifecycleSink`, not duck typing.

## Impact

- **Code**: `include/nexigen/core/{concepts,null_meter,sink_binding}`, all sink implementations and
  providers in `chrono`, `metrics`, `tracing`, `logging`, `com` concept aliases; tests in each
  module; `docs/architecture.md` module contracts.
- **Dependencies**: none new; `core` stays free of `chrono`, `metrics`, `tracing`, `com`.
- **Performance**: sink calls stay on cold paths; the shared binding is the same function-pointer
  erasure, and `NullMeter` keeps disabled counters at zero cost.
- **Out of scope**: `diagnostics`, `phm`, `sm`, `exec`, `synchro`, and `recorder` have no concept
  headers today; introducing concepts where virtual interfaces exist (`exec::ControlHost`) is a
  separate change with its own design.
