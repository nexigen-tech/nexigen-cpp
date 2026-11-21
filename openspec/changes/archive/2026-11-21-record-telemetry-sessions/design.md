# Design

## Context

See `proposal.md` — Why. Current state that shapes the approach:

- `nexigen_recorder` is COM-free and links only `core` + `chrono`; `SessionWriter` is a
  single-writer, preallocated, no-allocation append path.
- The three wire frames are fixed PODs that already carry publisher identity:
  `LogFrame` (256 B, `app_id`, `pid`, `dropped_counter`), `MetricFrame` (`app_id`, `pid`),
  `SpanFrame` (384 B, `app_id`, `pid`, `sequence`, `version`).
- `nexigen-export` already decodes all three record types through `copy_typed<T>`, which requires
  the payload to be exactly `sizeof(T)`.
- `nexigen-logd`, `nexigen-metricsd`, and `nexigen-traced` each subscribe to their own service;
  none writes a session.
- `com::Publisher` already exposes `lost_samples()`, but it is publisher-local: it notes that some
  receiver's queue was full, without identifying the receiver or locating the missing sample.
- `com` events are typed (`EventProducer<T>` / `EventConsumer<T>`), and the C++ binding has no
  type-erased consumer; the underlying C API does expose a service's payload type details.
- `metrics::MetricProvider::get_meter()` is the only route to instruments; a `Meter` creates
  counters with fixed policies and owns their registry slot. `com` has no metrics dependency
  today, and `nexigen_metrics_com` already links both modules.
- Built-in schema id 2 (`nexigen.metrics.MetricFrame`) has an empty field descriptor.

## Goals / Non-Goals

**Goals:**

- One session per recording run, written by one process, containing any subset of the four
  streams (logs, metrics, traces, events) from all applications that publish to the suite services.
- Records are byte-verbatim frames, so export needs no new decoder; events are stored as an
  opaque, self-identifying envelope.
- Loss that the frame itself reports, and loss the transport dropped, both become `GapMarker`
  records at the right sequence position.
- Producers and consumers expose cumulative loss counters as metrics.
- The collector starts and stops cleanly: footer, manifest, rotation, retention.

**Non-Goals:**

- Recording from a build without `NEXIGEN_ENABLE_COM`; the collector depends on iceoryx2.
- Live follow/convert while recording; `nexigen-export` stays on-demand.
- Changing the three wire frames, including adding loss counters to `MetricFrame`.
- Session browsing in `nexigen-hub`.
- Collector-side filtering by level or metric name; the session is raw and export filters.

## Decisions

### D1. A separate `nexigen-recd` tool, not in-process sinks and not `nexigen-logd`

A new COM-gated executable under `tools/nexigen-recd/` owns one `SessionWriter` and subscribes to
the three services.

- *In-process sinks per module* rejected: `SessionWriter` is single-writer, so every application
  would create its own session; the recorder would also enter the dependency graph of logging,
  metrics, and tracing, which today do not link it.
- *Extending `nexigen-logd`* rejected: its contract is rendering text to JSONL with its own
  rotation and retention; folding raw sessions into it would mix two retention policies and force
  its spec to own unrelated datasets.
- Alternative kept open: a generic "telemetry sink" tool name. The decision is the topology, not
  the binary name.

### D2. One subscription per stream, application identity from the frame

`Subscriber<LogFrame>`, `Subscriber<MetricFrame>`, and `Subscriber<SpanFrame>` attach to the
suite-wide service names (configurable, defaulting to the values the publishers already use).
Every frame carries `app_id` and `pid`, so no service-per-application and no wire change are
needed.

Channel identity is `(app_id, stream)`: a channel topic such as `nexigen.log.LIDR`, one schema per
stream, `DatasetKind::Telemetry`, `message_encoding = "nexigen.pod"`. `pid` stays in the payload
because it can change across an application restart inside one session; putting it in the channel
metadata would make the channel lie after the first restart.

Capture is bounded by publication: an application that does not publish (no COM sink configured)
is invisible to the collector, and no configuration of the collector can capture it.

### D3. Frame-to-record mapping

| Frame | `RecordType` | `event_ns` | `commit_ns` | `sequence` |
|---|---|---|---|---|
| `LogFrame` | `LogFrame` | `timestamp_ns` | collector clock at append | per-channel counter |
| `MetricFrame` | `MetricFrame` | `timestamp_ns` | collector clock at append | per-channel counter |
| `SpanFrame` | `SpanFrame` | `start_ns` | collector clock at append | per-channel counter |

