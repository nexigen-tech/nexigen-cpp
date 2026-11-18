# synchro/generator Specification

## Purpose

Defines a real-time periodic generator that publishes phase-aligned
synchronization events on a `com` event channel without cumulative drift.

## Requirements

### Requirement: Generator lifecycle and ownership

`SyncGenerator<Logger>` SHALL be created from a node, `SyncConfig`, logger, and
a dedicated `metrics::Meter`. Creation SHALL validate the configuration and
metric names before opening any transport port. The generator SHALL own one
`EventProducer<SyncPayload>` and one real-time worker, and SHALL be
non-copyable and non-movable.

#### Scenario: Valid generator is created

- **WHEN** `SyncGenerator::create` receives a valid node, configuration, logger, and meter
- **THEN** it creates the producer, registers its metrics, and returns a generator in `Stopped`

#### Scenario: Invalid configuration is rejected before transport

- **WHEN** the configuration is invalid
- **THEN** creation returns `SyncError` with kind `InvalidConfig` and opens no transport endpoint

#### Scenario: Metric registration failure is reported

- **WHEN** a required metric name is duplicate or the metric pool is exhausted
- **THEN** creation returns `SyncError` with kind `Metrics` and no transport endpoint remains open

#### Scenario: Generator is non-copyable and non-movable

- **WHEN** a caller attempts to copy or move a `SyncGenerator`
- **THEN** the operation is rejected at compile time

### Requirement: Start and stop transitions

`start()` SHALL resolve `StartPolicy` and begin generation at the resulting
raw-time instant. `start_at(target)` SHALL override the target for that start
only, using the configured alignment and past-target policy. `stop()` SHALL
interrupt a wait, join the worker, and leave the generator in `Stopped`.

#### Scenario: Start waits for the aligned instant

- **WHEN** `start()` resolves a start instant in the future
- **THEN** the generator reports `WaitingForStart` until that instant and then `Running`

#### Scenario: Runtime target overrides configuration

- **WHEN** `start_at()` is called with a future target
- **THEN** that target is used for this start without changing the stored `SyncConfig`

#### Scenario: Stop interrupts an active wait

- **WHEN** `stop()` is called while the generator is waiting for a phase or start instant
- **THEN** the wait is interrupted promptly and the worker is joined

#### Scenario: Start while running is rejected

- **WHEN** `start()` or `start_at()` is called while the generator is `Running` or `WaitingForStart`
- **THEN** the call returns `SyncError` with kind `Lifecycle` and the existing run is unchanged

#### Scenario: Faulted generator can be restarted

- **WHEN** the generator is `Faulted` and `start()` is called
- **THEN** it starts a new lifecycle with a new producer epoch and cycle zero

### Requirement: Hybrid absolute wait

The generator SHALL wait for the next raw-time deadline using `ppoll` on an
`eventfd` with a relative timeout derived from `CLOCK_MONOTONIC_RAW`. When the
remaining time is at most `spin_threshold_ns`, it SHALL spin until the deadline.
`EINTR` SHALL cause a recomputation, not a failure.

#### Scenario: Coarse wait sleeps before the spin window

- **WHEN** the remaining time exceeds the configured spin threshold
- **THEN** the generator blocks in `ppoll` and consumes no busy-poll CPU

#### Scenario: Final approach spins to the deadline

- **WHEN** the remaining time is at most the spin threshold
- **THEN** the generator spins on the raw clock until the scheduled instant

#### Scenario: Stop wakes the pending wait

- **WHEN** stop is requested from another thread
- **THEN** the eventfd wakes `ppoll` without waiting for the full timeout

#### Scenario: Signal interruption does not break the schedule

- **WHEN** `ppoll` returns `EINTR`
- **THEN** the generator recomputes the remaining time from the same absolute deadline and continues

### Requirement: Phase publication

At each phase deadline the generator SHALL sample the actual raw time, build a
`SyncPayload`, and publish it through the channel's `EventProducer`. The header
timestamp SHALL be the actual publication time, and the payload scheduled time
SHALL be the original grid deadline.

