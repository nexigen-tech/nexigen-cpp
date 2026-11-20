# Tasks

## 1. Built-in frame descriptors

- [ ] 1.1 Complete the `nexigen.metrics.MetricFrame` descriptor in
      `src/recorder/builtin_schemas.cpp` (application code, pid, timestamp, and every
      `MetricRecord` field) and verify `nexigen-export info` lists those fields for a session
      containing a metric frame.
- [ ] 1.2 Pin all three built-in descriptors to their frames with compile-time `sizeof`/`offsetof`
      checks and a test that compares every descriptor field against `LogFrame`, `MetricFrame`, and
      `SpanFrame`; verify the test fails when a descriptor offset is changed by hand.
- [ ] 1.3 Document the pinned built-in descriptors in `docs/recorder.md` and verify the documented
      field list matches `nexigen-export info` output.

## 2. Transport primitives

- [ ] 2.1 Add `com::TransportHeader` (application code, pid, publisher id, sequence) with
      `to_string`, `std::formatter`, and non-throwing `to_json`/`from_json`; verify round-trip and
      hostile-JSON tests pass under `-fno-exceptions`.
- [ ] 2.2 Stamp the header on every `send`/`send_copy` and expose `lost_before()` on received
      samples; verify that contiguous sends report 0, skipped sends report N, a late-joining
      subscriber reports 0 on its first sample, and a restarted publisher starts a new baseline.
- [ ] 2.3 Consume `core::CounterLike`, `core::MeterLike`, and `core::NullMeter` from
      `unify-core-concepts` (this change defines none of them); verify the com endpoints compile
      against the pinned core concepts and no concept header is duplicated here.
- [ ] 2.4 Make `Publisher<T,H,M>`, `Subscriber<T,H,M>`, `EventProducer<T,M>`, and
      `EventConsumer<T,OnEvent,OnLoss,M>` meter-aware with `M = core::NullMeter`, create the
      endpoint counters once, and verify a `NullMeter` instantiation has no counter storage; add
      free `make_*` factories that deduce the meter type and verify a call site names only `T`/`H`.
- [ ] 2.5 Verify endpoint counters reach a configured metric sink and are recorded as ordinary
      metric frames; verify `metrics::Meter` is accepted through `core::MeterLike` without com
      including any metrics header.
- [ ] 2.6 Add `com::RawSubscriber` over the iceoryx2 C API (service type details, borrowed payload
      and user-header spans, payload type name); verify it subscribes to a `LogFrame` +
      `TransportHeader` service and to an `EventHeader` event service, and that an unknown service
      fails without leaving a port.
- [ ] 2.7 Migrate `Iceoryx2Logger`, `Iceoryx2MetricSink`, `Iceoryx2TraceSink`,
      `nexigen-logd`, `nexigen-metricsd`, `nexigen-traced`, and the event endpoints to the new
      headers and meter parameters; verify their tests pass and a subscriber using the old default
      header fails the compatibility check instead of misreading samples.

## 3. Collector scaffold and configuration

- [ ] 3.1 Add the `nexigen-recd` target under `tools/nexigen-recd/` behind
      `NEXIGEN_ENABLE_COM`, wire it into `tools/CMakeLists.txt`, and verify
      `cmake -B build-com -G Ninja -DNEXIGEN_ENABLE_COM=ON && cmake --build build-com -j 64`
      builds it while a gate-off configure does not.
- [ ] 3.2 Add `RecorderDaemonConfig` with service names, event services, session root, per-stream
      flags, rotation, retention, and metrics publishing; verify `to_string`, `std::formatter`, and
      `to_json`/`from_json` round-trip and hostile-JSON tests pass under `-fno-exceptions`.
- [ ] 3.3 Add CLI parsing (`--session-root`, `--service-log`, `--service-metrics`,
      `--service-traces`, `--event-service`, `--logs`, `--metrics`, `--traces`, `--events`) and
      verify that starting with every stream disabled exits non-zero, prints usage, and creates no
      session directory.

## 4. Frame capture pipeline

- [ ] 4.1 Subscribe to the three frame services, drain through a `WaitSet`, and append each frame
      byte-verbatim; verify an integration test that publishes one frame of each stream produces a
      session with three datasets whose payload sizes equal `sizeof` of the frames.
- [ ] 4.2 Declare one channel per `(application code, stream)` with the stream schema and a topic
      naming the application; verify two application codes produce two channels and a publisher
      restart with the same code reuses the channel id.
- [ ] 4.3 Map record times per design D3 — log/metric `timestamp_ns`, span `start_ns`, commit time
      from `CLOCK_MONOTONIC_RAW`, per-channel sequence; verify a span exports with its start time
      as the dataset event time.
