# Design

## Context

`EventConsumer` owns one `EventSequenceTracker`. It reports `ProducerRestart` whenever the incoming `producer_epoch` differs from the active epoch. That assumes one producer per service, which holds for data channels but not for the CAN status channel: `nexigen-canrx` and `nexigen-cantx` both publish `CanChannelStatus` with `max_publishers=2`. Their epochs alternate, so every status sample after the first is classified as a restart and `nexigen-canview` emits a synthetic loss marker.

`EventHeader` already carries a 16-byte `app_id`, and `ChannelConfig::max_publishers` is available at consumer creation. Neither is currently used by the tracker.

## Goals / Non-Goals

**Goals:**

- Preserve transport-loss detection for channels that legitimately have multiple publishers.
- Keep the single-publisher path and its observable behaviour unchanged.
- Keep tracking fixed-storage, allocation-free, and wait-free.
- Make the CAN status endpoints agree on one configuration.

**Non-Goals:**

- Detecting loss of one publisher relative to another (there is no cross-publisher sequence).
- Changing the event header, service names, publisher limits, or payloads.
- Adding heap-backed maps or per-publisher dynamic containers.

## Decisions

### Fixed per-application table

`EventSequenceTracker` gains a `std::array<PublisherState, 16>` plus one overflow state. Layout per publisher:

```text
PublisherState           40 B
  char app_id[16]        16 B   (copied from EventHeader::app_id)
  uint64_t epoch          8 B
  uint64_t expected       8 B
  bool tracked            1 B
  padding                 7 B
```

The tracker is 768 B total: 16 × 40 B = 640 B table, 40 B overflow state, 8 B publisher count, 80 B `EventConsumerStats` (existing). Before this change it was 88 B (80 B statistics + 8 B expected). The worst-case growth is +680 B per consumer and about +10.6 KiB for `EventConsumerSet<16>`. All storage is in-place; the tracker remains trivially movable, with no heap allocation, no per-thread shards, and no atomics. The type is used from one consumer thread, matching the existing tracker contract.

### Dispatch rules

- `publisher_count == 1`: use slot 0, exactly the previous algorithm.
- `publisher_count > 1`: find the tracked slot whose `app_id` matches the header (one `memcmp` per slot, at most 16); otherwise claim a free slot and baseline that publisher silently (first sample, no loss callback).
- If no free slot exists (more distinct application codes than configured publishers, or a history containing older applications), fall back to one overflow state that reports epoch changes as restarts. This preserves visibility instead of silently merging publishers.
- An `app_id` shared by two live producers (including the empty application code) collides by design; those producers fall back to epoch semantics. The 16-byte application code is the publisher's identity because the header's `producer_epoch` changes on every restart.

### Per-publisher state machine

Each slot keeps the existing `first sample / epoch change / expected / gap / duplicate / unexpected reset` transitions. Counters aggregate globally in `EventConsumerStats`; `current_epoch` records the last delivered publisher's epoch. A restart reports the slot's old and new epochs, not another publisher's.

### Status configuration

`status_channel_config()` centralizes `max_publishers=2` for the receiver, transmitter, `CanClient`, and `nexigen-canview`. Before this change `nexigen-canview` created its status consumer with `batch_channel_config()` (`max_publishers=1`), so even a corrected tracker would not have entered multi-publisher mode.

### Memory ordering

No atomics are added. The tracker is owned by one consumer and updated from the consumer thread only; the existing `EventConsumerSet` wake path and atomics are unchanged.

## Risks / Trade-offs

- **Tracker size**: every consumer carries 768 B instead of 88 B, even on single-publisher channels. The fixed table avoids a cold-path map and keeps the hot path allocation-free; 10.6 KiB for a full 16-consumer set is acceptable.
- **Slot exhaustion**: with more distinct application codes than `max_publishers`, the fallback can report restarts. This is bounded and no longer affects the intended two-publisher status topology.
- **Application-code collisions**: two instances of the same application code on a multi-publisher service cannot be separated; the tracker falls back to epoch behaviour for that slot.
- **Single-publisher regression risk**: the branch keeps the old code path and the entire existing event-system suite must stay green.
