# exec/function-group-state Specification

## Purpose

Defines how a Function Group State selects its process set and how the Execution Manager performs, bounds, cancels, and reports a state transition, including the failure state that makes a group unusable until a new valid request.

## Requirements

### Requirement: Function Group state selects its process set

A Function Group State SHALL define exactly the processes that run while that state is current. A process SHALL be mapped to states of one Function Group only, and the `Off` state SHALL map no processes. A process configured for a state outside its Function Group SHALL be rejected at manifest validation.

#### Scenario: State selects the processes

- **WHEN** the group `Vehicle` transitions to `Driving`
- **THEN** exactly the processes whose startup configuration references `Vehicle:Driving` are running

#### Scenario: Off maps no processes

- **WHEN** the group `Vehicle` transitions to `Off`
- **THEN** every process of the group is stopped and no process references `Vehicle:Off`

#### Scenario: Cross-group state is rejected

- **WHEN** a process of group `Vehicle` declares a startup configuration for `ADAS:Driving`
- **THEN** manifest validation rejects the configuration

### Requirement: State transition terminates, restarts, and starts processes

On a validated state change the EM SHALL terminate every process of the group that is running and not needed in the requested state, restart every process whose startup configuration differs between the current and requested states, and start every process needed in the requested state that is not already running. A process needed in both states with an identical startup configuration SHALL keep running.

#### Scenario: Transition applies the three steps

- **WHEN** `Vehicle` changes from `Driving` to `Fallback`
- **THEN** processes not needed in `Fallback` are stopped, processes with a changed configuration are restarted, and processes new to `Fallback` are started

#### Scenario: Unchanged process continues

- **WHEN** a process is needed in both `Driving` and `Fallback` with the same arguments, environment, and timeouts
- **THEN** the EM does not restart it during the transition

### Requirement: Failed transition leaves the group Undefined

If a process configured for the requested state fails to start within its startup budget or terminates unexpectedly before the transition completes, the EM SHALL stop the transition, set the group state to `Undefined`, and report the failure. A termination timeout SHALL NOT leave the group Undefined: the process is force-terminated and the transition continues.

#### Scenario: Startup failure leaves the group Undefined

- **WHEN** a process needed in the requested state exhausts its startup attempts
- **THEN** the transition stops, the group state becomes `Undefined`, and the failure is reported

#### Scenario: Unexpected requested-state termination leaves the group Undefined

- **WHEN** a process configured for the requested state terminates unexpectedly during the transition
- **THEN** the transition stops, the group state becomes `Undefined`, and the failure is reported

#### Scenario: Termination timeout still reaches Off

- **WHEN** a process does not terminate within its termination timeout during a transition to `Off`
- **THEN** the EM force-terminates it and the group still reaches `Off`

### Requirement: Transition is single-flight and cancellable

A Function Group SHALL have at most one transition in flight. A new validated request for a different state SHALL cancel the in-flight transition and start the new transition. Because `SetState` has one acknowledgement, the cancellation is observed as the new request’s result (the old `InProgress` is superseded), not as a second acknowledgement. A request for the state already current or already the requested state SHALL be ignored and reported as success.

#### Scenario: New request replaces the in-flight transition

- **WHEN** a transition to `Driving` is in progress and a validated request for `Parking` arrives
- **THEN** the `Driving` transition is cancelled and the `Parking` transition starts

#### Scenario: Same-state request succeeds without a transition

- **WHEN** the group is already in `Driving` and a request for `Driving` arrives
- **THEN** the EM performs no transition and reports success

### Requirement: Transition waits are bounded per startup configuration

Each startup configuration SHALL provide a startup timeout, a termination timeout, a restart attempt count, and a restart delay. The EM SHALL monitor the time to reach `Running` and the time to reach `Terminated`, apply the configured retries on startup timeout, and log every timeout. A startup timeout SHALL follow the configured restart budget before the transition is declared failed.

#### Scenario: Startup timeout retries within the budget

- **WHEN** a reporting process does not reach `Running` within its startup timeout and has restart budget remaining
- **THEN** the EM terminates the attempt, waits the restart delay, and starts the next attempt

#### Scenario: Exhausted startup budget fails the transition

- **WHEN** the last startup attempt also times out
- **THEN** the transition stops, the group becomes `Undefined`, and the startup failure is reported
