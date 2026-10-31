# chrono/chrono-provider Specification

## Purpose

Defines the clock service: a `ChronoProvider` that samples the base clock domains on a CPU-pinned background worker and delivers a UTC-offset snapshot to a bound sink, with a control plane and clean shutdown.

## Requirements

### Requirement: Provider construction and sink binding

`ChronoProvider` SHALL be constructed from a `ChronoProvider_config` and a bound `ChronoSink`. It SHALL pre-allocate its entire working memory during construction so steady-state snapshotting and delivery never allocate, and it SHALL report configuration errors through the error model rather than terminating. Construction SHALL NOT start the background worker; `snapshot_now()` SHALL be usable immediately after construction.

#### Scenario: Construction succeeds with a valid config and sink

- **WHEN** a provider is constructed with a valid configuration and a healthy sink
- **THEN** construction succeeds and `startup_status()` returns success

#### Scenario: Construction does not start the worker

- **WHEN** a provider with a non-zero `collect_interval` is constructed
- **THEN** no snapshot is delivered to the sink until `start()` is called

#### Scenario: Steady-state sampling allocates nothing

- **WHEN** snapshots are produced repeatedly after construction under an armed allocation arena
- **THEN** no `operator new`, `delete`, `malloc`, `free`, or container growth is observed

### Requirement: Background snapshot delivery and collection period

`ChronoProvider` SHALL run a background snapshot worker (a `std::jthread`), started via `start()`, that delivers one `Chrono_snapshot` per configured `collect_interval` (a `std::chrono::nanoseconds` field of `ChronoProvider_config`) to the bound sink. A zero interval SHALL disable the background thread and leave `snapshot_now()` as the sole snapshot trigger.

#### Scenario: Configured period is honored

- **WHEN** the provider is configured with a small interval, `start()` is called, and the test waits several periods
- **THEN** the sink observes multiple snapshots whose spacing is consistent with the interval within a documented scheduling tolerance

#### Scenario: Zero interval disables background delivery

- **WHEN** `collect_interval` is zero and `start()` is called
- **THEN** no snapshot is delivered until `snapshot_now()` is called explicitly

#### Scenario: Manual snapshot is deterministic

- **WHEN** `snapshot_now()` is called once
- **THEN** exactly one snapshot is delivered to the sink on the calling thread before the call returns

### Requirement: CPU core affinity

When `ChronoProvider_config::core_mask` is non-zero, the background snapshot worker SHALL pin itself to exactly those CPU cores with `pthread_setaffinity_np` before delivering any snapshot, so snapshot sampling and sink I/O cannot preempt unpinned real-time threads. A zero mask SHALL leave affinity unchanged.

#### Scenario: Worker affinity is applied

- **WHEN** the provider starts with a non-zero mask containing permitted CPUs and the worker's actual affinity is read back with `pthread_getaffinity_np`
- **THEN** the worker's mask equals the configured mask

#### Scenario: Invalid mask is visible without stopping delivery

- **WHEN** the configured mask contains no permitted CPU or `pthread_setaffinity_np` fails
- **THEN** `startup_status()` reports `core::ErrorCode::SystemError`, the worker still runs unpinned, and snapshots still flow

#### Scenario: Zero mask leaves affinity alone

- **WHEN** the provider starts with `core_mask == 0`
- **THEN** the worker inherits the process affinity and no pinning call is required to succeed

### Requirement: Snapshot content

Every snapshot the provider delivers SHALL carry a monotonic-raw `stamp` (the series key) and the signed UTC offset (`realtime` minus `monotonic_raw`) sampled at that instant. `boottime` SHALL be reserved in `Clock_id` but not sampled or recorded in this version.

#### Scenario: Snapshot carries the key and the offset

- **WHEN** a snapshot is inspected
- **THEN** it holds a `chrono::Timestamp` stamp in the `monotonic_raw` domain and a signed `std::int64_t` UTC offset whose magnitude is bounded by the realtime-monotonic relationship

#### Scenario: boottime is not recorded

- **WHEN** a snapshot is serialized
- **THEN** its content names only the monotonic-raw stamp and the UTC offset, with no `boottime` field

### Requirement: Provider control plane

