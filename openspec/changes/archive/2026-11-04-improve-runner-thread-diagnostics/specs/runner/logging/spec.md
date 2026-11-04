# Spec Delta

## MODIFIED Requirements

### Requirement: Affinity failures are non-fatal warnings

Under the default `BestEffort` mode, an out-of-range CPU index, duplicate entry, unsupported logger index, scheduler rejection, or kernel affinity rejection SHALL produce a warning/startup report and SHALL NOT stop supervision or change application affinity. The warning SHALL report the actual per-attribute application outcome from the `ThreadApplyReport` — which of name, affinity, and scheduler were requested, applied, and failed, plus the first error — and SHALL NOT claim the thread is "unpinned" or name an "invalid cpus" field when a non-CPU attribute failed or when some attributes applied successfully. A failure explicitly configured as `Required` SHALL instead abort runner initialization before applications launch; runner MUST NOT silently continue with an unapplied required RT configuration.

#### Scenario: Invalid control CPU is ignored

- **WHEN** best-effort control placement contains an index outside the online set
- **THEN** a warning is recorded, the control thread is not pinned, and the command proceeds

#### Scenario: Invalid logger CPU is ignored

- **WHEN** best-effort logger placement contains an index the logger cannot use
- **THEN** a warning/startup report is recorded and the logger runs without pinning

#### Scenario: Kernel rejection is ignored

- **WHEN** the kernel rejects requested best-effort affinity or scheduling
- **THEN** a warning/startup report identifies the failed attribute and the process continues with any applied attributes

#### Scenario: Scheduler rejection is reported by attribute

- **WHEN** a best-effort scheduler request is rejected while the CPU set and name are valid
- **THEN** the warning names the scheduler attribute (not "cpus") and the process continues

#### Scenario: Partial application is reported accurately

- **WHEN** name and affinity apply but the scheduler is rejected under `BestEffort`
- **THEN** the warning reports the applied name and affinity and the failed scheduler separately rather than stating the thread is unpinned

#### Scenario: Required control or logger failure aborts initialization

- **WHEN** a required runner attribute cannot be applied during suite start
- **THEN** initialization returns `SystemError` before applications launch and no non-RT fallback is presented as success

#### Scenario: Applications are not affected by a failed pin

- **WHEN** runner pinning fails
- **THEN** applications still receive their configured or full CPU set
