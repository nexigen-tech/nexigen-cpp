# Spec Delta

## Purpose

Defines runtime-owned observability for the CANopen node stack: the metric catalogue, hot- and
cold-path logging policy, bounded error suppression, and startup registration checks, while the
CANopen core stays free of logger, meter, and tracer dependencies.

## ADDED Requirements

### Requirement: CANopen core remains telemetry-free

The `nexigen::canopen` core SHALL NOT accept, store, or call a logger, meter, tracer, or
diagnostic sink. Parsers, decoders, and assemblers SHALL report outcomes through `Status`,
`Result`, and structured result values; observability SHALL be applied only by the runtime layer.

#### Scenario: A decode failure is reported without logging

- **WHEN** a PDO payload produces a decode error in the core
- **THEN** the result carries the structured status
- **AND** the core emits no log record and records no metric

#### Scenario: EDS diagnostics stay in the result

- **WHEN** EDS parsing fails in the core
- **THEN** the diagnostics are returned to the caller
- **AND** the core does not write to a log or metric provider

### Requirement: One runtime owns one logger and one meter per bus

A CANopen bus runtime SHALL accept a borrowed logger and a borrowed meter at construction. The
logger type SHALL be a template parameter; the meter SHALL be supplied by reference. Each runtime
SHALL create its instruments on one meter named `canopen.<bus>` and SHALL reject duplicate bus
names.

#### Scenario: A bus runtime uses its own meter

- **WHEN** a runtime is created for bus `can0`
- **THEN** every Tier 1 instrument is created on the meter named `canopen.can0`

#### Scenario: Duplicate bus names are rejected

- **WHEN** a second runtime is created with the same bus name as an existing runtime
- **THEN** creation fails with `InvalidArgument`
- **AND** no transport is opened

#### Scenario: A null logger keeps the runtime working

- **WHEN** the runtime is instantiated with `logging::NullLogger`
- **THEN** it operates without emitting log records
- **AND** metric recording is unchanged

### Requirement: Tier 1 loop metrics

The runtime SHALL record `canopen_cycles_total`, `canopen_cycle_overruns_total`,
`canopen_loop_latency_ns`, and the current runtime state through the bus meter. The state SHALL use
`Stopped`, `Starting`, `Running`, `Degraded`, or `Error`.

#### Scenario: A completed cycle is counted and timed

- **WHEN** one runtime cycle completes
- **THEN** `canopen_cycles_total` increases by one
- **AND** one `canopen_loop_latency_ns` sample is recorded

#### Scenario: An overrun is visible

- **WHEN** a cycle starts after its deadline
- **THEN** `canopen_cycle_overruns_total` increases by one

#### Scenario: Runtime state is a state gauge

- **WHEN** the runtime transitions from `Starting` to `Running`
- **THEN** the `canopen_state` gauge reports `Running`

### Requirement: Tier 1 traffic metrics

The runtime SHALL record `canopen_frames_received_total`, `canopen_frames_processed_total`,
`canopen_frames_unrouted_total`, `canopen_tx_frames_total`, `canopen_pdo_rx_total`,
`canopen_pdo_tx_total`, `canopen_sdo_requests_total`, `canopen_nmt_transitions_total`,
`canopen_emcy_total`, and `canopen_signal_updates_total`.

#### Scenario: Received and processed frames are counted

- **WHEN** a frame is read from the transport and handled successfully
- **THEN** `canopen_frames_received_total` and `canopen_frames_processed_total` each increase by
  one

#### Scenario: An unrouted frame is not processed

- **WHEN** a frame has no COB-ID route
- **THEN** `canopen_frames_unrouted_total` increases by one
- **AND** `canopen_frames_processed_total` does not increase

#### Scenario: PDO traffic is counted separately

- **WHEN** a PDO frame is received and a PDO frame is transmitted
- **THEN** `canopen_pdo_rx_total` and `canopen_pdo_tx_total` each increase by one

### Requirement: Tier 1 error metrics

The runtime SHALL record error counters for frame drops, PDO decoding and mapping, SDO aborts and
timeouts, heartbeat misses, signal quality, TX failures, and bus-off. `canopen_frames_dropped_total`
SHALL equal the sum of `canopen_rx_overflow_total`, `canopen_tx_queue_full_total`, and
`canopen_driver_drop_total`.

#### Scenario: A dropped frame increments total and one cause

- **WHEN** the ingress queue overflows for one frame
- **THEN** `canopen_frames_dropped_total` and `canopen_rx_overflow_total` each increase by one

#### Scenario: PDO decode and mapping errors are distinct

