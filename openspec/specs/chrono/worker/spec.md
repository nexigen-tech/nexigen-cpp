# chrono/worker Specification

## Purpose

Defines the periodic snapshot daemon wrapper: a `ChronoWorker` that owns the background thread and the daemon-only machinery (thread attributes, logger binding, fixed-rate delivery loop, shutdown draining) around a bound `Provider`, so the provider type itself stays threadless for both daemon and manual modes.

## Requirements

### Requirement: Periodic snapshot delivery and collection period

A `ChronoWorker` bound to a `Provider` SHALL deliver one snapshot per configured `collect_interval` to the provider's bound sink when started, one full snapshot per period with no partial writes. A zero interval SHALL disable the background thread and leave explicit `snapshot_now()` calls the sole snapshot trigger.

#### Scenario: Configured period is honored

- **WHEN** the worker is configured with a small interval, started, and observed for several periods
- **THEN** the sink receives multiple snapshots whose spacing is consistent with the interval within a documented scheduling tolerance

#### Scenario: Zero interval disables background delivery

- **WHEN** the provider config has a zero `collect_interval` and the worker is started
- **THEN** no snapshot is delivered until `snapshot_now()` is called explicitly and no thread is started

#### Scenario: Manual snapshot is deterministic during daemon operation

- **WHEN** `snapshot_now()` is called on the bound provider while the worker runs
- **THEN** exactly one additional complete snapshot is delivered on the calling thread before the call returns

### Requirement: Deferred worker start

`ChronoWorker` SHALL provide `start() noexcept -> core::Status` that launches the background snapshot thread when the configured interval is non-zero, and `start() SHALL be idempotent`. A zero interval SHALL leave `start()` a no-op that returns success and starts nothing.

#### Scenario: start() begins periodic delivery

- **WHEN** a worker with a non-zero interval is started
- **THEN** snapshots flow to the sink at the configured period

#### Scenario: start() is idempotent

- **WHEN** `start()` is called twice on one worker
- **THEN** the second call returns success without starting a second thread

#### Scenario: start() with a zero interval does nothing

- **WHEN** the interval is zero and `start()` is called
- **THEN** the call returns success and no background thread is started

### Requirement: Worker thread attributes

The started worker SHALL apply its `core::ThreadSpec` to itself before its first snapshot, supporting CPUs above 63, thread naming, and Other/FIFO/RR scheduling. An empty CPU set SHALL leave affinity unchanged. Best-effort attribute failures SHALL remain visible without stopping delivery; a `Required` failure SHALL make `start()` fail, stop the loop before any snapshot, join the thread, and leave destruction safe. A zero interval SHALL apply no thread attributes at all.

#### Scenario: Worker attributes are applied before the first snapshot

- **WHEN** the worker starts with valid name, affinity, and scheduler attributes
- **THEN** the attributes are applied before the first snapshot is delivered

#### Scenario: Best-effort failure is visible without stopping delivery

- **WHEN** an affinity or scheduler request fails under best-effort mode
- **THEN** `startup_status()` and the thread report expose the failure, and snapshots keep flowing

#### Scenario: Required failure prevents snapshot delivery

- **WHEN** an attribute request fails under `Required`
- **THEN** `start()` returns `SystemError`, no snapshot loop runs, the thread is joined, and worker destruction is safe

#### Scenario: CPU above 63 is supported

- **WHEN** the worker spec contains a permitted CPU above 63
- **THEN** the worker uses it without truncating to a uint64 mask

#### Scenario: Empty CPU set leaves affinity alone

- **WHEN** the worker CPU set is empty
- **THEN** the worker inherits process affinity and no pinning call is required to succeed

#### Scenario: Zero interval applies no attributes

- **WHEN** the interval is zero and `start()` is called
- **THEN** no thread is created and no thread-attribute operation occurs

### Requirement: Worker state reporting

The worker SHALL expose `startup_status() const noexcept -> core::Status`, the applied `core::ThreadApplyReport`, and the post-pin affinity mask as observed by the worker (zero before start), all readable without locks from the dead-path diagnostics callers.

#### Scenario: Applied attributes are readable after start

- **WHEN** the worker starts with a valid thread spec and the caller reads the report and mask
- **THEN** the report reflects the applied attributes and the mask is the worker-observed post-pin affinity

#### Scenario: State reads are race-free

- **WHEN** the report and mask are read while the worker runs (verified under the thread sanitizer)
- **THEN** no data race is reported and the values are either pre-start zeros or post-application states

### Requirement: Worker logger binding

`ChronoWorker` SHALL support an optional logger binding for the daemon thread: a caller MAY bind a logger before `start()`, and the worker SHALL log its diagnostics (startup, attribute result, delivery failures, shutdown) through a `log(core::LogLevel, std::string_view)` call shape. The worker SHALL start and run normally without a bound logger.

#### Scenario: Worker logs through the bound logger

- **WHEN** a worker is started with a bound logger and shut down
- **THEN** the logger receives the worker's startup and shutdown diagnostics

#### Scenario: Worker runs without a logger

- **WHEN** a worker is started without a logger
- **THEN** snapshots still flow and no logging call occurs

### Requirement: Worker logger rebinding contract

A start with a logger on a running worker SHALL be rejected with `core::ErrorCode::Conflict` without touching the binding; rebinding SHALL be possible only while the worker is not running.

#### Scenario: Double start with a logger is rejected without rebinding

- **WHEN** a started worker receives a second `start(logger)` while running
- **THEN** the call returns `Conflict`, the second logger handle is not installed, and no race is possible on the binding (thread-sanitizer clean)

#### Scenario: Rebinding is only possible on a non-running worker

- **WHEN** `start(logger)` is called on a non-running worker
- **THEN** the binding is applied fresh, and `start()` on an already-started worker remains idempotent without a second binding

### Requirement: Worker shutdown

Destruction SHALL stop the running worker thread, deliver one final snapshot through the bound provider, and flush the bound sink before releasing storage, without allocating or throwing on the shutdown path. Destruction of a worker that was never started SHALL be safe.

#### Scenario: Clean shutdown drains the sink

- **WHEN** a started worker is destroyed
- **THEN** the thread stops and joins, a final snapshot is delivered through the provider, `flush()` reaches the sink, and no sanitizer reports a race or leak

#### Scenario: Unstarted worker destroys cleanly

- **WHEN** a worker that was never started is destroyed
- **THEN** destruction completes without error and the bound sink is still flushed

### Requirement: Worker diagnostics

`ChronoWorker` SHALL provide a cold-path `to_string() const` member plus free helper and a `std::formatter` specialization reporting the bound configuration and startup state, for post-mortem diagnosis of a mis-pinned, failed, or stopped daemon thread.

#### Scenario: Worker renders configuration and state

- **WHEN** `to_string()` and `std::format("{}", worker)` are evaluated on a started worker
- **THEN** the member text names the interval, the worker thread spec, and the startup state, and the formatter renders the same text
