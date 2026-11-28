# Observability embedding

How a nexigen module embeds logging, metrics, tracing, and diagnostics without
putting telemetry on a hot path or giving one event two owners. The behaviour
contract lives in `openspec/specs/*`; this guide fixes the shared vocabulary and
the reusable patterns. The CANopen node stack is the newest reference
implementation, next to `nexigen::synchro`, `nexigen::com`, and `nexigen::app`.

## 1. Injection model

- **The runtime owns telemetry.** Service/runtime layers accept and call a
  logger; pure data-plane modules return structured outcomes (`Status`,
  `Result`, `PdoDecodeResult`, `SdoEvent`, `EdsParseResult`) and never receive a
  logger, meter, tracer, or diagnostic sink.
- **The logger is a template parameter.** A runtime accepts
  `logging::concepts::Logger auto&` (or a concrete `Logger&` template argument),
  so `logging::NullLogger` is a drop-in for tests and embedded builds.
- **The meter is a borrowed reference.** A runtime takes `metrics::Meter&` from
  a provider it does not own; the provider owns every instrument address.
  Registration is cold path and runs before the transport is opened.
- **One meter per runtime instance.** Metric identity carries the scope that
  labels cannot express: `canopen.<bus>` (for example `canopen.can0`). Duplicate
  instances must be rejected before registration, not merged.
- **One owner per event.** An event has exactly one metric owner and at most one
  log owner. A COM publish failure uses `EventProducer`'s `publish_failed`
  counter; the CANopen runtime logs only the edge and creates no second
  publish-failure counter.
- **Errors are values.** Use `core::Result<T, E>`/`core::Status`; never throw.
  A telemetry failure never fails the data path unless startup declared it
  fatal.

## 2. Hot and cold rules

| Path | Allowed | Forbidden |
| --- | --- | --- |
| Hot (per frame / per cycle) | wait-free counter/gauge/state writes, one fixed-size gate compare, `logger.log(...)` that packs arguments | allocation, `std::format` on the producer, locks, virtual dispatch, blocking I/O |
| Cold (startup, config, lifecycle) | name validation, instrument registration, `to_string`, `nlohmann::json`, file I/O, formatted diagnostics | unbounded output; cap repeated diagnostics |

A suppressed log record never suppresses its metric. The metric is recorded
first, then the logger threshold and the suppression gate are consulted.

## 3. Scenarios

Each scenario names one reusable embedding pattern. The name is stable
vocabulary: use it in reviews and ADRs instead of re-describing the pattern.

### `OBS-BOOT` — pre-logger bootstrap diagnostics

**Definition:** what a process reports before a logger exists (argument parse,
settings load, time-base attach, node creation).

**Rules:**
- Write a bounded line to `stderr` and return a structured error; do not build a
  temporary logger.
- Once the logger exists, stop using the bootstrap channel for that process.
- Never buffer unbounded text waiting for the logger.

**Code reference:** `include/nexigen/app/app.hpp` — `std::fprintf(stderr, ...)`
before `Infrastructure`/logger publication, each followed by a `Result` return.

### `OBS-COLD` — detailed lifecycle and configuration journal

**Definition:** the cold-path record set for startup, configuration, file
loading, and shutdown.

**Rules:**
- Log at `Info` for a completed lifecycle step and `Error`/`Warning` for a
  failure; per-object detail stays at `Debug`.
- Cap repeated parser diagnostics at `max_cold_diagnostics` and emit one
  omitted-count trailer.
- Include the correlation keys (`bus`, `node`, `path`, `hash`) in the record.

**Code reference:** `include/nexigen/canopen/observability/cold_path_logging.hpp`
— `log_eds_result()`.

### `OBS-EDGE` — log on state transition, first error, and recovery

**Definition:** one record per meaningful edge rather than one per observation.

**Rules:**
- State transitions log the previous and next state.
- The first error of an episode and the recovery edge are always logged; the
  repeats between them are not.
- An edge record bypasses the global repeat budget.

**Code reference:** `include/nexigen/canopen/observability/cold_path_logging.hpp`
— `log_runtime_transition()`, `log_nmt_transition()`; recovery edge in
`detail/error_gate.hpp`.

### `OBS-HOT-ERR` — bounded hot-path error event

**Definition:** recording a detected error on the producer path.

**Rules:**
- Record the Tier 1 metric unconditionally.
- Consult `logger.level()` before marking an episode as logged.
- Delegate repeat suppression to one fixed-size gate per runtime; keep the
  logger's `SpamProtection` enabled as the last line of defence.
- Never format, allocate, or lock in the gate.

**Code reference:**
`include/nexigen/canopen/observability/detail/error_gate.hpp` —
`ErrorGate::on_error()`; `include/nexigen/canopen/observability/detail/metric_mapping.hpp` —
`record_hot_error()` (metric increment before the gate); design D5/D6 in
`openspec/changes/add-canopen-observability/design.md`.

