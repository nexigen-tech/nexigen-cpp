# Spec Delta

## MODIFIED Requirements

### Requirement: Declarative health manifest

The daemon SHALL load a JSON health manifest describing each supervised process, its checkpoint service identifier, Alive window and margins, Logical transition graph, Deadline pairs, failure tolerance, safety-critical flag, and recovery rules. The EM lifecycle channel SHALL be the standard `Nexigen/PHM/ExecutionState` service and SHALL NOT require a new per-process manifest field; a process's checkpoint service and name remain sufficient to bind its supervision.

#### Scenario: Manifest configures supervision

- **WHEN** a manifest names a process with its checkpoints, graph, deadlines, and recovery rules
- **THEN** the daemon supervises exactly that process according to those rules

#### Scenario: Process entry carries its supervision parameters

- **WHEN** a manifest entry declares a checkpoint service and Alive/Logical/Deadline parameters
- **THEN** the daemon loads them and supervises the process

#### Scenario: Lifecycle channel needs no new field

- **WHEN** a process entry has no lifecycle-specific field
- **THEN** the daemon still receives its EM lifecycle events on the standard channel
