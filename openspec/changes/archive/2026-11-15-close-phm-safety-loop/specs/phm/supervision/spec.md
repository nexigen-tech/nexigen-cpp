# Spec Delta

## MODIFIED Requirements

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
