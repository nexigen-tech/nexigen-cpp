# logging/spam-protection Specification

## Purpose

Defines two-level hot-path anti-spam for `nexigen::logging`: Level 1 repeat suppression (collapsing consecutive identical messages per shard) and Level 2 per-shard token-bucket rate limiting with `bypass_min_level` escape hatch.

## Requirements

### Requirement: Repeat suppression collapses identical messages

When `enable_repeat_suppression == true`, the producer SHALL compare `source_location`, format pointer, and `arg_payload_` bytes with the previous shard message; on a match the slot SHALL NOT be occupied and the shard's repeat counter SHALL increment.

#### Scenario: Identical consecutive messages are collapsed

- **WHEN** the same message is logged repeatedly from one thread with repeat suppression enabled
- **THEN** only the first message occupies a ring slot; subsequent identical messages increment the repeat counter without enqueuing

#### Scenario: Suppressed summary is emitted on transition

- **WHEN** a different message arrives after repeated identical messages
- **THEN** a `[SUPPRESSED] Last message repeated N times` record precedes the new message in the sink output

### Requirement: Per-shard token-bucket rate limiter

Each shard SHALL check a per-second message counter against `max_messages_per_sec_per_shard` (0 = no limit). Messages below `bypass_min_level` exceeding the quota SHALL be dropped in place (~1 ns) and counted in `throttled_count_` (`alignas(64) std::atomic<uint64_t>`, relaxed).

#### Scenario: Messages at or above bypass_min_level always pass

- **WHEN** `bypass_min_level == LogLevel::Critical` and the token bucket is exhausted
- **THEN** a `LogLevel::Critical` record is still enqueued while a `LogLevel::Info` record is dropped

#### Scenario: Rate limiting is wait-free

- **WHEN** the token bucket is checked on the hot path
- **THEN** the check completes in one atomic read and a compare, with no mutex, spin, yield, sleep, or CAS loop

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
