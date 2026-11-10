# Spec Delta

## Purpose

Defines the SM's process lifecycle and resource envelope: managed startup under the EM with a readiness report, self-health checkpoints to PHM, memory locking, the WaitSet event loop, the transition watchdog, and thread affinity plus SCHED_FIFO priorities.

## ADDED Requirements

### Requirement: Managed startup

The SM SHALL run as a foreground process launched and supervised by the EM (never a standalone daemon), receive its configuration through command-line arguments and environment variables, and report `ReportApplicationState(kRunning)` within `startup_timeout` after parsing the graph, opening the iceoryx2 channels, and pinning its threads.

#### Scenario: SM reports readiness

- **WHEN** the SM finishes initialization
- **THEN** it reports `kRunning` to the EM within the startup timeout

### Requirement: Self-health supervision

The SM SHALL emit aliveness checkpoints to PHM through the event pattern on every arbitration cycle, so PHM can order an emergency restart of the SM with the Function Group states preserved in shared memory.

#### Scenario: SM checkpoints its liveness

- **WHEN** the arbitration thread completes a cycle
- **THEN** a checkpoint reaches the PHM daemon's listener

### Requirement: Memory locking

The SM SHALL call `mlockall(MCL_CURRENT | MCL_FUTURE)` at initialization to pin its pages in RAM, and SHALL perform no heap allocation in the arbitration loop after initialization.

#### Scenario: Pages are locked, loop is allocation-free

- **WHEN** initialization completes
- **THEN** the SM's pages are locked in RAM and the arbitration cycle allocates nothing

### Requirement: WaitSet event loop

The SM's arbitration thread SHALL block on `WaitSet::wait_and_process`, multiplexing the PHM, EM, and TriggerIn channels; its idle CPU utilization SHALL be 0%.

#### Scenario: SM idles without polling

- **WHEN** no triggers arrive
- **THEN** the arbitration thread stays asleep in the kernel and wakes only on an event or timer

### Requirement: Transition watchdog

Every transition SHALL be bounded by `transition_max_time`; when the EM does not report the completed switch in time, the SM SHALL record a transition error and force the group into `SafeState` or `Off`.

#### Scenario: Stalled transition is forced

- **WHEN** the EM does not acknowledge a transition within `transition_max_time`
- **THEN** the SM records the timeout and forces the group into `SafeState` or `Off`

### Requirement: Thread affinity

The arbitration thread SHALL pin itself to the configured real-time core (environment mask) and the infrastructure logging/metrics threads SHALL pin to the background core, so application computation cannot delay a `SafeState` switch.

#### Scenario: Threads pin to their cores

- **WHEN** the SM starts with the RT and background masks configured
- **THEN** the arbitration thread's affinity equals the RT mask and the flusher threads' affinity equals the background mask

### Requirement: Scheduler priorities

The arbitration thread SHALL run under `SCHED_FIFO` at a priority below the PHM daemon and above application RT tasks; the infrastructure flusher threads SHALL run under `SCHED_OTHER` at priority 0.

#### Scenario: Arbitration thread runs FIFO

- **WHEN** the SM starts with the configured priority
- **THEN** the arbitration thread's scheduler is SCHED_FIFO with that priority and the flusher threads run SCHED_OTHER priority 0
