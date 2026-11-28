# ADR 0005 — Observability is embedded by the runtime, not the core

- **Status:** Accepted
- **Date:** 2026-11-28
- **Scope:** `nexigen::core`, `nexigen::logging`, `nexigen::metrics`,
  `nexigen::tracing`, `nexigen::diagnostics`, `nexigen::canopen`,
  `nexigen::canopen::observability`, `nexigen::synchro`, `nexigen::com`,
  `nexigen::app`
- **Change:** `openspec/changes/add-canopen-observability`

## Context

The platform has four telemetry subsystems with complementary guarantees:
`logging` packs arguments and formats on a consumer thread; `metrics` records
into preallocated, sharded instruments that a harvester may read concurrently;
`tracing` emits low-frequency spans; `diagnostics` carries structured health
events. Each subsystem works on its own, and `synchro`, `com`, and `app` already
show a working embedding pattern.

The CANopen node stack does not exist yet. Its core (`nexigen::canopen`) is a
pure data-plane library shared by the viewer and the future `nexigen-canopend`:
EDS/DCF parsing, PDO unpacking, SDO assembly, and COB-ID routing. If telemetry
were added to that core, every decoder would grow a logger/meter dependency,
tests would need fakes, and the viewer would link subsystems it does not use.
At the same time, a runtime that logs every repeated bus error will flood the
log ring and hide the first event that mattered.

The project therefore needs one written embedding contract before the runtime is
written, so the runtime cannot accidentally grow ad-hoc logging.

## Decision

1. **Runtime/service layers own telemetry.** The layer that owns a thread and a
   transport owns the logger and the meter. Data-plane modules return
   `Status`, `Result`, and structured result values. They do not accept, store,
   or call a logger, meter, tracer, or diagnostic sink.

2. **The logger is a template parameter; the meter is a borrowed reference.**
   A runtime is templated on a type satisfying `logging::concepts::Logger`, so
   `logging::NullLogger` works in tests and embedded builds. The meter comes
   from a provider owned by the process; the runtime never creates, copies, or
   outlives it.

3. **Metric identity carries the runtime scope.** A runtime creates its
   instruments on one meter per instance (`canopen.<bus>`), because the metrics
   API has no per-metric labels. Duplicate scopes are rejected at startup
   instead of sharing names and double-counting.

4. **One owner per event.** Every observable event has exactly one metric owner
   and at most one log owner. `com::EventProducer` owns `published` and
   `publish_failed`; a CANopen runtime that publishes through it logs the edge
   and creates no second publish-failure counter.

5. **Hot-path errors are bounded by an episode gate.** The metric is recorded
   first; `detail::metric_mapping::record_hot_error()` performs that increment
   before the gate call, so the ordering is structural rather than a review
   convention. The gate logs the first error of an episode, an error-code
   change, a recovery after the hold interval, and at most one bounded repeat
   per key.
   First-error, code-change, and recovery records bypass the global repeat
   budget; repeats consume it. The logger's `SpamProtection` stays enabled as
   the last line of defence. The gate is a fixed array (128 nodes × 9 domains ×
   40 bytes) with no atomics, locks, or allocation.

6. **Startup telemetry is fail-fast.** Instruments are registered before the
   transport is opened. A capacity or name failure returns
   `ResourceExhausted`/`InvalidArgument`, names the failing instrument, and
   leaves the transport closed. A partially instrumented bus never runs.

7. **Cold-path output is detailed and capped.** Lifecycle, configuration, and
   EDS/DCF loading log at `Info`; parser diagnostics are capped at
   `max_cold_diagnostics` with an omitted-count trailer. Successful per-object
   detail is `Debug`.

8. **Errors are values, never exceptions.** The project builds with
   `-fno-exceptions`/`-fno-rtti`; telemetry code follows the same rule and uses
   `core::Result`/`core::Status` with checked access.

9. **The patterns get names.** `docs/observability-embedding.md` defines
   `OBS-BOOT`, `OBS-COLD`, `OBS-EDGE`, `OBS-HOT-ERR`, `OBS-HOT-MET`, `OBS-SUM`,
   `OBS-SNAP`, and `OBS-SPAN`, so reviews can cite a pattern instead of
   restating it.

## Consequences

**Positive**

- `nexigen_canopen` stays COM-, GUI-, logger-, and meter-free; the viewer keeps
  decoding without telemetry dependencies.
- A repeated bus error cannot flood the log ring: the gate bounds records by
  key, interval, suppressed threshold, and a global budget, while the metric
  keeps counting every occurrence.
- Runtime startup either produces a complete instrument set or fails with the
  failing name; there is no silent partial telemetry.
- Tests can substitute `NullLogger` and verify allocation freedom and
  concurrency without infrastructure.
- New modules have a checklist and named scenarios instead of inventing a
  logging shape per team.

**Negative / accepted**

- The runtime carries ~45 KiB of episode storage per bus. Acceptable for the
  stated 1–3 buses; a sparse map can replace it later without changing the
  contract.
- The gate is single-threaded by contract. Recovery and summary polling must
  stay on the runtime thread; a future control-thread design must snapshot
  under explicit synchronization.
- Per-bus meters lose trivial cross-bus aggregation. A hub view can sum across
  meters later; adding labels is not an option in the current metrics API.
- Capacity must be planned: 25 counters per bus, recommended
  `max_counters >= 25 * buses + 16`. Deployments with three or more buses must
  raise the provider capacity.
- Tier 2 metrics (bytes, SDO duration, PDO jitter, critical EMCY, signal
  saturation) are deferred until the runtime exists and capacity is provisioned.

## Alternatives considered

- **Template core classes on logger/meter.** Rejected: it pulls `logging` and
  `metrics` into `nexigen_canopen`, turns pure decoders into stateful
  templates, and needs a null meter that supports gauges and histograms; the
  existing `CoreNullMeter` only satisfies counters.
- **Log every detected error and rely on `SpamProtection`.** Rejected: hashing
  cannot express first-error, code-change, or recovery edges, and a changing
  payload defeats identical-message suppression entirely.
- **One aggregate `canopen` meter.** Rejected for the stated deployment: it
  loses per-bus visibility. It remains the fallback if virtual-bus counts grow
  far beyond the documented range.
- **Put suppression inside the logger.** Rejected: the logger is shared by all
  modules and has no domain/node/code key; the gate belongs to the component
  that knows the error vocabulary.
- **Defer the contract until the runtime is written.** Rejected: the runtime
  would then define telemetry ad hoc, and the core boundary would erode before
  anyone writes it down.

## References

- [docs/observability-embedding.md](../observability-embedding.md) — scenario
  definitions, rules, and code references.
- [canopen/observability spec](../../openspec/changes/add-canopen-observability/specs/canopen/observability/spec.md)
  — normative CANopen requirements.
- [CANopen observability design](../../openspec/changes/add-canopen-observability/design.md)
  — metric catalogue, gate layout, capacity formula, and deferred work.
- [CANopen observability proposal](../../openspec/changes/add-canopen-observability/proposal.md)
  — motivation and impact.
- `include/nexigen/synchro/sync_generator.hpp` — logger/meter injection and
  one-time registration example.
- `include/nexigen/com/event_producer.hpp` — event metric ownership example.
