# Spec Delta

## ADDED Requirements

### Requirement: View indexes and snapshots fit explicit budgets

The viewer SHALL bound LIVE view indexes to 8 views including `All` and PAUSED snapshots to 16 views. Find-match storage SHALL be allocated lazily. Each PAUSED snapshot SHALL contain at most 1 MiB of copied record data. A view or snapshot that cannot be allocated SHALL be rejected before it becomes visible, with a visible error.

#### Scenario: Live-index memory is bounded

- **WHEN** eight LIVE views exist at the default history capacity
- **THEN** their visible indexes consume about 32 MiB and no ninth LIVE index is allocated

#### Scenario: Paused snapshots are bounded

- **WHEN** a PAUSED clone copies a source with more than 1 MiB of records
- **THEN** its frozen record storage stays at or below 1 MiB and the oldest records are omitted

#### Scenario: Find storage is lazy

- **WHEN** a view is created and no find is active
- **THEN** no full-history match array is allocated for that view

#### Scenario: Rejected view allocation

- **WHEN** a new view cannot allocate its index or snapshot
- **THEN** no window is created, existing views stay intact, and the viewer reports the failure
