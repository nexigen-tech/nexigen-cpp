# recorder/telemetry-capture Specification

## Purpose
Records the logs, metrics, traces, and `com` events that applications of a suite publish to the
configured services into one recorder session, so a single artifact holds a correlated timeline
that `nexigen-export` can decode without new format support.

## Requirements

### Requirement: Suite-wide single-writer capture

The collector SHALL subscribe to the configured log, metric, trace, and event services and write
every received frame or event of an enabled stream into one session with exactly one writer. Frames
from different applications SHALL share that session. At least one stream SHALL be enabled for a
session to start.

#### Scenario: Two applications share one session

- **WHEN** two applications publish frames on the same log service and log capture is enabled
- **THEN** both applications' records appear in the single session created by the collector

#### Scenario: Disabled stream produces no dataset

- **WHEN** trace capture is disabled and metric and log capture are enabled
- **THEN** the session contains log and metric datasets and no trace dataset

#### Scenario: No enabled stream is rejected

- **WHEN** the collector is started with every stream disabled
- **THEN** it exits with a usage error and creates no session directory

### Requirement: Per-application channel identity

For every observed `(application code, stream)` pair the collector SHALL declare the stream's
schema and a channel before the first record that uses it. The channel topic SHALL name the stream
and the application code, and the publisher process id SHALL remain in the frame payload rather
than in the channel metadata.

#### Scenario: Each application gets its own channel

- **WHEN** frames from application codes `LIDR` and `ADAS` arrive on the log service
- **THEN** the session declares two log channels whose topics identify `LIDR` and `ADAS`

#### Scenario: Restart does not fragment the dataset

- **WHEN** an application restarts with the same code and schema and publishes again
- **THEN** the collector reuses the existing channel and no duplicate dataset appears

#### Scenario: Different payload schema gets a new channel

- **WHEN** a stream's frame schema changes within one session
- **THEN** the collector declares a new channel before the first record with the new schema

### Requirement: Byte-verbatim frame storage

The collector SHALL store each accepted frame as its exact POD bytes under the frame's record type,
with no re-serialization. The record event time SHALL be the frame's native monotonic timestamp
(`LogFrame::timestamp_ns`, `MetricFrame::timestamp_ns`, `SpanFrame::start_ns`), and the commit time
SHALL be the collector's write-time reading of `CLOCK_MONOTONIC_RAW`.

#### Scenario: Payload length equals the frame size

- **WHEN** a 256-byte log frame is recorded
- **THEN** the stored record's payload size is exactly 256 bytes and the exporter decodes it as a
  `LogFrame`

#### Scenario: Span event time is the span start

- **WHEN** a span with `start_ns` earlier than its `publish_ts_ns` is recorded
- **THEN** the dataset's event time is the span start, not the publish or commit time

### Requirement: Loss becomes a gap record

Frame-reported drops and transport gaps SHALL both produce a `GapMarker` at the receiving position:
accumulated `LogFrame::dropped_counter` per `(application code, process id)` as
`GapReason::StreamDiscontinuity`, and the transport header's missing sequence count as
`GapReason::IoxRingOverflow`. A zero delta SHALL write no gap record.

#### Scenario: Drop delta produces a gap at its position

- **WHEN** a log frame reports two dropped records and a later frame from the same publisher is
  appended
- **THEN** a gap record with count 2 appears immediately before that later frame's record

#### Scenario: Transport gap produces a gap

- **WHEN** the transport header reports three missed samples before a metric frame
- **THEN** a gap record with count 3 and reason `IoxRingOverflow` appears before that frame's record

#### Scenario: Zero delta writes no gap

- **WHEN** a frame reports no dropped records and its sequence is contiguous
- **THEN** no gap record is written before it

#### Scenario: Recovery writes the gap once

- **WHEN** an append fails and later succeeds for the same channel
- **THEN** the session contains one gap record for the failed span of records, not one per failure

### Requirement: No fabricated loss coverage

The collector SHALL NOT infer loss from idle time, frame cadence, or missing frame classes. Only
frame-reported counters and sequence gaps reported by the transport header SHALL produce a gap
record.

#### Scenario: Idle publisher is not a gap

- **WHEN** an application publishes nothing for several seconds and then resumes
- **THEN** no gap record is written for the idle interval

#### Scenario: Contiguous metric frames report nothing

- **WHEN** metric frames arrive with consecutive transport sequences
- **THEN** the session contains no gap record

#### Scenario: First sample after connect is not a gap

- **WHEN** the collector starts while publishers are already running
- **THEN** the first sample from each publisher starts a baseline and writes no gap record

### Requirement: Session time binding and calibration

The session SHALL declare the `MonotonicRaw` time domain and SHALL carry the current boot token and
boot UUID and the time-base epoch when a time base is attached. The collector SHALL append a
`TimeSync` record at session start and after every segment rotation.

#### Scenario: Session names its boot

- **WHEN** a session is opened
- **THEN** its manifest carries the 16-hex boot token for the running kernel boot

