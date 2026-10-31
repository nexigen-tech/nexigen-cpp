# Spec Delta

## MODIFIED Requirements

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

### Requirement: Provider shutdown

Destruction SHALL stop and join the worker if it was started, deliver one final snapshot of pending state, and flush the bound sink before releasing storage, without allocating or throwing on the shutdown path. Destruction of a provider whose worker was never started SHALL be safe.

#### Scenario: Clean shutdown drains the sink

- **WHEN** a started provider is destroyed
- **THEN** the worker stops, a final snapshot is delivered, `flush()` reaches the sink, and no sanitizer reports a race or leak

#### Scenario: Unstarted provider destroys cleanly

- **WHEN** a provider whose worker was never started is destroyed
- **THEN** destruction completes without error and the bound sink is still flushed

## ADDED Requirements

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
