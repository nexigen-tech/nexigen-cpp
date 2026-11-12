# metrics/metrics-daemon Specification

## Purpose

Defines the central metrics daemon that subscribes to `MetricFrame` events across processes, aggregates the latest value per metric, and exports Prometheus text plus a quota-bounded JSON-lines history.

## Requirements

### Requirement: Daemon subscribes and drains frames without polling

The `MetricsDaemon` SHALL open a `Subscriber<MetricFrame>` on the configured iceoryx2 service and process incoming frames through a `WaitSet` drain loop, sleeping in the kernel until a frame arrives or a batch timer fires.

#### Scenario: Frame arrives and is processed

- **WHEN** an `Iceoryx2MetricSink` publishes frames
- **THEN** the daemon's subscriber receives them within one backoff window and hands them to the aggregator

#### Scenario: Idle daemon costs zero CPU

- **WHEN** no frames are published for an extended period
- **THEN** the daemon's service-core utilization is 0% (WaitSet sleep, no polling)

### Requirement: Daemon aggregates the latest record per metric

The daemon SHALL keep, for each distinct `(app, meter name, metric name)` key, the most recently received `MetricRecord`, so a scrape reflects current values regardless of the number of publishing processes.

#### Scenario: Latest record wins

- **WHEN** two frames from the same `(app, meter, metric)` key arrive in order
- **THEN** the aggregate holds the record from the later frame and exposes exactly one time series for that key

#### Scenario: Distinct metrics are kept separately

- **WHEN** frames for two different metric names arrive from the same application
- **THEN** the aggregate holds a distinct latest record for each metric name

### Requirement: Daemon renders Prometheus text exposition

The daemon SHALL render each aggregated record into Prometheus text exposition lines: counters and gauges as `# TYPE`-tagged samples; `DescriptiveStatistics` as a `_mean`/`_stddev`/`_min`/`_max`/`_count` gauge family; histograms as a `# TYPE <name> histogram` with cumulative `_bucket{le="..."}` lines plus `_sum` and `_count`; and state records as a gauge with a `state` label valued `1`.

#### Scenario: Scalar metrics render

- **WHEN** an aggregated `Counter` record is rendered
- **THEN** the output contains a `# TYPE <name> counter` line and a sample line carrying the counter value

#### Scenario: Descriptive statistics render as a gauge family

- **WHEN** an aggregated `DescriptiveStatistics` record is rendered
- **THEN** the output contains `<name>_mean`, `<name>_stddev`, `<name>_min`, `<name>_max`, and `<name>_count` samples carrying the snapshot's mean, stddev, min, max, and count

#### Scenario: Histograms render cumulative buckets

- **WHEN** an aggregated histogram record whose per-bucket counts are not cumulative is rendered
- **THEN** the output contains `_bucket` lines with monotonically non-decreasing counts ending in `le="+Inf"` at the total, plus `_sum` and `_count` lines

#### Scenario: State records render as a labelled gauge

- **WHEN** an aggregated `State` record holding `"CALIBRATING"` is rendered
- **THEN** the output contains a sample whose `state` label is `"CALIBRATING"` and whose value is `1`

### Requirement: Prometheus labels and metadata

Rendered samples SHALL carry `app`, `meter`, `version`, and `pid` labels from the frame and record identity, SHALL emit the metric unit through a `# UNIT` comment, and SHALL NOT append per-sample timestamps.

#### Scenario: Labels are present and stable

- **WHEN** a record is rendered
- **THEN** its sample line carries `app`, `meter`, `version`, and `pid` labels, and no timestamp suffix is present

#### Scenario: Unit is a comment, not a label

- **WHEN** a record carrying a unit is rendered
- **THEN** a `# UNIT <name> <unit>` line precedes the samples, and no `unit` label appears on any sample line

### Requirement: Daemon serves GET /metrics over HTTP

The daemon SHALL serve the rendered exposition on an HTTP endpoint via a minimal server: a `GET` to the configured endpoint SHALL return `200` with a Prometheus text content type, and every other path SHALL return `404`.

#### Scenario: Metrics endpoint returns exposition

- **WHEN** an HTTP client issues `GET /metrics`
- **THEN** the response status is `200`, its content type is `text/plain; version=0.0.4`, and its body contains the aggregated exposition lines

#### Scenario: Unknown path returns 404

- **WHEN** an HTTP client requests a path other than the configured endpoint
- **THEN** the response status is `404`

### Requirement: Daemon writes a quota-bounded JSON-lines history

The daemon SHALL append each received window as one JSON line to a file in the configured storage directory, and SHALL bound total storage so that exceeding the quota rotates or discards the oldest data. On shutdown the daemon SHALL flush and `fsync` pending history.

#### Scenario: Window is appended as JSON

- **WHEN** a window of frames is received
- **THEN** a parseable JSON line appears in the storage directory, and its timestamp and records match the received window

#### Scenario: Shutdown flushes history

- **WHEN** the daemon receives `SIGTERM` or a stop request
- **THEN** all buffered history is flushed and `fsync`ed before the daemon exits

### Requirement: MetricsDaemon configuration

`MetricDaemonConfig` SHALL carry the subscription service name, the HTTP bind address, port, and endpoint, the storage directory and quota, and the background worker thread affinity. Invalid values SHALL be rejected at startup.

#### Scenario: Valid config starts the daemon

- **WHEN** a valid config file is parsed
- **THEN** the daemon opens the subscription, binds the HTTP endpoint, creates the storage directory if needed, and starts processing

#### Scenario: Invalid config is rejected

- **WHEN** the service name is empty or the storage directory is not an absolute path
- **THEN** the daemon exits with a visible error before creating any thread, subscriber, or listener

### Requirement: MetricsDaemon diagnostics

`MetricDaemonConfig` SHALL provide a cold-path `to_string()` member plus a free helper and a `std::formatter` specialization, and SHALL round-trip through `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. The daemon SHALL report its subscription service and active output directory at startup.

#### Scenario: Config renders its state

- **WHEN** `to_string()`, the free `to_string`, and `std::format("{}", ...)` are evaluated on a `MetricDaemonConfig`
- **THEN** the text names the subscription service, the HTTP endpoint, and the storage directory, and the formatter output equals the member output

#### Scenario: Config serialization round-trip

- **WHEN** a `MetricDaemonConfig` is serialized with `to_json` and parsed back with `from_json` under `JSON_NOEXCEPTION=1`
- **THEN** all fields survive the round-trip unchanged

### Requirement: MetricsDaemon HTTP thread is configurable

`MetricDaemonConfig` SHALL carry an `http_thread` spec. When it is empty, the HTTP serving thread SHALL inherit `bg_cpu_affinity` through `effective_thread_spec`; when non-empty, it SHALL use the explicit spec. The HTTP front end SHALL serve on a `WorkerThread` and SHALL stop and join it during daemon shutdown before closing the listener. The field SHALL participate in the config JSON round-trip.

#### Scenario: Empty HTTP spec inherits the background spec

- **WHEN** `http_thread` is empty and `bg_cpu_affinity` names a CPU
- **THEN** the HTTP thread runs on that CPU

#### Scenario: Explicit HTTP spec wins

- **WHEN** `http_thread` names a different CPU from `bg_cpu_affinity`
- **THEN** the HTTP thread runs on the explicit CPU

#### Scenario: Shutdown joins the HTTP thread

- **WHEN** the daemon stops
- **THEN** the HTTP worker observes the stop request, joins, and the listener closes afterwards

#### Scenario: HTTP spec round-trips through JSON

- **WHEN** a config with a non-empty `http_thread` is serialized and parsed back
- **THEN** the recovered spec equals the original