- [ ] 4.4 Emit gaps from both sources: accumulated `LogFrame::dropped_counter` as
      `StreamDiscontinuity`, and `TransportHeader` sequence gaps as `IoxRingOverflow`; verify a
      skipped sequence produces a gap with the exact count, a zero delta produces none, and an
      append failure produces one gap on recovery.
- [ ] 4.5 Bind the session to `MonotonicRaw`, the boot token/UUID, and the time-base epoch when
      attached, and append `TimeSync` at start and after each rotation; verify the manifest carries
      the 16-hex boot token and that `nexigen-export` converts instants after a forced rotation.

## 5. Event capture

- [ ] 5.1 Subscribe to each configured event service through `RawSubscriber`, record
      `EventHeader` plus payload bytes as `ComPayload`, and declare one channel per
      `(application, service, payload type)`; verify an event with a 32-byte payload produces one
      record and a channel naming the type and size.
- [ ] 5.2 Track event sequence and producer epoch per publisher; verify a three-event skip writes
      one `IoxRingOverflow` gap and an epoch change writes none.
- [ ] 5.3 Handle a configured service that does not exist; verify the collector reports it, keeps
      recording the other streams, and stays running.

## 6. Instrumentation counters

- [ ] 6.1 Wire one meter per instrumented endpoint (named after its service) into the endpoints,
      loggers, tracer provider, and chrono provider at the composition root; verify
      publisher/subscriber counters appear in `nexigen-metricsd` with their meter and metric names
      and that two publishers never share one meter.
- [ ] 6.2 Verify every `com` endpoint counter increments correctly: publisher `published`,
      `publish_failed`, `evicted`; subscriber `received`, `lost`; event producer `published`,
      `publish_failed`; event consumer `received`, `lost`, `duplicates`.
- [ ] 6.3 Verify logger counters (`accepted`, three `dropped.*`) match the `dropped()` snapshot
      after a known drop mix.
- [ ] 6.4 Verify tracer counters (`started`, three `dropped.*`) match the tracer's `dropped()`
      snapshot after a known span mix.
- [ ] 6.5 Verify chrono counters (`calibration_failed`, `acquire_exhausted`, `snapshot_failed`)
      advance on the corresponding failure paths and preserve the returned statuses.
- [ ] 6.6 Verify a `NullMeter` endpoint, logger, tracer, and chrono provider contain no counter
      storage, and that a restart starts a new counter series in the session instead of a
      fabricated delta.

## 7. Session lifecycle

- [ ] 7.1 Apply rotation and retention limits from the configuration; verify a forced rotation
      keeps the session listable and retention deletes only closed segments.
- [ ] 7.2 Handle SIGTERM/SIGINT by draining, writing the footer, and closing the manifest; verify a
      clean close reports a closed segment while `SIGKILL` leaves it unclosed and
      `nexigen-export verify` fails.
- [ ] 7.3 Verify per-stream flags: disabling traces omits the trace dataset, and defaults of
      `nexigen-logd`, `nexigen-metricsd`, and `nexigen-traced` are unchanged by an untouched
      configuration.

## 8. Export, integration, and documentation

- [ ] 8.1 Emit gap objects in `perfetto_visitor` with `first_lost_sequence`, `lost_count`, and
      `reason`, positioned at the gap's event time; verify a session with three gaps produces
      exactly three ordered objects and a clean dataset produces none.
- [ ] 8.2 End-to-end test: record log, metric, trace, and event datasets from two application
      codes, force the collector's subscriber queue to overflow, then verify
      `nexigen-export list` reports all datasets, `info` reports every frame field, the expected
      `IoxRingOverflow` gaps are present in JSONL and Perfetto, and
      `extract --format jsonl|perfetto|mcap` succeeds on a copied session.
- [ ] 8.3 Extend the COM CI job to build and run the com gap-reporting, raw-subscriber, and
      collector integration tests with the pinned iceoryx2 revision; verify the job passes on Debug
      and Release.
- [ ] 8.4 Document the tool, its configuration, the transport header, the loss counters, and the
      "one destination per stream" rule in `docs/recorder.md`; refresh the module map and recorder
      flow in `docs/architecture.md` (§4.13, §5.6, Mermaid diagrams); verify the documented
      commands run as written.
- [ ] 8.5 Run the full validation ladder — Debug, Release, portable Release, TSan, ASan, and the
      coverage target — and verify 100% of tests pass with module coverage gates unchanged.

## Workflow follow-up

- Land and archive `unify-core-concepts` first; this change consumes its meter vocabulary and sink
  contract.
- Archive the change after review and verification.
- Rebuild the graph with `graphify update .` after the code lands.
