# synchro/consumer Specification

## Purpose

Defines a synchronization-event consumer that verifies schedule continuity,
measures delivery latency, dispatches phase handlers, and reports
synchronization loss inside a bounded wait loop.

## Requirements

### Requirement: Consumer endpoint and set attachment

`SyncConsumer<Logger>` SHALL implement `IEventConsumer`, own one
`EventConsumer<SyncPayload>`, and expose its listener, drain, attachment state,
and received count by delegation. It SHALL NOT create its own waiting thread.

#### Scenario: Consumer attaches to a set

- **WHEN** a `SyncConsumer` is attached to an `EventConsumerSet`
- **THEN** the set waits on the consumer's listener and drains the consumer on a fired event

#### Scenario: Consumer drains without a set

- **WHEN** a `SyncConsumer` is not attached and `drain()` is called
- **THEN** it drains all queued synchronization events through its handlers

#### Scenario: Consumer mode is exclusive

- **WHEN** a `SyncConsumer` is attached to a set
- **THEN** its own blocking wait is unavailable and the set owns waiting

### Requirement: Logical phase continuity

The consumer SHALL compute `absolute_phase = cycle_number * phases_total +
phase_index` and compare it with the last accepted phase. A forward jump SHALL
be classified as a generator skip; a repeated or backward position SHALL be a
resynchronization, not a false loss.

#### Scenario: Forward jump counts skipped phases

- **WHEN** the next accepted absolute phase is greater than expected
- **THEN** the consumer counts the missing phases as generator skips

#### Scenario: Epoch change resynchronizes

- **WHEN** the producer epoch changes
- **THEN** the consumer resets logical tracking and reports a producer restart

#### Scenario: Phase count change resynchronizes

- **WHEN** `phases_total` differs from the configured value
- **THEN** the consumer resynchronizes without counting a generator skip

#### Scenario: Backward cycle resynchronizes

- **WHEN** the cycle number moves backwards without an epoch change
- **THEN** the consumer resynchronizes with a warning instead of reporting a huge loss

#### Scenario: Duplicate is not a generator skip

- **WHEN** a duplicate transport sample is dropped
- **THEN** the logical position and generator-skip counters are unchanged

### Requirement: Transport loss reconciliation

The consumer SHALL distinguish transport loss from generator skips. After a
transport gap, the expected logical position SHALL advance by the reported
lost count plus one; any remaining forward difference SHALL be attributed to
generator skips.

#### Scenario: Pure transport gap

- **WHEN** the transport reports N lost samples and the next logical phase is N positions ahead
- **THEN** no generator skip is counted

#### Scenario: Transport gap plus generator overrun

- **WHEN** the transport reports N lost samples and the next logical phase is more than N positions ahead
- **THEN** only the excess positions are counted as generator skips

#### Scenario: Loss counters are exposed

- **WHEN** a run mixes transport loss, duplicate drops, and generator skips
- **THEN** `SyncConsumerStats` reports each cause independently

### Requirement: Late-event classification

For each delivered event the consumer SHALL compute IPC latency from the header
timestamp, end-to-end latency from the payload scheduled time, and a signed
`is_late` flag against the configured or payload-derived maximum latency.

#### Scenario: IPC latency is measured

- **WHEN** an event is received
- **THEN** `ipc_latency_ns` is the receive time minus the header publication time

#### Scenario: End-to-end latency is measured

- **WHEN** an event is received
- **THEN** `e2e_latency_ns` is the receive time minus the payload scheduled time

#### Scenario: Late event is detected

- **WHEN** end-to-end latency exceeds the maximum allowed latency
- **THEN** the event is marked `is_late = true`

#### Scenario: Late policy is applied

- **WHEN** a late event arrives
- **THEN** `DropAndWarn` suppresses delivery and increments the dropped-late counter, while `DeliverWithFlag` delivers it with the flag set

### Requirement: Phase handler dispatch

The consumer SHALL support handlers for every phase, a specific phase index,
and the start of a cycle. `on_cycle_start(handler)` SHALL be equivalent to
`on_phase(0, handler)`. Handler registration SHALL occur during configuration
and SHALL not allocate during event dispatch.

#### Scenario: Any-phase handler runs for every phase

- **WHEN** a phase is accepted
- **THEN** the any-phase handler receives its `SyncContext`

#### Scenario: Phase-specific handler runs only for its phase

- **WHEN** phase 4 is accepted and a handler is registered for phase 4
- **THEN** that handler runs and handlers for other phases do not

