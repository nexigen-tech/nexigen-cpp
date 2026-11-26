# Proposal

## Why

The CAN status channel is created with `max_publishers=2` because the receiver and the transmitter both publish status. `EventConsumer` keeps one transport sequence/epoch state for the whole channel, so every alternation between the two publishers looks like a `ProducerRestart`. `nexigen-canview` turns each restart into a synthetic `TransportGap` record, producing steady false-positive loss markers (`id=0x0`, `hw_ts=0`, `count=0`, `exact=at least`) even though no event was lost.

## What Changes

- Add per-publisher sequence tracking to `EventSequenceTracker` when `max_publishers > 1`: one sequence/epoch state per 16-byte `app_id` in a fixed 16-slot table.
- Establish a baseline silently for each newly seen application; report `ProducerRestart` only when that application's own epoch changes; report gaps, duplicates, and unexpected resets against that publisher's sequence.
- Fall back to the existing single-sequence behaviour when the channel allows one publisher, when `app_id` is empty, or when more distinct applications appear than the configured publisher slots.
- Pass `ChannelConfig::max_publishers` from `EventConsumer::create()` into the tracker.
- Add `status_channel_config()` and use it for the receiver, transmitter, `CanClient`, and `nexigen-canview`, so every status endpoint requests the same two-publisher service.
- Update the `com/event-system` capability with a multi-publisher tracking requirement.

No change to service names, payloads, the single-publisher contract, or the on-wire event header.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `com/event-system`: sequence and epoch tracking for channels that legitimately have more than one publisher.

## Impact

- **Code**: `include/nexigen/com/detail/event_sequence_tracker.hpp`, `include/nexigen/com/event_consumer.hpp`, `include/nexigen/hw/can/com/{can_transport,can_client}.hpp`, `tools/nexigen-canrx`, `tools/nexigen-cantx`, `tools/detail/can_tool_common.hpp`, `tools/nexigen-canview/backend/capture_thread.cpp`, and the related tests.
- **APIs**: no signature change to `EventConsumer` or `EventSequenceTracker::observe()`; `EventSequenceTracker` gains an explicit constructor argument with a default of `1`, preserving existing callers.
- **Performance / latency**: single-publisher channels keep the original branch and pay only one comparison. Multi-publisher channels add a bounded scan of at most `max_publishers` (≤16) slots plus one 16-byte `memcmp` per event; no allocation, syscall, lock, formatting, or blocking. The path stays wait-free and bounded.
- **Memory**: `EventSequenceTracker` grows from 88 B to 768 B (one 16×40 B publisher table, one 40 B overflow state, the counter, and the existing 80 B statistics). `EventConsumerSet<16>` worst case grows by about 10.6 KiB. Storage is fixed and in-place; no heap allocation is introduced.
- **Systems**: status endpoints intentionally share one service with two publishers; the change makes consumers interpret that topology correctly. Single-publisher services keep byte-for-byte behaviour.
- **Testing**: pure tracker unit tests, a live two-producer event-channel test, a canview capture test with two status publishers, and the existing CAN/COM suites; Debug, Release, ASan, and TSan builds.