- Payloads are `std::as_bytes` of the received POD, never a re-serialization, so
  `copy_typed<T>` in the exporter keeps working and the size check is a `static_assert` in the
  collector.
- `SpanFrame::sequence` is per sink instance and multiple sinks can share one service, so it is
  preserved in the payload but not used as the record sequence.
- `commit_ns` follows the existing recorder contract (`CLOCK_MONOTONIC_RAW` write time); the
  native event time stays authoritative for the dataset index.

### D4. Loss becomes `GapMarker`, not silence

- `LogFrame::dropped_counter` is a delta since that publisher's previous frame. The collector
  accumulates it per `(app_id, pid)` and, before the next record for that channel, appends one
  `GapMarker` with `GapReason::StreamDiscontinuity` and the accumulated count.
- Transport loss is reported by the header's sequence (D9): the number of missing samples becomes
  a `GapMarker` with `GapReason::IoxRingOverflow` before the received record.
- If `SessionWriter::append` fails (buffer full, disk error), the collector counts the failure,
  keeps draining, and emits a `GapMarker` when the writer recovers. Publishers never block: they
  publish into iceoryx2 and are unaffected by a slow recorder.
- The collector never infers loss from idle time or from a missing *class* of frames; only
  reported drops and sequence gaps produce a marker.

### D5. Time binding

The session declares `TimeDomain::MonotonicRaw`. `StorageSettings` is filled with the boot token
and UUID from `read_boot_identity()` and with the time-base epoch when `TimeBaseClock` is attached,
otherwise `0`. The collector appends one `TimeSync` record at start and one after each rotation
using `sample_utc_offset()`, so the exporter can map monotonic instants to UTC. A full `TimeBase`
series remains `nexigen-chronod`'s job; the collector does not duplicate it.

### D6. Single-threaded event loop, preallocated write path

The main thread runs a `WaitSet` over the three subscribers plus an interval trigger for
periodic flush. Draining and `append` run on that thread only: no atomics, no locks, and no
allocation are added to the recorder. `StorageSettings::write_buffer_bytes` and the segment limits
are fixed at start; the collector's own batch scratch is allocated once at startup. SIGTERM/SIGINT
finish the drain, append the footer, and close the manifest.

### D7. No collector-side filtering

The session stores what publishers send. Filtering by time, channel, or level belongs to
`nexigen-export`, which already supports `--start`, `--end`, `--channels`, and level-aware
renderers. A second filter policy inside the collector would let two configurations disagree about
what "the session" contains. The cost is volume, handled by per-stream enable flags
(`--logs`, `--metrics`, `--traces`) and retention limits.

### D8. Coexistence with the existing sinks is explicit

`nexigen-logd`/`metricsd`/`traced` are unchanged. Running them together with the collector stores
the same telemetry twice under different retention rules, so the capability spec requires the
collector to be opt-in and the docs to recommend one destination per stream. No default is changed.

### D9. Transport sequence in the user header, not in the frames

`com::Publisher<T, H>` and `com::Subscriber<T, H>` already support a user header, and
`Sample::user_header()` exposes it. A new `com::TransportHeader` carries `app_id`, `pid`, a
`publisher_id` that changes on every publisher construction, and a per-publisher `sequence` that
increments on every send. Each subscriber keeps the last sequence per `publisher_id` in a small
fixed table and exposes `lost_before()` on the received sample; a late-joining or restarted
publisher simply starts a new baseline.

Only the telemetry services adopt the header (`Publisher<LogFrame, TransportHeader>`,
`Publisher<MetricFrame, TransportHeader>`, `Publisher<SpanFrame, TransportHeader>`, and the
matching subscribers); other `com` services keep `H = void`.

- *Sequence inside the frame* rejected: `LogFrame` is exactly 256 B with no version or reserved
  bytes, and `MetricFrame`/`SpanFrame` would need layout changes plus decoder updates.
- *Publisher-only counters* rejected: `lost_samples()` cannot say which subscriber lost the sample
  or where the gap belongs in the stream.
- *iceoryx2 monitoring* rejected: the pinned revision reports port liveness, not missed samples,
  and `DiscardData` swallows a full subscriber queue without an error.

Cost: the service type includes the header, so all publishers and subscribers of the three
telemetry services must be rebuilt and deployed together; a mixed deployment cannot connect.

### D10. Events are recorded through a type-erased raw subscriber

