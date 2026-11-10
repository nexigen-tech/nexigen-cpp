# sm/recovery-handler Specification

## Purpose

Defines the SM's high-priority recovery handler: the interface through which the PHM daemon reports critical faults, interrupting non-critical transitions and driving the failed Function Group into `SafeState` or `DegradedMode` within one millisecond.

## Requirements

### Requirement: PHM fault-event interface

The SM SHALL provide a dedicated high-priority recovery interface for the PHM daemon: a recovery-notification event channel carrying the failed Function Group; on receiving a critical fault event the SM SHALL interrupt any ongoing non-critical transitions and initiate the failed group's transition to `SafeState` or `DegradedMode` within 1 ms.

#### Scenario: Fault event drives SafeState

- **WHEN** the PHM emits a critical fault event for a Function Group while a non-critical transition is in progress
- **THEN** the SM aborts the non-critical transition and commands the group into `SafeState` within 1 ms

### Requirement: Recovery diagnostics

The recovery handler SHALL provide a cold-path `to_string()` member plus free helper and a `std::formatter` specialization reporting the last fault event handled.

#### Scenario: Recovery renders its last fault

- **WHEN** `to_string()` and `std::format("{}", recovery)` are evaluated after a fault event
- **THEN** the text names the affected group and the formatter output equals the member output
