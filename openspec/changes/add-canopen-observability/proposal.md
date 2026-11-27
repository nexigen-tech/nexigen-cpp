# Proposal

## Why

The CANopen node stack and `nexigen-canopend` do not exist yet, but the transport-independent
`nexigen::canopen` core is already shared by the viewer and the future node stack. Without an
explicit observability contract, the runtime will either stay opaque or grow ad-hoc logging on its
hot path, where a single repeated error can flood the log ring and hide the event that mattered.

This change fixes the contract before the runtime is written. It keeps `nexigen::canopen` clean
and puts observability in the runtime layer, so decoding remains testable without logging,
metrics, COM, or GUI dependencies.

## What Changes

- Add a runtime-facing CANopen observability layer: one `CanOpenBusRuntime` per bus, one meter
  `canopen.<bus>` per runtime, logger injected as a template parameter and meter injected by
  reference.
- Define the Tier 1 hot-path metric catalogue: cycles, frame traffic, drops and their causes,
  PDO/SDO errors, NMT/heartbeat, EMCY, signal quality, TX failures, bus-off, queue depth,
  active SDO transfers, online nodes, runtime state and loop latency.
- Define observability policy A for hot-path errors: the metric is always recorded; the first
  error of an episode, a code change, recovery and a bounded periodic repeat are logged. Repeats
  are suppressed by a per-key gate, with the logger's `SpamProtection` kept as a safety net.
- Add a cold-path policy for EDS/DCF loading, configuration and NMT transitions, including a cap
  on emitted parser diagnostics.
- Define startup capacity validation: metric registration happens before the transport opens, and
  a capacity/name failure fails the runtime with a diagnostic naming the failing instrument.
- Document the project-wide observability embedding scenarios (`OBS-BOOT`, `OBS-COLD`,
  `OBS-EDGE`, `OBS-HOT-ERR`, `OBS-HOT-MET`, `OBS-SUM`, `OBS-SNAP`, `OBS-SPAN`) and the
  injection/no-duplication rules in `docs/observability-embedding.md` plus a new ADR.
- Record Tier 2 metrics (bytes, SDO duration/segments, PDO jitter, critical EMCY, signal
  saturation, bus error frames) as deferred work in `design.md`; they are not normative in this
  change.
- Record the deferred trace-span and PHM/SM correlation points (`canopen.eds.load`,
  `canopen.sdo.transaction`, `canopen.pdo.cycle`, bus-off/heartbeat/critical-EMCY/deadline
  escalation) in `design.md`; they are implemented by the runtime change, not here.

No requirement of `canopen/core` changes. The core keeps returning `Status`/`Result`/structured
events and never receives a logger or a meter.

## Capabilities

### New Capabilities

- `canopen/observability`: runtime-owned logging and metrics for the CANopen node stack: injection
  model, Tier 1 metric catalogue, hot/cold logging policy, suppression ladder, no-duplication
  rules and startup capacity checks.

### Modified Capabilities

None. `canopen/core` remains telemetry-free; the new capability lives above it.

## Impact

- **New code**: a runtime-facing static library (`nexigen_canopen_observability`) with public
  `CanOpenLogPolicy`, `CanOpenRuntimeState` and `CanOpenErrorDomain` vocabulary, plus
  `detail::RuntimeMetrics` and `detail::ErrorGate`. `nexigen_canopen` itself is not modified.
- **Dependencies**: the new target links `nexigen_canopen`, `nexigen_logging` and
  `nexigen_metrics`; it must not enter `nexigen_canopen`, which stays COM-, GUI- and
  telemetry-free.
- **Performance**: hot-path recording is a small set of wait-free `MultiThreaded` instrument
  updates (single-digit nanoseconds per counter in the existing metrics latency gates). The error
  gate adds one fixed-size compare per detected error and never formats, allocates or takes a
  lock. No new work is added to successful per-frame paths beyond the counters.
- **Memory**: the error gate is a fixed array of 128 nodes x 9 domains, allocated once at startup
  (about 48 KiB per bus at the planned layout). Instruments come from the provider's preallocated
  registry; Tier 1 needs 25 counters, 4 gauges, 1 state gauge and 1 explicit histogram per bus.
  With the default `max_counters = 64`, two buses plus other modules need the counter capacity
  raised; the runtime validates this before opening the transport.
- **Logging**: log volume is bounded by the per-key interval, the suppressed-count threshold, the
  global hot-log budget and the periodic domain summary. `SpamProtection` remains enabled.
- **Docs**: `docs/observability-embedding.md`, `docs/adr/0005-observability-embedding.md`, and
  vocabulary entries for the named scenarios.