Events are typed (`EventProducer<T>` publishes `Publisher<T, EventHeader>`), so the collector
cannot instantiate `EventConsumer<T>` without knowing every application's payload type. The
iceoryx2 C API exposes a service's payload and user-header type details
(`iox2_service_details`) and accepts those details when opening a subscriber, so `com` gains a
`RawSubscriber`:

- created from a service name and a read config, it reads the existing service's type details and
  opens a subscriber with them;
- `receive_raw()` returns borrowed spans for the user header and the payload bytes, plus the
  payload type name and size; nothing is decoded;
- the same per-publisher sequence tracker as D9 produces `lost_before()` from `EventHeader`.

The collector records each event as one `ComPayload` record whose payload is the 64-byte
`EventHeader` followed by the payload bytes, in a channel per `(app_id, service, payload type)`
with `message_encoding = "nexigen.event"` and the payload type name in the channel metadata.
Payloads stay opaque: without the payload schema, export renders the header and the raw bytes.

*In-process tap* rejected: it would copy every event inside the producing application, on the same
path that publishes the event. *Building the collector with the suite's event types* rejected: it
breaks "any app, no recompile" and the collector would need generated code per deployment.

### D11. One meter concept in `core`, injected statically into every telemetry module

`core` declares the only metric vocabulary the lower layers may name, following the existing
`concepts/` layout (landed by `unify-core-concepts`; this change only consumes it):

- `core/concepts/counter_concept.hpp` — `core::CounterLike`;
- `core/concepts/meter_concept.hpp` — `core::MeterLike`;
- `core/null_meter.hpp` — `core::NullMeter`, the no-op default whose counters compile away.

The convention is identical in every module and mirrors the existing `concepts::Logger` /
`concepts::Sink` style: the meter is the **last template parameter, defaulted to
`core::NullMeter`**, and is injected by reference at construction.

| Module | Type | Counters |
|---|---|---|
| `com` | `Publisher<T, H, M>`, `Subscriber<T, H, M>` | `published`, `publish_failed`, `evicted`; `received`, `lost` |
| `com` | `EventProducer<T, M>`, `EventConsumer<T, OnEvent, OnLoss, M>` | `published`, `publish_failed`; `received`, `lost`, `duplicates` |
| `logging` | `ConsoleLogger<…, M>`, `FileLogger<…, M>`, `Iceoryx2Logger<…, M>` | `accepted`; `dropped.{rate_limited,buffer_full,slot_exhausted}` |
| `tracing` | `TracerProvider<Shard, MaxThreads, M>` | `started`; `dropped.{rate_limited,buffer_full,slot_exhausted}` |
| `chrono` | `Provider<MaxSources, M>` | `calibration_failed`, `acquire_exhausted`, `snapshot_failed` |

`metrics::Meter` models `core::MeterLike`; `metrics` is the only module that owns instruments and
does not itself take a meter. Every other module depends only on `core`, so the layering rule
holds. The meter is created once at the composition root (`MetricProvider::get_meter`) and passed
to whichever endpoints need it; `NullMeter` is the default, so existing call sites do not change.

Counters are meter-scoped, and `metrics::Meter` rejects a duplicate counter name inside one meter.
The composition root therefore supplies **one meter per endpoint instance**, named after the
service (for example `com.publisher.<service>`), instead of one shared `com` meter: a second
publisher on a shared meter would fail with `InvalidArgument`. This also keeps per-service
attribution in the metric name. Meter capacity (`MetricProviderConfig::max_meters`) must cover the
number of instrumented endpoints.

The concept constrains only `add`/`inc`, not the concurrency policy, so the meter decides it: a
meter handed to an endpoint that may record from several threads must return thread-safe counters
(in practice `Counter<MultiThreaded>`). Single-threaded meters remain valid for endpoints with a
single caller.

Because the meter type is a template parameter, `add()` inlines into the endpoint and a
`NullMeter` endpoint contains no counter code at all. The trade-off is one instantiation per meter
type per endpoint, which is one in practice: an application either instruments or it does not.

Each module also provides a free factory that deduces the meter type, so call sites do not spell
it: `make_publisher<T, H>(node, name, options, meter)`, `make_subscriber<T, H>(...)`,
`make_event_producer<T>(...)`, and the logger/provider constructors take the meter as an argument
and use `NullMeter` when it is omitted.

