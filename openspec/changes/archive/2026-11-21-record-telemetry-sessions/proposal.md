# Proposal

## Why

A suite currently has no single artifact that correlates logs, metrics, and traces: logs go to
console/JSONL, metrics to Prometheus/JSON sinks, traces to JSON/Perfetto sinks, and a recorder
session holds only time-base history. The recorder format already reserves `LogFrame`,
`MetricFrame`, and `SpanFrame`, and `nexigen-export` already decodes them, but no producer writes
them, so cross-application correlation requires joining files from several processes by hand.

## What Changes

- Add a COM-gated collector that subscribes to the `LogFrame`, `MetricFrame`, and `SpanFrame`
  streams and writes all three into one recorder session. Whether it is a new tool or an extension
  of `nexigen-logd` is decided in design.
- Record `com` event channels in the same session, using a type-erased raw subscriber so the
  collector does not need to know each application's event payload types.
- Add a transport sequence header to the telemetry publish-subscribe services and report gaps to
  each subscriber, so transport loss is observable instead of silent.
- Declare meter concepts in `nexigen::core` and inject a meter into `com`, `logging`, `tracing`,
  and `chrono` endpoints as a defaulted template parameter, so each endpoint creates the counters
  it needs; the values travel through the existing metric sink and are recorded as ordinary metric
  frames.
- Make the built-in recorder descriptors for the three frames complete and pinned to the C++
  layouts. `nexigen.metrics.MetricFrame` is an empty descriptor today.
- Declare one channel per (application, stream) before the first record, carrying the application
  id/pid and the frame schema, so export and `info` can list datasets per application.
- Bind the session to the boot token and time-base epoch, keep the native monotonic event time from
  each frame, and write drop deltas as `GapMarker` records instead of silently losing them.
- Support session configuration and lifecycle: session root, rotation/retention, clean close, and
  an explicit statement of how recording coexists with the JSONL sinks (no double storage by
  default).
- Emit explicit gap objects in Perfetto output, matching the guarantees JSONL already has.
- No change to producer hot paths. `nexigen_recorder` stays COM-free and keeps linking only
  `core` + `chrono`.

**BREAKING**: the telemetry pub/sub services gain a user header, so every publisher and subscriber
of the log, metric, and trace services must be rebuilt and deployed together. `EventHeader` and the
event service types are unchanged.

## Capabilities

### New Capabilities

- `recorder/telemetry-capture`: a single collector records logs, metrics, traces, and `com` events
  from every application that publishes to the configured services into one session, with
  per-application channels, loss marking, loss counters, and session lifecycle.

> The meter vocabulary (`core::CounterLike`, `core::MeterLike`, `core::NullMeter`) is defined by
> `unify-core-concepts`; this change consumes it and does not declare it.

### Modified Capabilities

- `recorder/raw-storage`: the built-in frame descriptors must fully describe `LogFrame`,
  `MetricFrame`, and `SpanFrame` and be pinned to their C++ sizes and offsets; the metrics
  descriptor is currently empty.
- `com/publish-subscribe`: telemetry services gain a transport sequence header, every subscriber
  reports how many samples were lost before the one it received, a type-erased raw subscriber is
  added for introspection tools, and endpoints gain a meter-constrained template parameter with
  their own counters.
- `recorder/export`: Perfetto output gains explicit gap objects at their sequence positions.

## Impact

- **Code**: `include/nexigen/com/` (transport header, publisher
  sequence, subscriber gap reporting, raw introspection subscriber, meter-constrained endpoints);
  `include/nexigen/{logging,metrics,tracing,chrono}/` (meter parameter and counters);
  `tools/nexigen-export/` (Perfetto gap objects); `src/recorder/builtin_schemas.cpp`; the
  collector under `tools/` and `tools/CMakeLists.txt`; integration tests beside
  `tests/{core,com,logging,tracing,chrono,recorder}/`; `docs/recorder.md`,
  `docs/architecture.md` (§4.13, §5.6).
- **Dependencies**: iceoryx2 behind `NEXIGEN_ENABLE_COM`; no new third-party libraries. The
  recorder library remains free of COM, MCAP, FFmpeg, and iceoryx2.
- **Performance**: zero added latency and zero allocations on producer hot paths. The collector is
  cold path: preallocated append buffer, no JSON serialization and no `std::string` in the write
  loop; memory is the session buffer plus the schema/channel tables, and disk throughput is the
  practical bound.
- **Systems**: recording is opt-in at runtime; applications are not configured individually, and
  the session has exactly one writer.
