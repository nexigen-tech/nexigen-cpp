# logging/log-daemon Specification

## Purpose

Defines the central log aggregation daemon that subscribes to `LogFrame` events from the platform's `Iceoryx2Logger` instances, formats a final JSON line from the frame's metadata, and writes it to disk with rotation — removing per-process formatting and disk I/O from client processes and consolidating the platform's log storage in one place.

## Requirements

### Requirement: Daemon subscribes and drains log frames

The `LogDaemon` SHALL open a `Subscriber<LogFrame>` on the configured iceoryx2 service and process incoming frames through a `WaitSet` drain loop without busy polling: the daemon thread SHALL sleep in the kernel until a frame arrives or a batch-flush timer fires.

#### Scenario: Frame arrives and is processed

- **WHEN** an `Iceoryx2Logger` publishes a frame
- **THEN** the daemon's subscriber receives it within one backoff window and the frame is handed to the formatter

#### Scenario: Idle daemon costs zero CPU

- **WHEN** no frames are published for an extended period
- **THEN** the daemon's service-core utilization is 0% (WaitSet sleep, no polling)

### Requirement: Daemon formats a final JSON line

The daemon SHALL render each received `LogFrame` into a complete JSON line. The monotonic `timestamp_ns` SHALL be formatted as an ISO8601 UTC date, `app_id` as a string, `log_level` as a canonical name, and `payload` as the message text. The resulting JSON SHALL match the structure produced by the existing per-process `FileSink` so `nexigen-hub` can read daemon-written files without modification.

#### Scenario: Frame renders to the canonical JSON line

- **WHEN** a frame with `timestamp_ns=123456789`, `app_id="LIDR"`, `log_level=Warning`, `thread_id=7`, `pid=42`, `payload="calibration lost"` is formatted
- **THEN** the output line contains `"app":"LIDR"`, `"level":"warning"`, `"timestamp":"<ISO8601>"`, `"thread":7`, `"pid":42`, `"message":"calibration lost"`

### Requirement: Daemon writes to disk with rotation

The daemon SHALL write each formatted JSON line to the configured output directory using the existing `detail::FileSink` rotation machinery. The daemon SHALL batch incoming frames and flush to disk when a pre-allocated buffer is full or a batch timer expires, minimizing write syscalls.

#### Scenario: Log file is written to the configured directory

- **WHEN** the daemon receives frames with `app_id="LIDR"` at UTC 2026-11-11T14:30
- **THEN** a file appears at `<log_directory>/2026-11-11T14/LIDR.jsonl` and subsequent frames land in the same file until the next hour boundary

#### Scenario: Flush empties the buffer on shutdown

- **WHEN** the daemon receives a `SIGTERM` or a stop request
- **THEN** all buffered lines are flushed to disk and `fsynced` before the daemon exits

### Requirement: LogDaemon configuration

`LogDaemonConfig` SHALL carry the service name to subscribe to, the output log directory, file size and archive limits, the minimum storage log level, the batch flush interval, and the background worker thread affinity. Invalid values SHALL be rejected at startup.

#### Scenario: Config loads from file

- **WHEN** a valid config file is parsed
- **THEN** the daemon opens the subscription, creates the output directory if needed, and starts processing

#### Scenario: Invalid config is rejected

- **WHEN** the service name is empty, or the log directory is not an absolute path
- **THEN** the daemon exits with a visible error before creating any thread or subscriber

### Requirement: LogDaemon diagnostics

The `LogDaemon` SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting the subscription service and the active output directory.

#### Scenario: Daemon renders its state

- **WHEN** `to_string()` and `std::format("{}", daemon)` are evaluated
- **THEN** the text names the service and the output directory, and the formatter output equals the member output

### Requirement: LogDaemon main thread applies a configurable spec

`LogDaemonConfig` SHALL carry a `main_thread` spec. When it is empty, the daemon SHALL derive a spec from the existing `bg_cpu_affinity` CPU set; when non-empty, it SHALL use the explicit spec. The daemon SHALL apply the effective spec on its own main thread before opening the subscriber and drain loop. A `Required` application failure SHALL exit non-zero before any port is created. The field SHALL participate in the config JSON round-trip.

#### Scenario: Default derives affinity from the legacy field

- **WHEN** `main_thread` is empty and `bg_cpu_affinity` names a CPU
- **THEN** the daemon main thread is pinned to that CPU

#### Scenario: Explicit spec adds scheduler attributes

- **WHEN** `main_thread` requests a CPU, policy, and priority
- **THEN** the main thread applies all of them before the first drain

#### Scenario: Required failure stops startup

- **WHEN** a required attribute in the effective spec is rejected
- **THEN** the daemon exits non-zero before creating its subscriber

#### Scenario: Main spec round-trips through JSON

- **WHEN** a config with a non-empty `main_thread` is serialized and parsed back
- **THEN** the recovered spec equals the original