*Runtime `CounterPort`/`CounterFactory` pair* (the earlier draft) rejected: it works, but every
increment pays an indirect call even when counters are disabled, and it is a second injection
mechanism next to the concept-based loggers and sinks. *Passing `metrics::Meter*` directly into
`com`* rejected: it makes `com` (L1) depend on `metrics` (L2).

### D12. Perfetto export writes gap objects

### D12. Perfetto export writes gap objects

`perfetto_visitor` currently ignores `GapMarker`, so a trace dataset's holes disappear in Perfetto.
It gains a branch that emits one event per gap with `first_lost_sequence`, `lost_count`, and
`reason`, positioned at the gap record's event time on the affected channel's track. JSONL already
emits the decoded marker through the generic typed-payload path and is unchanged.

## Risks / Trade-offs

- **Disk saturation drops records** → `min_free_bytes`, rotation, retention, per-stream flags, and
  a counter plus `GapMarker` on recovery; the collector never blocks publishers.
- **Metric/trace loss used to be silent** → D9 makes every subscriber see sequence gaps; the
  remaining limit is the fixed publisher-tracking table, which bounds concurrent publishers per
  stream rather than hiding loss.
- **Telemetry service type changes** → publishers and subscribers must be rebuilt together; mixed
  deployments fail the connection compatibility check instead of silently misreading headers.
- **Counter templates instantiate per meter type** → one instantiation in practice; `NullMeter` is
  the default and produces no code; a compile-time test asserts a `NullMeter` endpoint has no
  counter storage.
- **Endpoint counters need a wired meter** → without one, endpoints use `NullMeter` and run
  unchanged; the collector still records metric frames produced elsewhere.
- **Concept drift between modules** → the concepts live once in `core`, and each module's
  conformance is pinned by a `static_assert` in its test TU.
- **Raw subscriber rides the iceoryx2 C API** → the C API is part of the same pinned revision and
  already generates the type-detail functions; a dedicated test covers payload/user-header type
  round-tripping so an upstream change fails loudly.
- **Event payloads are opaque in the session** → the channel records the payload type name and
  size, and export renders the header plus raw bytes; typed decoding would require embedding the
  application's payload schema, which the transport does not carry.
- **Metric names encode stream and side** (no labels in `metrics`) → names are fixed by D11 and
  covered by a test so dashboards do not drift.
- **Session size from three streams** → retention defaults and per-stream flags; `max_record_bytes`
  already bounds a frame to 1 MiB, well above the 256/384 B frames.
- **Schema drift between the POD and its descriptor** → `static_assert(sizeof/offsetof)` in the
  collector and a test that compares `builtin_schemas()` against the frame layouts; otherwise
  `info`/MCAP would describe fields that do not exist.
- **One writer means one failure domain** → a collector crash leaves the segment unclosed, which
  the manifest already reports and `nexigen-export verify` detects; recovery is a restart with a
  new session.
- **COM-only** → gate-off builds cannot record; accepted because the transport for multi-app
  collection is already iceoryx2.

## Migration Plan

1. Complete the built-in `MetricFrame` descriptor and pin all three descriptors to the C++ layouts;
   this lands independently and fixes `info`/MCAP regardless of the collector.
2. Add `core::CounterLike`/`MeterLike`/`NullMeter`, then the `com::TransportHeader`, subscriber gap
   reporting, the raw introspection subscriber, and the meter-constrained endpoints; migrate the
   three telemetry sinks, the three daemons, the event endpoints, the loggers, the tracer provider,
   and the chrono provider in one change. Mixed deployments are rejected by the service type check.
3. Add `nexigen-recd` behind `NEXIGEN_ENABLE_COM`, off by default in examples; it records the four
   streams. Counters arrive as ordinary metric frames and need no collector code.
4. Add Perfetto gap objects to `nexigen-export`; this lands independently and fixes trace holes.
5. Extend the existing COM CI jobs (Rust toolchain already installed) with a collector integration
   test: publish one frame of each stream plus one event, force a subscriber queue overflow, close
   the session, export JSONL and Perfetto, assert the datasets and the expected gap markers.
6. Document the tool, the transport header, the counters, and the "one destination per stream"
   rule; rollback is removing the binary and the config surface — no existing behavior changes.

## Open Questions

- Final binary and flag names (`nexigen-recd`, `--session-root`, `--logs/--metrics/--traces`).
- Whether the solution pack ships a ready-made `recd.json` alongside the other daemon configs.
- Whether `nexigen-hub` should later list recorded sessions; not required by this change.
