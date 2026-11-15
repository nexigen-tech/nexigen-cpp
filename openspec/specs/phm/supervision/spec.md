# phm/supervision Specification

## Purpose

Defines the PHM daemon's supervision engine: Alive, Logical, and Deadline supervision over a single WaitSet-driven loop, aggregation of per-process results into a supervision status machine, core isolation, and babbling-idiot protection.

## Requirements

### Requirement: Alive supervision

The daemon SHALL verify that each supervised process emits the configured expected number of checkpoints per alive reference cycle, and SHALL record an aliveness failure when the count falls outside the inclusive `[expected - min_margin, expected + max_margin]` window. A manifest entry without the count fields SHALL keep the legacy at-least-one-checkpoint-per-window behaviour.

#### Scenario: Missing checkpoints fail aliveness

- **WHEN** a process stops emitting checkpoints and the alive window plus margins elapses
- **THEN** the daemon records an aliveness failure for that process

#### Scenario: Too many checkpoints fail aliveness

- **WHEN** a process emits more checkpoints in one reference cycle than `expected + max_margin`
- **THEN** the daemon records an aliveness failure for that cycle

#### Scenario: Legitimate faster execution passes

- **WHEN** a process emits `expected + max_margin` checkpoints in one reference cycle
- **THEN** the daemon records no aliveness failure for that cycle

#### Scenario: Legacy manifest keeps at-least-one semantics

- **WHEN** a manifest entry omits the expected-count fields
- **THEN** one checkpoint per window still passes and zero checkpoints still fail

### Requirement: Logical supervision

The daemon SHALL validate checkpoint ordering against a directed graph of allowed transitions (`from_id -> to_id`), and SHALL enforce declared fork/join regions: after a fork, every declared branch entry MUST be reached before the join. A checkpoint that violates an edge, or a join reached with an unentered branch, SHALL be reported as a logical failure immediately.

#### Scenario: Out-of-order checkpoint fails logic

- **WHEN** a process emits a checkpoint that is not a valid successor of the previous one
- **THEN** the daemon reports a logical failure without waiting for the next cycle

#### Scenario: Join without every branch fails logic

- **WHEN** a process reaches a declared join while one declared branch entry was never reached
- **THEN** the daemon reports a logical failure immediately

#### Scenario: All branches entered before the join pass

- **WHEN** a process reaches every declared branch entry before the join
- **THEN** the daemon records no logical failure and clears the branch counters

#### Scenario: Process without fork declarations is unaffected

- **WHEN** a manifest entry declares no fork/join regions
- **THEN** logical supervision validates pairwise edges exactly as before

### Requirement: Deadline supervision

The daemon SHALL measure the elapsed time between a `source_checkpoint` and its `target_checkpoint`; an elapsed time above `deadline_max_duration` or below `deadline_min_duration` SHALL be reported as a determinism violation.

#### Scenario: Late deadline is reported

- **WHEN** a target checkpoint arrives after `deadline_max_duration`
- **THEN** the daemon reports a deadline violation

#### Scenario: Early deadline is reported

- **WHEN** a target checkpoint arrives faster than `deadline_min_duration`
- **THEN** the daemon reports a deadline violation

### Requirement: Status aggregation

The daemon SHALL aggregate Alive, Logical, and Deadline results per process into a `SupervisionStatus` machine with the states `Ok`, `Failed`, `Expired`, and `Exhausted`; a single failure within `failed_supervision_cycles_tolerance` SHALL yield `Failed`, and consecutive failures beyond the tolerance SHALL yield `Expired`. `Exhausted` SHALL mean that recovery budget is spent, not that the process was stopped deliberately.

#### Scenario: Tolerance defers expiration

- **WHEN** a process fails one cycle within the tolerance
- **THEN** its status becomes `Failed`, not `Expired`

#### Scenario: Repeated failures expire the process

- **WHEN** a process fails more cycles in a row than the tolerance allows
- **THEN** its status becomes `Expired`, triggering recovery

#### Scenario: Deliberate stop is not exhaustion

- **WHEN** the Execution Manager reports that a process is terminating or terminated and its recovery budget is intact
- **THEN** the process is suspended rather than reported `Exhausted`
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

### Requirement: Lifecycle-armed supervision

The PHM supervisor SHALL accept EM execution-state notifications for each supervised process. On `Running` it SHALL arm Alive/Logical/Deadline supervision and reset the failure counters; on `Terminating` and `Terminated` it SHALL suspend supervision for that process. A `Failed` notification SHALL place the process in `Exhausted` and SHALL NOT restart it again. A suspended process SHALL NOT become `Expired`, SHALL keep its last non-failure status, and SHALL NOT trigger a recovery action. A process that never receives a lifecycle notification SHALL keep the pre-change supervision behavior.

#### Scenario: Running arms supervision

- **WHEN** PHM receives `Running` for a supervised process
- **THEN** Alive and Deadline supervision start for it and the failure counters reset

#### Scenario: Terminating suspends supervision

- **WHEN** PHM receives `Terminating` while the process is within its Alive window
- **THEN** the process is not reported `Expired` and no recovery action is applied

#### Scenario: Terminated does not trigger recovery

- **WHEN** PHM receives `Terminated` for a process and its Alive window later elapses
- **THEN** the supervisor keeps the process suspended and the recovery handler is not invoked

#### Scenario: Failed exhaustion is terminal

- **WHEN** PHM receives `Failed` for a process whose restart budget is spent
- **THEN** the process becomes `Exhausted`, no further restart is requested, and the escalation path runs once

#### Scenario: Legacy PHM keeps working

- **WHEN** no lifecycle event is ever received for a supervised process
- **THEN** Alive, Logical, and Deadline supervision behave exactly as before
### Requirement: Lifecycle channel diagnostics

The PHM daemon SHALL expose the lifecycle channel state through its existing cold-path diagnostics, naming the standard service and the number of lifecycle notifications applied.

#### Scenario: Lifecycle diagnostics are readable

- **WHEN** the daemon renders its diagnostics after applying lifecycle events
- **THEN** the text names `Nexigen/PHM/ExecutionState` and the applied notification count

### Requirement: Exhaustion escalates instead of restarting

An `Exhausted` process SHALL NOT trigger another restart attempt. The daemon SHALL escalate it once to the State Manager as a platform fault and SHALL keep the status `Exhausted` until the process reports `Running` again.

#### Scenario: Exhausted process is not restarted again

- **WHEN** a process is `Exhausted` and its Alive window elapses again
- **THEN** the daemon sends no restart request and repeats no escalation

#### Scenario: Recovery returns the process to Ok

- **WHEN** an `Exhausted` process later reports `Running`
- **THEN** its status becomes `Ok` and supervision is armed again

### Requirement: Supervision status vocabulary

`nexigen::phm::SupervisionStatus` SHALL expose exactly the values `Ok`, `Failed`, `Expired`, and `Exhausted`, each with a canonical name, and the full project conversion set.

#### Scenario: Renamed value round-trips

- **WHEN** each `SupervisionStatus` value passes through `to_string`, `from_string`, `to_json`, and `from_json`
- **THEN** every canonical name round-trips, `Exhausted` is reported as `Exhausted`, and no `Stopped` name is produced
