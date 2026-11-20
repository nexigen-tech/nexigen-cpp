# Tasks

## 1. Core vocabulary

- [x] 1.1 Add `core/concepts/message_concept.hpp` and `level_logger_concept.hpp` with
      `core::concepts::Message` and `core::concepts::LevelLogger`; verify positive tests and
      negative-compile probes for non-POD payloads and a missing `log(LogLevel, string_view)`.
- [x] 1.2 Add `core/concepts/sink_concept.hpp` with `Sink` and `LifecycleSink`; verify a conforming
      fake sink satisfies both and probes with a throwing or misnamed member do not.
- [x] 1.3 Add `core/sink_binding.hpp` with `SinkBinding<Event>` (nullable `start`/`stop`, no-op on
      null `on_event`/`flush`); verify construction allocates nothing and a null binding reports
      success.
- [x] 1.4 Add `core/null_meter.hpp` and `core/concepts/meter_concept.hpp` (moved from the recorder
      change); verify `NullMeter` satisfies `MeterLike`/`CounterLike` and exposes a stateless
      no-op counter.
- [x] 1.5 Verify `core` still links nothing above it: configure a core-only target and confirm no
      `chrono`, `metrics`, `tracing`, `logging`, or `com` symbol is referenced.
- [x] 1.6 Audit the result against D0: verify every `core` concept is named by at least two modules
      (or documents the decoupling it enables), and that no module keeps a duplicate of a core
      concept.
- [x] 1.7 Move the full logging `Logger` concept to `core::concepts::Logger` and alias it from
      `logging::concepts`; verify every logger and the synchro generator still satisfy the alias.
- [x] 1.8 Actualize `core::concepts::Logger<T, Format>` to check the real variadic signatures
      through the module's format wrapper; verify the logging conformance tests still pass.
- [x] 1.9 Add instrument concepts (`GaugeLike`, `StateGaugeLike`, `DescriptiveStatisticsLike`,
      `HistogramLike`, `InstrumentMeterLike`) and pin every metrics instrument with
      `static_assert`; verify the metrics tests build.

## 2. Chrono migration

- [x] 2.1 Rename `on_snapshot` to `on_event` in `NullSink`, `ConsoleSink`, `JsonFileSink`,
      `CompositeSink`, and their tests; verify `chrono/chrono-sink` tests pass.
- [x] 2.2 Replace `chrono::detail::SinkBinding` with `core::SinkBinding<Snapshot>` and keep
      `start`/`stop` null; verify an unbound provider still no-ops successfully.
- [x] 2.3 Alias `chrono::concepts::ChronoSink` to `core::concepts::Sink<T, Snapshot>` and
      `chrono::concepts::Logger` to `core::concepts::LevelLogger`; verify the worker binding tests
      pass.
- [x] 2.4 Update the chrono module docs and sink examples to the new names; verify the documented
      example compiles.

## 3. Metrics migration

- [x] 3.1 Rename `on_batch` to `on_event` in the null, console, JSON, composite, and iceoryx2 sinks
      and their tests; verify metrics sink tests pass.
- [x] 3.2 Replace `metrics::detail::SinkBinding` with `core::SinkBinding<MetricBatchSnapshot>`;
      verify provider delivery and flush behavior are unchanged.
- [x] 3.3 Alias `metrics::concepts::MetricSink` to
      `core::concepts::Sink<T, MetricBatchSnapshot>`; verify the concept tests and the
      `MeterLike`/`CounterLike` static asserts pass.
- [x] 3.4 Update the metrics sink docs and examples; verify the documented sink example compiles.

## 4. Tracing migration

- [x] 4.1 Rename `on_span` to `on_event` in the null, console, JSON, chrome, composite, and
      iceoryx2 sinks and their tests; verify tracing sink tests pass.
- [x] 4.2 Replace the local `SinkBinding` with `core::SinkBinding<SpanRecord>`, keeping
      `start`/`stop` wired; verify the shutdown drain still calls stop exactly once.
- [x] 4.3 Express `TraceSink` as `core::concepts::LifecycleSink<S, SpanRecord>` plus
      `to_string()`; verify the `static_assert` suite still covers every sink.
- [x] 4.4 Update the tracing docs and sink examples; verify the documented example compiles.

## 5. Logging migration

- [x] 5.1 Add a `RecordSink` concept alias in `logging/detail` over
      `core::concepts::LifecycleSink<S, Record>` and constrain `AsyncWorker` with it; verify a
      missing member fails the build.
- [x] 5.2 Rename `handle` to `on_event` and `synchronize` to `flush` in console, file, bounded, and
      iceoryx2 sinks and their tests; verify logger tests pass.
- [x] 5.3 Verify `start` runs before the first record and `stop` after the final drain, and that
      flush surfaces write errors exactly as before.
- [x] 5.4 Update the logging sink documentation; verify the documented sink example compiles.

## 6. Aliases, docs, and validation

- [x] 6.1 Alias `com::concepts::Message`/`EventPayload` to the core concepts and delete the
      duplicated definitions; verify com concept tests and publisher constraints pass.
- [x] 6.2 Delete the superseded concept and binding headers; verify no include references them.
- [x] 6.3 Update `docs/architecture.md` module contracts and the concept inventory; verify each
      documented method name matches the headers.
- [x] 6.4 Run the full validation ladder — Debug, Release, portable Release, TSan, ASan, and the
      coverage target — and verify 100% of tests pass with module gates unchanged.

## Workflow follow-up

- Archive `unify-core-concepts` before archiving `record-telemetry-sessions`, which consumes
  `core::MeterLike`.
- Rebuild the graph with `graphify update .` after the code lands.