### `OBS-HOT-MET` — metrics-first hot path

**Definition:** the per-frame counter/gauge/histogram update that is the only
telemetry on the producer path when nothing goes wrong.

**Rules:**
- Instruments are created once at startup and cached as raw pointers.
- Use `MultiThreaded` + `Readable` + `Cumulative` when a harvester may read
  concurrently with the producer.
- No metric name construction, lookup, or string operation on the hot path.

**Code reference:**
`include/nexigen/canopen/observability/detail/runtime_metrics.hpp` —
`RuntimeMetrics::register_metrics()`; `include/nexigen/canopen/observability/detail/metric_mapping.hpp`
— the `record_*`/`set_*` helpers; `include/nexigen/synchro/sync_generator.hpp`
— `SyncGenerator::register_metrics()`.

### `OBS-SUM` — windowed cold-path summary

**Definition:** one periodic record per active scope that aggregates what the
hot path suppressed.

**Rules:**
- Emit only after activity; an idle interval emits nothing.
- One record per active domain/scope, then reset the window.
- First-error and recovery records do not depend on the summary; the summary is
  additional visibility, not the only one.

**Code reference:**
`include/nexigen/canopen/observability/detail/error_gate.hpp` —
`ErrorGate::emit_summary()`.

### `OBS-SNAP` — pull-based diagnostic snapshot

**Definition:** an on-demand, cold-path value that captures current state for a
diagnostic endpoint or test.

**Rules:**
- Snapshots are plain trivially copyable values or an explicit harvest call;
  they never run on the producer path.
- Document which thread may snapshot and whether the call consumes a
  `ResetOnRead` window.
- A snapshot must not hold instrument addresses that outlive the provider.

**Code reference:** `include/nexigen/diagnostics/lap_snapshot.hpp` —
`Stopwatch::lap_snapshot()`; `metrics::MetricProvider::collect_now()` in
`include/nexigen/metrics/metric_provider.hpp`.

### `OBS-SPAN` — low-frequency transaction span

**Definition:** a trace span around a cold or low-frequency transaction, not
around every frame.

**Rules:**
- Span start/end are paired by one `ScopedSpan` guard on one thread.
- Never create a span per CAN frame; sample or wrap only the transaction.
- Only failed transactions are also logged by `OBS-HOT-ERR`.

**Code reference:** `include/nexigen/tracing/scoped_span.hpp` — `ScopedSpan`.

## 4. Deferred CANopen span and PHM correlation points

This change reserves the names; the CANopen runtime change implements them.

- `canopen.eds.load` — cold span around EDS/DCF load: path, content hash,
  object count, duration.
- `canopen.sdo.transaction` — request-to-completion span: node, index/subindex,
  direction, segmented flag, size.
- `canopen.pdo.cycle` — sampled or low-frequency span around a PDO/SYNC cycle,
  never one per frame.
- PHM/SM escalation: bus-off, heartbeat timeout, critical EMCY, and cycle
  deadline miss map to `DiagnosticEvent`/PHM recovery in the runtime layer, so
  `nexigen::canopen` stays free of PHM and SM dependencies.

## 5. Checklist for a new module

1. Decide the runtime boundary: which layer owns the logger and the meter.
2. Keep the data-plane target free of `nexigen_logging`, `nexigen_metrics`,
   `nexigen_tracing`, `nexigen_com`, and `nexigen_phm`; add a purity test when
   the boundary is easy to erode.
3. Name the meter after the runtime scope; reject duplicate scopes at startup.
4. Register every instrument before opening the transport; fail with a
   diagnostic naming the instrument.
5. Record metrics first, then consult the logger threshold and the gate. Use the
   component's `record_hot_error()` helper so the ordering is structural rather
   than a review convention.
6. Document the one owner of every counter and every edge record.
7. Add the allocation trap and the concurrency sanitizer build to the test
   matrix.

## 6. References

- `openspec/changes/add-canopen-observability/` — proposal, design, spec, tasks.
- `openspec/changes/add-canopen-observability/specs/canopen/observability/spec.md`
  — normative requirements and scenarios.
- `docs/adr/0005-observability-embedding.md` — decision record.
- `include/nexigen/synchro/sync_generator.hpp` — logger template parameter,
  borrowed meter, one-time registration.
- `include/nexigen/com/event_producer.hpp` — event metric ownership
  (`published`, `publish_failed`).
- `include/nexigen/canopen/observability/` — CANopen implementation of
  `OBS-COLD`, `OBS-EDGE`, `OBS-HOT-ERR`, `OBS-HOT-MET`, and `OBS-SUM`.
