# Design

## Context

See `proposal.md` — Why. Current state:

- Concepts live in `chrono`, `com`, `logging`, `metrics`, and `tracing`; none in `diagnostics`,
  `phm`, `sm`, `exec`, `synchro`, or `recorder`.
- Sink delivery has four shapes: `on_snapshot` (chrono), `on_batch` (metrics), `on_span`
  (tracing), and duck-typed `handle`/`synchronize` (logging).
- `detail::SinkBinding` is copy-pasted in chrono, metrics, and tracing with different pointers.
- `com::concepts::Message` is `std::is_trivially_copyable_v`; `chrono::concepts::Logger` is a
  one-line level/message contract.
- Domain concepts name domain types: `chrono::Snapshot`, `metrics::MetricBatchSnapshot`,
  `tracing::SpanRecord`, `chrono::Timestamp`.

## Goals / Non-Goals

**Goals:**

- One structural sink contract, one binding type, one set of delivery method names.
- Generic concepts that name no domain type live in `core`; domain concepts stay in modules.
- No new dependency edges: `core` still depends on nothing above it.

**Non-Goals:**

- Moving `Snapshot`, `SpanRecord`, `MetricBatchSnapshot`, `Timestamp`, or `SpanContext` into core.
- Introducing concepts into modules that have none today; `exec::ControlHost` and similar virtual
  interfaces need their own change because removing runtime selection changes behavior.
- Changing payload layouts, wire formats, or metric semantics.

## Decisions

### D0. A concept moves to `core` only when it removes a dependency edge

The test is coupling, not category. A concept is promoted only if it satisfies both:

1. two or more modules must name it, **or** it is what lets one module accept an implementation
   from another without depending on that module; and
2. it references no domain type, so `core` does not have to learn `Snapshot`, `SpanRecord`,
   `MetricBatchSnapshot`, or `Timestamp`.

Applying it: `Message` (named by `com`, `exec`, and `phm`), `Sink`/`LifecycleSink` (four modules),
`CounterLike`/`MeterLike` (four modules), `LevelLogger` (the minimal shape a worker accepts,
without pulling in `logging`), and `Logger` (the full logging contract, named by `logging` and
`synchro`, whose requires-expressions use no logging type) qualify. `Provider`, `Stamper`, `Metric`, `ReadableMetric`, `Tracer`,
and `SpanGuard` do not: they name module types and are consumed inside their own module, so they
stay put. Relocating concepts that already live in `core`, or that only one module names, is left
out.

### D1. Parameterized concepts in core, domain types stay in modules

```cpp
// core/concepts/sink_concept.hpp
template <class S, class Event>
concept Sink = requires(S& s, const Event& e) {
    { s.on_event(e) } noexcept -> std::same_as<core::Status>;
    { s.flush() }    noexcept -> std::same_as<core::Status>;
};

template <class S, class Event>
concept LifecycleSink = Sink<S, Event> && requires(S& s) {
    { s.start() } noexcept -> std::same_as<core::Status>;
    { s.stop() }  noexcept;
};
```

`chrono::concepts::ChronoSink<T>` becomes `core::concepts::Sink<T, Snapshot>`, metrics and tracing
follow the same pattern. The module supplies the event type; core owns only the shape.

*Moving the existing concepts verbatim* rejected: it would drag `Snapshot`, `SpanRecord`, and
`MetricBatchSnapshot` into L0 and invert the layering.

### D2. One delivery vocabulary: `on_event`, `flush`, `start`, `stop`

The four current names collapse to one. `to_string()` is a diagnostics convention, not part of the
sink contract: tracing keeps it as an additional requirement on its concept.

The rename is breaking for every sink implementation and test. It is mechanical, and the compiler
finds all call sites; keeping four names would leave the "unification" cosmetic.

### D3. One `core::SinkBinding<Event>` with function-pointer erasure

```cpp
template <class Event>
struct SinkBinding {
    void* self{nullptr};
    core::Status (*on_event)(void*, const Event&) noexcept{nullptr};
    core::Status (*flush)(void*) noexcept{nullptr};
    core::Status (*start)(void*) noexcept{nullptr}; // nullable
    void (*stop)(void*) noexcept{nullptr};          // nullable
};
```

- chrono and metrics leave `start`/`stop` null; tracing and logging use them.
- A null `on_event` or `flush` is a successful no-op, preserving today's "unbound provider" rules.
- Providers keep type erasure, so `Provider<...>` types do not change with the sink type and the
  worker loops do not instantiate per sink.

*Templating providers on the sink type* rejected: sinks are cold-path and runtime-configured; the
gain is nil and the type explosion is real (every provider alias becomes sink-dependent).

### D4. `Message` and `LevelLogger` move; aliases keep the old names

- `com::concepts::Message<T>` becomes an alias of `core::concepts::Message<T>`;
  `com::concepts::EventPayload` follows.
- `chrono::concepts::Logger<T>` becomes an alias of `core::concepts::LevelLogger<T>`.
- `logging::concepts::Logger<T>` becomes an alias of
  `core::concepts::Logger<T, FormatWithLocation>`. The format wrapper is a template parameter, so
  the concept checks the real variadic signatures while core names no logging type; the logging
  module keeps the public name.

### D5. Domain concepts stay where their types live

`chrono::{Provider, Stamper}`, `metrics::{Metric, ReadableMetric}`, and `tracing::{Tracer,
SpanGuard}` keep their headers. They describe domain behavior and reference module types; moving
them would either move those types or produce concepts with the wrong parameters.

### D6. Logging gets a real sink concept

`AsyncWorker`'s duck-typed sink becomes `core::concepts::LifecycleSink<Sink, Record>` with
`on_event`/`flush`. This is the only place where a sink was never checked at compile time, so this
change also removes a class of silent contract drift.

## Risks / Trade-offs

- **Breaking renames ripple through tests and examples** → mechanical, compiler-guided; each module
  lands with its tests and docs in the same group.
- **Concept error messages get longer** → module-level aliases (`ChronoSink`, `MetricSink`,
  `TraceSink`) keep call sites readable.
- **Shared binding must preserve null semantics** → the no-op rules are pinned by tests in each
  provider before the old bindings are deleted.
- **`to_string` is not in the core contract** → tracing's concept adds it explicitly, so no sink
  loses its diagnostic surface.
- **Concept / type mismatch at the sink seam** → each module keeps a `static_assert` that its
  concrete sinks satisfy its alias.

## Migration Plan

1. Land `core/concepts`, `core/sink_binding.hpp`, `core/null_meter.hpp`; no behavior changes.
2. Migrate chrono: rename `on_snapshot` → `on_event`, replace the local binding, keep tests green.
3. Migrate metrics and tracing the same way; tracing keeps `start`/`stop`/`to_string`.
4. Migrate logging's consumer sink to the lifecycle concept and rename `handle`/`synchronize`.
5. Alias `com`/`chrono` concepts and delete the duplicated concept headers.
6. Update docs and architecture contracts. Rollback is a revert of the renames; no data or wire
   format is involved.

## Open Questions

- Whether `diagnostics` should adopt a `ClockLike` concept for its stopwatch; deferred until a
  consumer needs an alternative clock.
- Which virtual interfaces outside `exec::ControlHost` are worth converting; separate change.