- **WHEN** a PDO decode returns `Partial`
- **THEN** `canopen_pdo_decode_errors_total` increases and `canopen_pdo_mapping_errors_total` does
  not

- **WHEN** a PDO decode returns `UnknownObject`
- **THEN** `canopen_pdo_mapping_errors_total` increases and `canopen_pdo_decode_errors_total` does
  not

#### Scenario: SDO failures are counted separately

- **WHEN** an SDO abort event is observed
- **THEN** `canopen_sdo_aborts_total` increases by one

- **WHEN** a segmented SDO transfer expires
- **THEN** `canopen_sdo_timeouts_total` increases by one

#### Scenario: Heartbeat and bus failures are counted

- **WHEN** a heartbeat timeout episode starts
- **THEN** `canopen_heartbeat_misses_total` increases by one

- **WHEN** the bus enters bus-off
- **THEN** `canopen_bus_off_total` increases by one

### Requirement: Tier 1 saturation metrics

The runtime SHALL expose `canopen_rx_queue_depth`, `canopen_tx_queue_depth`,
`canopen_sdo_active_transfers`, and `canopen_nodes_online` as gauges reflecting the current state.

#### Scenario: Queue depth changes are visible

- **WHEN** the ingress queue depth changes from three to four
- **THEN** `canopen_rx_queue_depth` reports four

#### Scenario: Active SDO transfers are visible

- **WHEN** a segmented SDO transfer starts and later completes
- **THEN** `canopen_sdo_active_transfers` reflects the active count at each observation

### Requirement: Hot-path metric recording is wait-free and allocation-free

Recording a Tier 1 metric SHALL be wait-free and SHALL NOT allocate, format text, take a lock, or
perform virtual dispatch. Instruments SHALL use the `MultiThreaded` concurrency policy so a
background harvester may read them concurrently with the runtime thread.

#### Scenario: No allocation during recording

- **WHEN** Tier 1 metrics are recorded with the allocation trap armed
- **THEN** no heap allocation occurs

#### Scenario: Concurrent harvesting is race-free

- **WHEN** a background harvester snapshots Tier 1 instruments while the runtime records
- **THEN** the run is clean under ThreadSanitizer

### Requirement: Hot-path errors always increment a metric

Every hot-path error SHALL increment its Tier 1 metric before any level check or suppression. A
suppressed log SHALL NOT suppress the metric.

#### Scenario: A filtered log still counts

- **WHEN** the logger threshold is `Fatal` and an `Error`-level hot-path error occurs
- **THEN** the error metric still increases by one

#### Scenario: Repeated errors keep counting

- **WHEN** the same hot-path error occurs ten times inside one episode
- **THEN** its metric increases by ten even if fewer than ten log records are emitted

### Requirement: Hot-path error logging follows episode policy

The runtime SHALL log the first error of an episode, an error-code change, recovery after the hold
interval, and bounded repeats. It SHALL NOT log every occurrence of a repeated error.

#### Scenario: First error opens an episode and logs

- **WHEN** a hot-path error occurs for an idle `(domain, node, code)` key
- **THEN** one log record is emitted
- **AND** the episode becomes active

#### Scenario: Repeated error is suppressed inside the interval

- **WHEN** the same error repeats before `repeat_interval_ms`
- **THEN** no additional log record is emitted
- **AND** the suppressed count increases

#### Scenario: A forced repeat is emitted after the threshold

- **WHEN** suppressed occurrences for one key reach `max_suppressed_per_key`
- **THEN** one log record reports the suppressed count
- **AND** the suppressed count resets

#### Scenario: A code change logs immediately

- **WHEN** an active episode reports a different error code
- **THEN** one log record is emitted immediately

#### Scenario: Recovery is logged after the hold interval

- **WHEN** no error recurs for `recovery_hold_ms`
- **THEN** one recovery log record reports the total suppressed count
- **AND** the episode closes

### Requirement: Suppression defaults and global budget

The log policy SHALL default to `repeat_interval_ms=5000`, `recovery_hold_ms=5000`,
`max_suppressed_per_key=10000`, `summary_interval_ms=60000`, `max_hot_logs_per_second=50`,
`hot_error_level=Error`, `recovery_level=Info`, and `summary_level=Info`. First-error and recovery
records SHALL bypass the global budget; repeat records SHALL respect it.

#### Scenario: Defaults apply to a default policy

- **WHEN** a runtime uses a default `CanOpenLogPolicy`
- **THEN** the effective intervals and levels are the documented defaults

#### Scenario: Global budget bounds repeats

- **WHEN** repeat logs exceed `max_hot_logs_per_second`
- **THEN** excess repeats are suppressed and counted
- **AND** a first-error or recovery record is still emitted

