# core/thread-management — internal structure (delta)

## Purpose

This delta specifies how the `core/thread-management` capability is internally reorganized to reduce cyclomatic complexity without changing any public type, function signature, JSON contract, or runtime behavior.

## Requirements

### Requirement: `apply_thread_spec_self` delegates to single-attribute applicators

The public `apply_thread_spec_self` function SHALL remain the only entry point for applying a `ThreadSpec` to the calling thread. Its body SHALL be composed of small, focused helpers that each handle one attribute class: name, affinity, or scheduler. Each helper SHALL record its result in the supplied `ThreadApplyReport` and SHALL honor the current `ThreadApplyMode` (best-effort vs. required).

#### Scenario: Name application is isolated

- **WHEN** a `ThreadSpec` requests a non-empty name
- **THEN** a dedicated helper attempts `pthread_setname_np`, updates `report.applied` or `report.failed`, and sets `report.first_error` on first failure

#### Scenario: Affinity application is isolated

- **WHEN** a `ThreadSpec` requests a non-empty `CpuSet`
- **THEN** a dedicated helper builds the native `cpu_set_t`, attempts `pthread_setaffinity_np`, and records the outcome in the report

#### Scenario: Scheduler application is isolated

- **WHEN** a `ThreadSpec` has `scheduler_configured()` true
- **THEN** a dedicated helper builds the native `sched_param`, attempts `sched_setscheduler`, and records the outcome in the report

### Requirement: Required-mode rollback remains centralized

The rollback path for `Required` mode SHALL remain in the main function or a single `restore_applied_attributes` helper. It SHALL restore name, affinity, and scheduler in the reverse order of mutation and SHALL update `report.rollback_failed` for any restoration that fails.

#### Scenario: Required FIFO failure rolls back earlier changes

- **WHEN** name and affinity are applied but scheduler fails under `Required`
- **THEN** the function returns `SystemError`, affinity and name are restored, and `report.rollback_failed` is zero if all restorations succeed

### Requirement: `parse_thread_spec_json` delegates to field parsers

The internal `parse_thread_spec_json` function SHALL parse each JSON field through a dedicated helper: `parse_name`, `parse_cpus`, `parse_mode`, and `parse_scheduler`. Each helper SHALL operate on a staging `ThreadSpec` and SHALL return a failure indication rather than mutating the output on a bad field.

#### Scenario: Oversized name leaves the destination unchanged

- **WHEN** the JSON `name` field is 16 bytes or contains a NUL
- **THEN** the helper reports failure and the caller returns `false` without modifying the caller's `ThreadSpec`

#### Scenario: Invalid scheduler pair leaves the destination unchanged

- **WHEN** the JSON `policy`/`priority` pair violates the `Other=0`, `FIFO/RR=1..99` rule
- **THEN** the helper reports failure and the caller returns `false` without modifying the caller's `ThreadSpec`

### Requirement: Refactored internals preserve all observable behavior

After decomposition, the public functions SHALL produce identical results for every input: the same `ThreadApplyReport` masks, the same rollback behavior, the same JSON round-trip, and the same validation errors. No public header SHALL gain or lose a symbol.

#### Scenario: Existing thread-management tests pass unchanged

- **WHEN** the existing unit tests for `ThreadSpec`, `CpuSet`, `apply_thread_spec_self`, and JSON round-trip run
- **THEN** they pass without modification

### Requirement: Complexity of decomposed functions stays within the project target

Every function introduced or modified by this refactor SHALL have cyclomatic complexity ≤ 15 as measured by lizard. The overall average complexity for `include/nexigen/core/thread_management.hpp` SHALL not increase.

#### Scenario: Post-refactor lizard report

- **WHEN** lizard is run over `include/nexigen/core/thread_management.hpp`
- **THEN** no function in the file exceeds complexity 15