#### Scenario: Cycle-start handler aliases phase zero

- **WHEN** a cycle-start handler is registered and phase zero is accepted
- **THEN** it runs through the phase-zero slot

#### Scenario: Dispatch order is deterministic

- **WHEN** both any-phase and phase-specific handlers are registered
- **THEN** the any-phase handler runs first and the specific handler follows

#### Scenario: Handler dispatch is allocation-free

- **WHEN** an armed allocation counter observes accepted events after registration
- **THEN** zero heap allocations occur in the consumer dispatch path

### Requirement: Synchronization watchdog

The consumer SHALL track the last accepted event time and expose a watchdog
deadline derived from `sync_loss_timeout_ns`. It SHALL enter `Unsynced` and log
an error when no synchronization event arrives before the deadline.

#### Scenario: Locked state follows the first event

- **WHEN** the first valid event is accepted
- **THEN** the consumer changes from `Unsynced` to `Locked`

#### Scenario: Silence causes loss

- **WHEN** no event arrives within `sync_loss_timeout_ns`
- **THEN** the consumer enters `Unsynced`, increments the loss counter, and logs an error

#### Scenario: Reacquisition is logged

- **WHEN** an event arrives after synchronization loss
- **THEN** the consumer returns to `Locked` and logs reacquisition

#### Scenario: Initial timeout is enforced

- **WHEN** no first event arrives within `initial_sync_timeout_ns`
- **THEN** the watchdog reports synchronization loss without waiting for a first sample

### Requirement: SyncRunner loop

`SyncRunner<Logger>` SHALL own one `SyncConsumer` and one
`EventConsumerSet<16>`, attach the consumer, and provide a bounded wait loop.
The loop SHALL drain events, check the watchdog on timeout, and support
`wake_up()`-driven shutdown.

#### Scenario: Runner drains a fired consumer

- **WHEN** the attached consumer's listener fires
- **THEN** the runner drains the consumer and continues with the next wait

#### Scenario: Runner checks the watchdog

- **WHEN** `wait_for` returns `Timeout`
- **THEN** the runner checks the consumer's watchdog deadline and reports loss when due

#### Scenario: Stop wakes the runner

- **WHEN** the application requests stop from another thread
- **THEN** the runner wakes from its wait and exits without waiting for the watchdog timeout

#### Scenario: Runner owns one channel

- **WHEN** one runner is created
- **THEN** it manages exactly one synchronization channel with one consumer

### Requirement: Consumer observability

The consumer SHALL emit `sync_consumer_received_total`,
`sync_consumer_dropped_late_total`, `sync_consumer_transport_lost_total`,
`sync_consumer_generator_skips_total`, `sync_consumer_ipc_latency_ns`,
`sync_consumer_e2e_latency_ns`, and `sync_consumer_locked` through its meter.
It SHALL log initialization, first lock, reacquisition, transport gaps,
generator skips, late drops, and synchronization loss.

#### Scenario: Counters track mixed anomalies

- **WHEN** a run receives valid, late, transport-lost, and skipped phases
- **THEN** each consumer counter reports its own cause

#### Scenario: Lock gauge follows state

- **WHEN** the consumer is `Locked` or `Unsynced`
- **THEN** `sync_consumer_locked` reports `1` or `0`

#### Scenario: Metrics are harvester-safe

- **WHEN** the metric provider harvests while the consumer dispatches events
- **THEN** instruments use the multi-threaded policy and the run is TSan-clean

#### Scenario: Diagnostics render

- **WHEN** `to_string()` and `std::format("{}")` are evaluated for a `SyncConsumer` or `SyncContext`
- **THEN** both render deterministic cold-path state

#### Scenario: Context serializes

- **WHEN** a `SyncContext` is rendered and round-tripped through JSON
- **THEN** the formatter matches `to_string()`, every schedule and latency field keeps its value, and hostile JSON leaves the destination unchanged

### Requirement: Consumer limits and multi-channel operation

A single `EventConsumerSet` SHALL support at most 16 consumers. Systems with
more synchronization consumers SHALL use multiple channels or multiple runner
instances; the implementation SHALL NOT promise more than 16 per set.

#### Scenario: Seventeenth consumer is rejected

- **WHEN** a seventeenth consumer is attached to an `EventConsumerSet<16>`
- **THEN** attach fails with `OutOfSamples`

#### Scenario: Multiple channels use separate runners

- **WHEN** an application needs more than one synchronization channel
- **THEN** it creates one runner per channel rather than exceeding the set limit
