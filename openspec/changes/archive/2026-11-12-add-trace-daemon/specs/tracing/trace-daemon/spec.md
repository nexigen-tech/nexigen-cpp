# Spec Delta

## Purpose

Defines the central flight recorder: a daemon that drains span traffic into a bounded in-memory ring and writes a versioned binary snapshot when the health layer reports an incident.

## ADDED Requirements

### Requirement: Daemon drains spans through a blocking wait set

The `TraceDaemon` SHALL subscribe to the configured span service and drain all available frames on every `WaitSet` wake-up, combining an interval timer (default 20 ms) with a notification attachment on the recovery event channel. It SHALL not busy-poll while idle and SHALL append frames to its ring in arrival order.

#### Scenario: Published frames reach the ring within one interval

- **WHEN** a sink publishes a span while the daemon runs with a 20 ms drain interval
- **THEN** the frame is in the ring by the second subsequent wake-up and the retained order matches the publish order

#### Scenario: Idle daemon blocks instead of polling

- **WHEN** no spans and no recovery events arrive for an extended period
- **THEN** the daemon sleeps in the kernel, performs no disk I/O, and its service-core utilization is 0%

#### Scenario: The drain timer is configurable within bounds

- **WHEN** the configured interval is below 5 ms or above 1000 ms
- **THEN** configuration validation rejects it before any port is created

### Requirement: The ring overwrites the oldest frames

The ring SHALL allocate exactly `ring_capacity_records` frame slots at startup, append new frames at the head, and overwrite the oldest frame when full. It SHALL count overwritten frames and SHALL produce retained frames oldest-first.

#### Scenario: Capacity is enforced with oldest eviction

- **WHEN** `ring_capacity_records + 10` frames are pushed
- **THEN** the ring retains exactly the configured capacity, the overwrite counter is 10, and the oldest ten frames are gone

#### Scenario: Snapshot order is chronological

- **WHEN** a full ring is dumped
- **THEN** the frames appear from oldest to newest and each frame's sequence is greater than its predecessor's

#### Scenario: Pushing allocates nothing

- **WHEN** frames are pushed continuously after initialization with the allocation counter armed
- **THEN** zero heap allocations occur

### Requirement: A recovery notification triggers one snapshot

On a wake-up with at least one `RecoveryNotification` on the configured channel, the daemon SHALL first drain pending spans, then write exactly one snapshot for that wake-up, record the notification's function group, and clear the ring after a successful write. With recovery triggers disabled it SHALL keep recording and write nothing.

#### Scenario: Notification produces a snapshot

- **WHEN** a `RecoveryNotification` for group `ADAS_FG` arrives after spans were recorded
- **THEN** one snapshot file appears, its header names `ADAS_FG`, and it contains the spans recorded before the trigger

#### Scenario: Notifications in one wake-up coalesce

- **WHEN** two recovery notifications are pending in the same wake-up
- **THEN** exactly one snapshot is written and its header names the last notification's function group

#### Scenario: Disabled triggers write nothing

- **WHEN** `trigger_on_recovery` is false and a recovery notification arrives
- **THEN** no snapshot is written and the ring keeps recording

### Requirement: A snapshot is a versioned binary record

A snapshot SHALL begin with a fixed header carrying a magic marker, the format version, the retained frame count, the first start and last end timestamps, the ring overwrite count, the trigger function group, and the trigger timestamp. The header SHALL be followed by the retained frames as 384-byte `SpanFrame` records. Every header field SHALL have a fixed width and the whole file SHALL be pointer-free.

#### Scenario: Header describes the retained window

- **WHEN** a snapshot is read back
- **THEN** the magic and version match the writer, the frame count equals the ring's retained count, and the first/last timestamps bound every frame in the file

#### Scenario: Frames round-trip through the file

- **WHEN** known frames are dumped and read back
- **THEN** every field of every frame equals the value that entered the ring

#### Scenario: Unknown format is detectable

- **WHEN** a reader encounters a different magic or version
- **THEN** it can reject the file without interpreting the frame area

### Requirement: Snapshot retention is bounded

The daemon SHALL create the dump directory when missing, name snapshots `incident_trace_<UTC>.nxtr` with a sortable UTC stamp, and retain at most `max_dump_files` files, deleting the oldest. A failed write SHALL leave no partial file, SHALL not terminate the daemon, and SHALL be counted in diagnostics.

#### Scenario: Old snapshots are pruned

- **WHEN** a new snapshot would exceed `max_dump_files`
- **THEN** the oldest snapshot is deleted and exactly the configured number remain

#### Scenario: A failed write is contained

- **WHEN** the dump directory is not writable or the write fails midway
- **THEN** no partial snapshot remains, the daemon keeps draining, and the failure is visible in its diagnostics

### Requirement: Lifecycle and steady-state I/O

The daemon SHALL report `Running` through the application state reporter after its ports are open, apply the configured thread affinity, attempt `mlockall` best-effort without failing startup, and exit cleanly on `SIGTERM` or `SIGINT` without writing a snapshot. In steady state it SHALL perform no disk I/O.

#### Scenario: Readiness is reported after initialization

- **WHEN** the daemon finishes opening its subscriber, listener, and ring
- **THEN** a `Running` notification appears on its application state channel before the drain loop starts

#### Scenario: Graceful stop does not write a snapshot

- **WHEN** the daemon receives `SIGTERM`
- **THEN** it exits with code 0 and the dump directory contains no new snapshot

#### Scenario: Memory locking failure is non-fatal

- **WHEN** `mlockall` is denied by the environment
- **THEN** startup continues and the failure is reported on stderr

### Requirement: Trace daemon configuration

`TraceDaemonConfig` SHALL carry the span and recovery service names, the recovery event name, the subscriber depth, the drain interval, the ring capacity in records, the dump directory, the maximum dump count, the recovery-trigger flag, the application name, and the background thread specification. It SHALL reject empty names, an out-of-range interval, non-positive ring capacity, a relative dump directory, and a zero dump limit.

#### Scenario: Invalid configuration is rejected before startup

- **WHEN** any required name is empty, the interval is out of range, the ring capacity is zero, the directory is relative, or the dump limit is zero
- **THEN** validation fails before any port, thread, or directory is created

#### Scenario: Valid configuration passes validation

- **WHEN** the defaults are validated
- **THEN** the result is success and every name, interval, capacity, and directory satisfies its bound

### Requirement: Trace daemon configuration diagnostics

The configuration SHALL provide cold-path `to_string`, `std::formatter`, and a non-throwing JSON round-trip under `JSON_NOEXCEPTION=1`. Malformed input SHALL leave the parsed values unchanged.

#### Scenario: Configuration round-trips through JSON

- **WHEN** a valid configuration is serialized and parsed back
- **THEN** every field equals the original value and a hostile document leaves the parsed values unchanged

#### Scenario: Formatter matches the member diagnostic

- **WHEN** `std::format("{}", config)` and `config.to_string()` are evaluated
- **THEN** both produce the same text naming the services, interval, capacity, and directory