The provider SHALL expose `snapshot_now() noexcept -> core::Status`, `flush() noexcept -> core::Status`, and `startup_status() const noexcept -> core::Status`. Failures from sampling or the bound sink SHALL propagate through these returns, and the provider SHALL remain usable after a failed snapshot.

#### Scenario: Flush failure is propagated

- **WHEN** the bound sink reports an I/O failure from `flush()`
- **THEN** `ChronoProvider::flush()` returns that failure as a `core::Status` error arm

#### Scenario: Snapshot errors do not crash

- **WHEN** the sink rejects a snapshot or a clock read fails
- **THEN** `snapshot_now()` reports the failure through its `core::Status` and the provider remains usable

#### Scenario: Startup status reports success by default

- **WHEN** the provider starts with a zero core mask and a healthy sink
- **THEN** `startup_status()` returns success

### Requirement: Provider shutdown

Destruction SHALL stop and join the worker if it was started, deliver one final snapshot of pending state, and flush the bound sink before releasing storage, without allocating or throwing on the shutdown path. Destruction of a provider whose worker was never started SHALL be safe.

#### Scenario: Clean shutdown drains the sink

- **WHEN** a started provider is destroyed
- **THEN** the worker stops, a final snapshot is delivered, `flush()` reaches the sink, and no sanitizer reports a race or leak

#### Scenario: Unstarted provider destroys cleanly

- **WHEN** a provider whose worker was never started is destroyed
- **THEN** destruction completes without error and the bound sink is still flushed

### Requirement: Deferred worker start

`ChronoProvider` SHALL provide `start() noexcept -> core::Status` that starts the background snapshot worker when `collect_interval` is non-zero. A zero interval SHALL leave `start()` a no-op that returns success. `start()` SHALL be idempotent.

#### Scenario: start() begins background delivery

- **WHEN** a provider with a non-zero interval calls `start()`
- **THEN** the background worker begins delivering snapshots to the sink

#### Scenario: start() is idempotent

- **WHEN** `start()` is called twice on one provider
- **THEN** the second call returns success without starting a second worker

#### Scenario: start() with a zero interval does nothing

- **WHEN** a provider with `collect_interval == 0` calls `start()`
- **THEN** the call returns success and no background thread is started

### Requirement: Optional worker logger

`ChronoProvider` SHALL support an optional logger binding for its background worker: a caller MAY bind a logger before `start()` so the worker logs its diagnostics (startup, affinity result, snapshot delivery failures, shutdown) through it. The worker SHALL start and run normally without a bound logger.

#### Scenario: Worker logs through the bound logger

- **WHEN** a provider is started with a bound logger and then shut down
- **THEN** the bound logger receives the worker's startup and shutdown diagnostics

#### Scenario: Worker runs without a logger

- **WHEN** a provider is started without a logger
- **THEN** snapshots still flow and no logging call occurs

#### Scenario: Clock log level serializes

- **WHEN** each `Clock_log_level` value passes through `to_string`/`from_string`, `to_json`/`from_json`, and `std::format`
- **THEN** the value round-trips and the formatter renders the canonical name

### Requirement: Provider configuration serialization

`ChronoProvider_config` SHALL implement `to_string() const` plus a free `to_string(const T&)`, non-throwing `to_json`/`from_json` for its complete visible state under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Configuration JSON round-trip

- **WHEN** a `ChronoProvider_config` with an interval and a core mask is serialized and parsed back
- **THEN** the JSON carries the interval in nanoseconds and the mask, and the parsed configuration compares equal to the original

#### Scenario: Configuration renders and formats

- **WHEN** `to_string()`, the free `to_string`, and `std::format("{}", config)` are applied
- **THEN** the member and free results are equal non-empty strings and the formatter renders the same text

### Requirement: Provider diagnostics

`ChronoProvider` SHALL provide a cold-path `to_string() const` (member plus free helper) and a `std::formatter` specialization reporting its configuration and startup state, for post-mortem diagnosis of a mis-pinned or failed worker.

#### Scenario: Provider renders configuration and state

- **WHEN** `to_string()` and `std::format("{}", provider)` are evaluated on a started provider
- **THEN** the member text names the interval and core mask and reflects the startup status, and the formatter renders the same text
