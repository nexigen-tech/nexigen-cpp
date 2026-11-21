# Proposal

## Why

`Infrastructure::shutdown()` currently destroys the metric provider, tracer provider, and logger, but application code keeps running afterwards in `main`, holding raw handles (`metrics::Meter*`, instrument pointers, `tracing::Tracer*`, `Logger&`) that are now dangling. Writing through any of them is silent use-after-free: instruments write into freed arena memory, and `logger()` dereferences a disengaged `optional`. The application lifecycle is main-scoped, so the honest invariant is "handles are valid for as long as the `Infrastructure` object is alive" — not "until an explicit `shutdown()` call".

## What Changes

- `Infrastructure::shutdown()` becomes a quiesce operation: stop the signal watcher, flush the logger and log the final record, then quiesce tracing, quiesce metrics, flush the logger once more, and quiesce the logger before releasing signal ownership — all **without** destroying component storage or sinks.
- Startup and teardown are mirror images for the logger: it is constructed and its worker started before the metric and tracer providers, and on the way out it is flushed and quiesced after both, so every initialization and shutdown diagnostic from metrics and tracing is loggable. The logger is also the last telemetry component destroyed by `~Infrastructure`.
- Quiescence order is `tracer.quiesce()` → `metrics.quiesce()` → `logger.flush()` → `logger.quiesce()`, so every component can still emit diagnostics through the logger while it shuts down and the tracer's final counter updates are captured by the last metric harvest. The accepted consequence is that counter updates produced by the logger's final drain are not harvested.
- Component storage (metric registry, tracer pool, logger rings and sinks) is released only when the `Infrastructure` object is destroyed, which in the supported lifecycle is the end of `main`.
- Each telemetry subsystem gains an explicit, idempotent quiescence entry point that stops its background worker after a final drain and marks the provider sealed so destruction performs no second drain or sink I/O:
  - `MetricProvider::quiesce()` — stop harvester, final collection, flush, seal.
  - `TracerProvider::quiesce()` — flush barrier, stop consumer (drain + sink stop), seal, publish the liveness flag.
  - Logger quiescence — a narrow `QuiescableLogger` concept (`flush()` then stop the consumer), implemented by `NullLogger`, `ConsoleLogger`, `FileLogger`, `Iceoryx2Logger`, and forwarded by `CompositeLogger`.
- Control-plane symmetry: after quiescence every entry point that creates handles or triggers collection/flush on the infrastructure or on a provider (`meter()`/`get_meter`, `tracer_for()`/`get_tracer`, `collect_now`, `flush`) returns `NotInitialized`. Only recording/reading on already-issued handles and read-only diagnostics (`startup_status()`, worker reports, `dropped()` statistics) remain usable.
- Post-quiesce behavior is per-subsystem and documented rather than hot-path gated: metric instruments accept and discard records, tracer spans report `NotInitialized` through the existing liveness check, and log records are dropped by the existing ring overflow policy.
- **BREAKING**: after `shutdown()`, every control-plane entry point — `MetricProvider::collect_now()`/`flush()`, `TracerProvider::get_tracer()`/`flush()`, and logger `flush()` — returns `NotInitialized` instead of operating on a destroyed component. Component destruction moves from `shutdown()` to `~Infrastructure`.
- The `chrono()` time-base handle is explicitly out of scope and keeps its current behavior (unusable after shutdown); it is recorded as a follow-up.
- No change to recording/formatting hot paths, no new hot-path branches, and no new heap allocations; the only cost is memory residency — the telemetry registries stay mapped until `~Infrastructure` instead of `shutdown()` (defaults: ~3.3 MiB metrics, ~10 MiB tracing, logger shards per configuration).

## Capabilities

### New Capabilities

- `logging/logger-quiescence`: the narrow `QuiescableLogger` contract — a logger can be stopped after a final flush, keeps its object alive and memory-safe, and drops records afterwards without blocking producers.

### Modified Capabilities

- `app/application`: shutdown stops and flushes components instead of destroying them, in the order tracing → metrics → logger; the logger is built first and quiesced last among the three; handle validity is tied to the infrastructure lifetime; every control-plane entry point rejects with `NotInitialized` after shutdown begins; the chrono exception is stated explicitly.
- `metrics/metric-provider`: new explicit quiescence contract (stop, final collection, flush, seal); provider destruction no longer drains a quiesced provider; instrument and meter handles stay valid until the provider is destroyed.
- `tracing/tracer-provider`: new explicit quiescence contract; the shutdown-drain requirement distinguishes a quiesced provider from a destroyed one; dead-handle behavior is restated against the mapped-storage contract.

## Impact

- **Code**: `include/nexigen/app/infrastructure.hpp`; `include/nexigen/metrics/metric_provider.hpp` + `src/metrics/provider.cpp`; `include/nexigen/tracing/tracer_provider.hpp`; `include/nexigen/logging/detail/async_worker.hpp`, `console_logger.hpp`, `file_logger.hpp`, `iceoryx2_logger.hpp`, `composite_logger.hpp`, `null_logger.hpp`, plus the new quiescence concept header; `nexigen::app` policy static assertion.
- **Tests**: `tests/app/infrastructure_test.cpp`, `tests/metrics/metric_provider_test.cpp`, `tests/tracing/tracer_provider_test.cpp`, `tests/logging/*` — post-shutdown handle use under ASan, suppressed sink delivery, idempotent quiesce, `NotInitialized` factory gates.
- **Docs**: `docs/architecture.md` handle-lifetime invariant, `docs/metrics-guide.md` real-time checklist, `README.md` lifecycle example, new ADR under `docs/adr/`.
- **Performance**: hot paths unchanged — counter `inc()` 2 ns gate, span start and log-record paths keep their existing instruction sequences; quiescence is a cold-path control operation. Memory residency extends to `~Infrastructure` with no change to peak allocation and zero allocations on create or quiesce.
- **Compatibility**: source-compatible additions only; the observable post-shutdown behavior is documented as breaking for code that called provider control-plane methods after `shutdown()`.