#### Scenario: Rotation keeps UTC resolvable

- **WHEN** the segment rotates during recording
- **THEN** the new segment is preceded or followed by a `TimeSync` record that keeps the whole
  session convertible to UTC

### Requirement: Session lifecycle and clean shutdown

The collector SHALL apply the configured rotation and retention limits, finalize the active segment
with its footer and closed state on SIGTERM or SIGINT, and leave an unclosed segment that
`nexigen-export verify` reports when the process dies without shutdown.

#### Scenario: SIGTERM closes the session

- **WHEN** the collector receives SIGTERM while draining frames
- **THEN** it drains what is queued, writes the footer, and the manifest reports a cleanly closed
  segment

#### Scenario: Crash is visible

- **WHEN** the collector process is killed without shutdown
- **THEN** the manifest reports the segment as not cleanly closed and verification fails

#### Scenario: Retention deletes only closed segments

- **WHEN** the retention limit is reached while the active segment is still open
- **THEN** only closed segments are deleted

### Requirement: Opt-in configuration without changing existing defaults

Recording SHALL be opt-in. The collector SHALL accept the service name, session root, per-stream
enable flags, rotation, and retention limits, and SHALL NOT change the defaults of
`nexigen-logd`, `nexigen-metricsd`, `nexigen-traced`, or any producer.

#### Scenario: Defaults stay untouched

- **WHEN** the collector is not started
- **THEN** log, metric, and trace daemons behave exactly as before

#### Scenario: Custom service names are honoured

- **WHEN** the collector is configured with a non-default log service name
- **THEN** it subscribes only to that service and records frames from it

### Requirement: Export compatibility

Recorded log, metric, and span datasets SHALL appear in the exporter's dataset listing and SHALL be
extractable to JSONL, Perfetto, and MCAP using the built-in descriptors, with no collector-specific
decoder.

#### Scenario: Three datasets are listable

- **WHEN** a session recorded one log, one metric, and one span dataset
- **THEN** `nexigen-export list` reports all three with their byte and time ranges

#### Scenario: Extract needs only the session

- **WHEN** `nexigen-export extract` runs on a copied session with no collector binary present
- **THEN** it decodes and writes the selected datasets

### Requirement: Collector configuration serialization

The collector's configuration type SHALL provide a cold-path `to_string()`, a `std::formatter`
specialization, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Hostile or
absent JSON fields SHALL leave the destination unchanged.

#### Scenario: Configuration round-trips

- **WHEN** a configuration is rendered to JSON and parsed back
- **THEN** every consumed field holds its original value

#### Scenario: Hostile JSON is non-throwing

- **WHEN** a field has the wrong type or a stream flag is not a boolean
- **THEN** parsing leaves that field unchanged and does not throw

### Requirement: Event channel capture

The collector SHALL record `com` event channels as `ComPayload` records without a compile-time
payload type. Each record SHALL carry the event header followed by the payload bytes, and its
channel SHALL name the application, service, payload type, and payload size. A new producer epoch
SHALL start a fresh sequence baseline.

#### Scenario: Event is recorded with header and payload

- **WHEN** an application publishes an event with a 32-byte payload
- **THEN** the session stores one `ComPayload` record whose payload is the event header followed
  by the 32 payload bytes

#### Scenario: Channel names the payload type

- **WHEN** events of the type `CameraFrame` are recorded
- **THEN** the channel metadata names `CameraFrame` and its payload size

#### Scenario: Epoch change is not a gap

- **WHEN** an event producer restarts and its producer epoch changes
- **THEN** the collector starts a new baseline and writes no gap record for the reset sequence

#### Scenario: Event transport gap is recorded

- **WHEN** event sequences skip three values within one producer epoch
- **THEN** a gap record with count 3 and reason `IoxRingOverflow` precedes the next event

#### Scenario: Unknown event service is skipped

- **WHEN** a configured event service does not exist
- **THEN** the collector reports it and continues recording the remaining streams

### Requirement: Instrumentation counters are recorded as metrics

The collector SHALL record the counters published by telemetry components as ordinary metric
frames. It SHALL NOT derive loss counters from frame or header fields. Counters for published,
received, lost, failed, evicted, accepted, and dropped messages or spans SHALL appear in the
session as metric records with their meter and metric names preserved.

#### Scenario: Component counters appear in the session

- **WHEN** applications publish endpoint, logger, tracer, or chrono counters through their metric
  sink
- **THEN** the session contains those counters as metric records under their original meter and
  metric names

#### Scenario: Collector does not synthesize counters

- **WHEN** a stream carries no component counters
- **THEN** the collector records no loss counter of its own for that stream

#### Scenario: Counter values survive a publisher restart

- **WHEN** an application restarts and its counters start again from zero
- **THEN** the session shows the new counter series as a new source rather than a negative or
  fabricated delta

#### Scenario: Counters are visible without a session

- **WHEN** metrics are published and a metrics subscriber is connected
- **THEN** the endpoint counters are readable through `nexigen-metricsd` without opening the session