#### Scenario: Summary is emitted only after activity

- **WHEN** a summary interval elapses with no errors or suppressed records
- **THEN** no summary log record is emitted

- **WHEN** a summary interval elapses after suppressed errors
- **THEN** one summary log record per active domain reports the counts

### Requirement: Cold-path logging is detailed and bounded

The runtime SHALL log lifecycle, bus configuration, EDS/DCF loading, and NMT transitions with
context. EDS parser diagnostics SHALL be capped at `max_cold_diagnostics` (default 32) followed by
a count of omitted diagnostics. Cold-path logging MAY allocate.

#### Scenario: EDS success records the load

- **WHEN** an EDS file loads successfully
- **THEN** one cold-path record reports the path, content hash, object count, and PDO mapping count

#### Scenario: EDS failure is capped

- **WHEN** an EDS file produces more than 32 diagnostics
- **THEN** the runtime logs the first 32 diagnostics
- **AND** one trailing record reports how many diagnostics were omitted

#### Scenario: Lifecycle and NMT transitions are logged

- **WHEN** the runtime starts, stops, or a node changes NMT state
- **THEN** one cold-path record describes the transition

### Requirement: Each event has one owner

Each observable event SHALL have exactly one metric owner and at most one log owner. The core SHALL
not log, and the runtime SHALL not duplicate counters already owned by `com` or `logging`. A COM
publish failure SHALL use the COM producer's counters and only the runtime edge log.

#### Scenario: SDO abort has one counter and one owner

- **WHEN** an SDO abort is observed
- **THEN** exactly one CANopen metric counts it
- **AND** only the runtime decides whether to log it

#### Scenario: COM publish failure is not double-counted

- **WHEN** the runtime publishes an event and the COM producer reports a failure
- **THEN** the COM producer's `publish_failed` counter is the metric
- **AND** the runtime does not create a second CANopen publish-failure counter

### Requirement: Startup registration validates names and capacity

Before opening the CAN transport, the runtime SHALL create every Tier 1 instrument. On an invalid or
duplicate metric name, or exhausted provider capacity, it SHALL log the failing instrument and
return the corresponding error; no transport SHALL be opened.

#### Scenario: Exhausted capacity fails startup

- **WHEN** the meter has insufficient counter capacity for one Tier 1 instrument
- **THEN** runtime creation fails with `ResourceExhausted`
- **AND** no transport is opened

#### Scenario: Duplicate metric name fails startup

- **WHEN** a Tier 1 metric name is already registered on the same meter
- **THEN** runtime creation fails with `InvalidArgument`
- **AND** the diagnostic names the duplicate metric

#### Scenario: Successful registration creates every instrument

- **WHEN** runtime creation succeeds
- **THEN** every Tier 1 metric exists on the bus meter with the documented kind and unit

### Requirement: Error tracking is fixed-size

The runtime SHALL track error episodes for at most 128 nodes and 9 domains in storage allocated at
startup. Detecting, suppressing, or recovering an error SHALL NOT allocate or resize storage.

#### Scenario: Error handling does not allocate

- **WHEN** an error episode starts, repeats, or recovers with the allocation trap armed
- **THEN** no heap allocation occurs

#### Scenario: The gate covers the CANopen node range

- **WHEN** errors are tracked for node identifiers 0 through 127
- **THEN** each node has an independent episode slot per error domain

### Requirement: Observability vocabulary is serializable

The public `CanOpenLogPolicy`, `CanOpenRuntimeState`, and `CanOpenErrorDomain` types SHALL provide
cold-path `to_string`, `std::formatter`, and non-throwing `nlohmann::json` serialization and
deserialization under `-fno-exceptions`.

#### Scenario: Runtime state round-trips

- **WHEN** `CanOpenRuntimeState` is rendered with `to_string` and parsed with `from_string`
- **THEN** the parsed value equals the original

#### Scenario: Error domain is formatted and serialized

- **WHEN** `CanOpenErrorDomain` is passed to `std::format` and to `nlohmann::json`
- **THEN** both use the canonical lowercase name
- **AND** parsing the JSON restores the same enumerator

#### Scenario: Log policy round-trips through JSON

- **WHEN** a `CanOpenLogPolicy` is serialized and parsed back
- **THEN** every field is unchanged

- **WHEN** a log policy is parsed from malformed or missing fields
- **THEN** parsing does not throw and leaves the corresponding fields unchanged

#### Scenario: Log policy formatter matches to_string

- **WHEN** a `CanOpenLogPolicy` is formatted with `std::format`
- **THEN** the text equals its `to_string()` output
