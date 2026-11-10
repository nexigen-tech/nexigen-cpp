# phm/supervision Specification

## Purpose

Defines the PHM daemon's supervision engine: Alive, Logical, and Deadline supervision over a single WaitSet-driven loop, aggregation of per-process results into a supervision status machine, core isolation, and babbling-idiot protection.

## Requirements

### Requirement: Alive supervision

The daemon SHALL verify that each supervised process emits its expected checkpoints within a configured window (`alive_reference_cycle` with `min_margin`/`max_margin`); when the expected count is not received in the window, the daemon SHALL record an aliveness failure.

#### Scenario: Missing checkpoints fail aliveness

- **WHEN** a process stops emitting checkpoints and the alive window plus margins elapses
- **THEN** the daemon records an aliveness failure for that process

### Requirement: Logical supervision

The daemon SHALL validate checkpoint ordering against a directed graph of allowed transitions (`from_id -> to_id`); a checkpoint that violates the graph SHALL be reported as a logical failure immediately.

#### Scenario: Out-of-order checkpoint fails logic

- **WHEN** a process emits a checkpoint that is not a valid successor of the previous one
- **THEN** the daemon reports a logical failure without waiting for the next cycle

### Requirement: Deadline supervision

The daemon SHALL measure the elapsed time between a `source_checkpoint` and its `target_checkpoint`; an elapsed time above `deadline_max_duration` or below `deadline_min_duration` SHALL be reported as a determinism violation.

#### Scenario: Late deadline is reported

- **WHEN** a target checkpoint arrives after `deadline_max_duration`
- **THEN** the daemon reports a deadline violation

#### Scenario: Early deadline is reported

- **WHEN** a target checkpoint arrives faster than `deadline_min_duration`
- **THEN** the daemon reports a deadline violation

### Requirement: Status aggregation

The daemon SHALL aggregate Alive, Logical, and Deadline results per process into a `SupervisionStatus` machine with the states `Ok`, `Failed`, `Expired`, and `Stopped`; a single failure within `failed_supervision_cycles_tolerance` SHALL yield `Failed`, and consecutive failures beyond the tolerance SHALL yield `Expired`.

#### Scenario: Tolerance defers expiration

- **WHEN** a process fails one cycle within the tolerance
- **THEN** its status becomes `Failed`, not `Expired`

#### Scenario: Repeated failures expire the process

- **WHEN** a process fails more cycles in a row than the tolerance allows
- **THEN** its status becomes `Expired`, triggering recovery

### Requirement: WaitSet-driven monitoring loop

The daemon's main monitoring thread SHALL block on `WaitSet::wait_and_process`, multiplexing every monitoring channel; it SHALL wake only on an incoming checkpoint/notification or on an attached deadline/interval timer, and SHALL consume 0% CPU while idle.

#### Scenario: Daemon idles without polling

- **WHEN** no checkpoints arrive and only deadline/interval timers are attached
- **THEN** the monitoring thread stays asleep in the kernel and wakes only on a timer or an event

### Requirement: Core isolation

The daemon SHALL pin itself to the configured infrastructure CPU core and run under `SCHED_FIFO` at the highest configured priority so no other process competes for its execution.

#### Scenario: Daemon pins to the service core

- **WHEN** the daemon starts with a configured core mask
- **THEN** its monitoring thread's affinity and scheduler class match the configuration

### Requirement: Babbling idiot protection

The daemon SHALL process each supervision cycle in `O(1)` regardless of how fast a faulty process floods checkpoints, relying on event aggregation and time margins (`min_margin`/`deadline_min_duration`) so a spinning process cannot starve the daemon.

#### Scenario: A flood does not starve the daemon

- **WHEN** a faulty process emits checkpoints far faster than the configured margins allow
- **THEN** the daemon still handles the supervision cycle in constant time

### Requirement: Supervision diagnostics

The supervision engine SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting per-process supervision status.

#### Scenario: Supervision renders status

- **WHEN** `to_string()` and `std::format("{}", supervision)` are evaluated after a process changes status
- **THEN** the text names the process and its current status, and the formatter output equals the member output
