# logging/multi-producer delta spec

## ADDED Requirements

### Requirement: Drop summaries are synthesized by the worker

When an application thread completes the flush control barrier, the background
worker SHALL synthesize the drop summary for shards with pending drops — same
text contract, same one-summary-per-flush rule — and deliver it only to the
sink. The flushing thread and every other non-owner thread SHALL NOT push any
record into a ring shard they do not own; a shard's ring SHALL be `try_push`-ed
only by its owner thread, every other ring access being the consumer's `pop`.

#### Scenario: Foreign-thread flush never touches another shard's ring

- **WHEN** a producer thread has pending buffer-full drops and a different
  application thread (not the ring owner) completes the flush barrier under
  ThreadSanitizer while the owner keeps logging
- **THEN** no summary is pushed into the owner's ring by the flushing thread,
  TSan reports no data race on the shard ring's cells, and the summary appears
  exactly once in the sink stream after the barrier returns

#### Scenario: Flush-time summary content is unchanged

- **WHEN** records were dropped (rate-limited or buffer-full) before the flush
  and the flush barrier completes
- **THEN** the sink stream contains one warning line reporting the exact
  per-cause counts, followed by the sink synchronization

#### Scenario: No drops means no summary

- **WHEN** the flush barrier completes while all drop cells are zero
- **THEN** no summary record is added to the sink stream

### Requirement: Reported drop cells reset after synthesis

After the worker synthesizes a flush-time drop summary it SHALL reset exactly
the per-cause cells (and the throttled counter) whose values that summary
carried. Drop counters SHALL persist between flush barriers, so drops
accumulating after one summary are carried by the next one, and a summary
SHALL NOT repeat counts already carried by an earlier summary when no new
drops happened in between.

#### Scenario: Reported counters are not re-reported

- **WHEN** a flush barrier reports and resets a shard's pending cells and the
  owner logs no further drops before a second flush barrier
- **THEN** the second flush produces no duplicate summary for the same counts

#### Scenario: Drops accumulating after a summary reach the next summary

- **WHEN** the owner thread commits further drops after a flush summary was
  synthesized and then stops, and a second flush barrier completes
- **THEN** the second summary reports only the counts accumulated after the
  first summary

## MODIFIED Requirements

### Requirement: Sharded drop accounting without contention

Ring-full drops SHALL be counted per shard by its own producer with a single
atomic `fetch_add` on that shard's own state cell (no cross-thread counter on
the accepted path, no non-atomic read-modify-write); over-throttle drops SHALL
likewise be per-shard. Consumers of the counters (snapshot aggregates and
flush-time drop-summary synthesis) SHALL read the cells relaxed and SHALL be
the only writers allowed to reset them, resetting only cells whose values have
just been reported. The aggregate drop story (rate-limited vs buffer-full
separation, drop summary lines) is unchanged.

#### Scenario: Full shard drops only that shard's producer

- **WHEN** one thread's shard is full while other threads' shards have room
- **THEN** only the full-shard producer's records are dropped and counted, and
  the other threads continue accepting

#### Scenario: Snapshot aggregates all shards

- **WHEN** `dropped()` is called from any thread after drops on several shards
- **THEN** the returned statistics are the sum over all shards for each drop
  cause (a snapshot, not synchronized linearization)

#### Scenario: Concurrent bump during consumer reset loses no increments

- **WHEN** an owner thread commits new drops with `fetch_add` while a snapshot
  or reset of the same cell happens on the consumer thread
- **THEN** each increment survives as an atomic update, TSan reports no race,
  and no reset can observe a torn or self-decremented value