#### Scenario: Published phase carries both times

- **WHEN** a phase is published
- **THEN** the payload carries its scheduled time and the header carries the actual publication timestamp

#### Scenario: Jitter is recorded

- **WHEN** the actual publication time differs from the scheduled time
- **THEN** the signed difference is recorded in the jitter histogram

#### Scenario: Notification failure is distinguished

- **WHEN** the sample is sent but the wake notification fails
- **THEN** the phase counts as published and the outcome is `NotifyFailed`

#### Scenario: Send failure does not advance the phase

- **WHEN** the transport rejects the sample
- **THEN** the error is logged and the phase is not counted as successfully published

### Requirement: Overrun handling

If the actual time reaches the next phase deadline, the generator SHALL NOT
publish a burst of stale phases. It SHALL skip every stale phase slot, count
each skipped slot, log one warning per overrun episode, and continue with the
next phase whose deadline is still in the future.

#### Scenario: Stale phases are skipped

- **WHEN** the generator wakes after several phase deadlines have passed
- **THEN** it publishes only the next future phase and counts every skipped phase slot

#### Scenario: Overrun does not drift the schedule

- **WHEN** phases are skipped
- **THEN** subsequent deadlines remain anchored to the original `T_start` and cycle grid

#### Scenario: Overrun across a cycle boundary

- **WHEN** the skipped range crosses the end of a cycle
- **THEN** the generator advances the cycle number and phase index on the same absolute grid

#### Scenario: One warning per episode

- **WHEN** multiple phases are skipped in one wake-up
- **THEN** the log contains one warning naming the skipped count rather than one warning per phase

### Requirement: Generator epochs and restart

Every start after creation SHALL call `on_source_reset()` so the event header
carries a new producer epoch and transport sequence starts at zero. The first
payload of a lifecycle SHALL carry cycle zero.

#### Scenario: Restart changes the epoch

- **WHEN** a stopped generator is started again
- **THEN** published events carry a different `producer_epoch`

#### Scenario: Restart resets sequence and cycle

- **WHEN** a stopped generator is started again
- **THEN** its first event has transport sequence zero and payload cycle zero

### Requirement: Generator observability

The generator SHALL emit `sync_cycles_total`, `sync_phases_total`,
`sync_overruns_total`, `sync_jitter_ns`, and `sync_state` through the supplied
meter. It SHALL log initialization, start, stop, overruns, and faults with the
configured logger, without allocating on the steady-state publication path.

#### Scenario: Counters track a run

- **WHEN** a run completes cycles and phases
- **THEN** the cycle and phase counters include every completed cycle and published phase

#### Scenario: Overrun counter counts slots

- **WHEN** one wake-up skips three phase slots
- **THEN** `sync_overruns_total` increases by three

#### Scenario: State gauge follows the lifecycle

- **WHEN** the generator enters `Stopped`, `WaitingForStart`, `Running`, or `Faulted`
- **THEN** `sync_state` reports the matching numeric code

#### Scenario: Metrics are harvester-safe

- **WHEN** the metric provider harvests while the generator publishes
- **THEN** instruments use the multi-threaded policy and the run is TSan-clean

#### Scenario: Steady-state path is allocation-free

- **WHEN** an armed allocation counter observes phase publication after creation
- **THEN** zero heap allocations occur in the generator loop

#### Scenario: Generator diagnostics render

- **WHEN** `to_string()` and `std::format("{}", generator)` are evaluated
- **THEN** both render the generator's cold-path state

### Requirement: Generator operational limits

The generator SHALL enforce a minimum phase period of 1 ms, giving a maximum of
1000 phases per second. It SHALL run its publication loop on the configured
real-time thread and SHALL not require more than one producer thread.

#### Scenario: Rate limit is enforced

- **WHEN** configuration requests a phase period below 1 ms
- **THEN** creation fails with `InvalidConfig`

#### Scenario: One producer thread owns publication

- **WHEN** the generator is running
- **THEN** all transport publications come from its single worker thread
