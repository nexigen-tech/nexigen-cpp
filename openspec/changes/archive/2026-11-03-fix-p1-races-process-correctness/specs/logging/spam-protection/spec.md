# logging/spam-protection delta spec

## REMOVED Requirements

### Requirement: Dynamic quota updates are wait-free

**Reason**: The former "wait-free" claim described a plain-struct write from
the reconfiguring thread while producers load the fields on their hot path —
a data race with no ordering story (analysis 2026-11-03 #9). The honest
contract is per-field atomic observability, not linearized reconfiguration.
**Migration**: Replaced by the ADDED requirement "Dynamic quota updates are
race-free" in this delta.

## ADDED Requirements

### Requirement: Dynamic quota updates are race-free

`set_spam_protection_config(const SpamProtectionConfig&) noexcept` SHALL
publish each consumed quota field with an atomic store, so a producer's hot
path never observes a byte-torn value and neither side ever blocks. Each field
read concurrently SHALL be a valid old or new value (field-atomic, eventually
consistent — not linearized end-to-end).

#### Scenario: Config change takes effect without stopping producers

- **WHEN** `set_spam_protection_config` lowers `max_messages_per_sec_per_shard`
  while producers are logging
- **THEN** subsequent producer calls observe the new quota within a bounded
  number of calls, no producer blocks during the change, and TSan reports no
  data race between the reconfiguration and concurrent `emit()` calls

#### Scenario: No torn quota read

- **WHEN** a producer's hot path loads any quota field while
  `set_spam_protection_config` stores it concurrently
- **THEN** the loaded value is whole (a valid old or new field value), never a
  mixture of bytes from both

#### Scenario: Boundary mixture of fields is tolerated

- **WHEN** a producer performs several quota loads during one emit while a
  reconfiguration runs concurrently
- **THEN** individual fields remain whole values; a documented one-call mixture
  across fields (e.g. old limit with new bypass level for that call) is within
  the tolerated semantics
