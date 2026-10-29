# Spec Delta

## MODIFIED Requirements

### Requirement: Sharded drop accounting without contention

Ring-full drops SHALL be counted per shard by its own producer (no cross-thread counter on the accepted path) and aggregated by `dropped()` snapshots; over-throttle drops SHALL likewise be per-shard. The aggregate drop story (rate-limited vs buffer-full separation, drop summary lines) is unchanged. Drop information SHALL never be silently lost: pending drop counts SHALL be reported through an in-stream summary record at window transitions, at flush, and at logger destruction.

#### Scenario: Full shard drops only that shard's producer

- **WHEN** one thread's shard is full while other threads' shards have room
- **THEN** only the full-shard producer's records are dropped and counted, and the other threads continue accepting

#### Scenario: Snapshot aggregates all shards

- **WHEN** `dropped()` is called from any thread after drops on several shards
- **THEN** the returned statistics are the sum over all shards for each drop cause (a snapshot, not synchronized linearization)

#### Scenario: Flush-time drop summary is emitted

- **WHEN** drops occurred but no window transition happened before `flush()` or logger destruction
- **THEN** a drop summary record reporting the pending per-cause counts is pushed before the drain completes

#### Scenario: Drop summary emitted without throttle

- **WHEN** the throttle is disabled and `buffer_full` drops occur
- **THEN** a drop summary record is still emitted at the next record or at flush

#### Scenario: Summary includes all drop causes

- **WHEN** a drop summary is emitted
- **THEN** it reports `rate_limited`, `buffer_full`, `slot_exhausted` (`unregistered_thread_drops_`), and `throttled_count_` counts

#### Scenario: Failed summary push preserves counters

- **WHEN** the summary's `try_push` fails because the ring is full
- **THEN** the drop counters are NOT reset, and the summary is retried at the next opportunity (next record, next window, or flush)
