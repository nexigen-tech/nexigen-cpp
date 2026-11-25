# Proposal

## Why

`replace-can-service-packet` moved the public `tx` service to `CanMessageBatch`, but `nexigen-cantx` still unpacks every packet into a per-message ring and `CanTransmitter::submit_loop` re-batches the messages one by one. At 10 000 frames/s that keeps the sender at roughly 200 % CPU: two threads spend their time on 10 000 ring pushes, 10 000 ring pops, per-message validation and re-assembly, even though the packet already exists when it arrives.

The packet boundary should survive the queue instead of being destroyed and recreated.

## What Changes

- Add `CanTransmitter::enqueue_batch(std::string_view channel, const CanMessageBatch&)`.
  - Validate the packet once on the caller thread.
  - Normalize every contained message (zero timestamp, unknown origin) while copying the packet.
  - Push the whole packet into the channel ring as one element.
- Replace the per-message `CanMessageRing<1024>` inside `CanTransmitter` with a bounded SPSC ring of `CanMessageBatch` packets (capacity 16).
- Make `submit_loop` pop whole packets and hand them straight to the driver's `submit_batch` path, removing per-message pop, copy, sequence handling and re-batching.
- Keep `CanTransmitter::enqueue(CanMessage)` unchanged in signature and semantics; it now wraps the message into a one-message packet and uses the same batch path.
- Preserve all-or-nothing packet admission: a packet is accepted completely or rejected completely; no partially submitted batch.
- Preserve per-message order, timestamp, hardware sequence, kind, flags, origin and payload.
- Count a dropped packet as `count` dropped messages, and keep a single `BufferFull` result for the caller.
- Update `nexigen-cantx` to call `enqueue_batch` from its `tx` callback and count a rejected packet once.
- Consume fired wait-set listener activations in `EventConsumerSet` before draining, so an iceoryx2 waitset no longer stays level-triggered and busy-polls `epoll_wait`.
- Report `Events` only when a drain actually delivered a sample, and `WokenUp` only when the wake flag was set.

No service payload, service name, `CanMessage` layout or `CanClient`/`CanPublisher` API changes. The change is compatible with `replace-can-service-packet` and does not require a wire-format migration.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `hw/can/sender-app`: the sender queue accepts whole `tx` packets atomically and submits them without re-batching, while single-message enqueue remains supported and bounded.
- `com/event-system`: a wait-set-driven consumer consumes its fired listener activation, so idle waits do not spin after traffic or wake-ups.

## Impact

**Performance**

- Removes 10 000 SPSC push/pop pairs per second at the demo rate and replaces them with about 100 packet push/pop pairs per second.
- Removes 10 000 per-message validations on the submit thread; validation stays on the caller thread once per message, inside the packet copy.
- Consuming fired listener activations removes the level-triggered `epoll_wait` loop that kept each waitset thread at ~100 % CPU.
- Measured at 10 000 frames/s (`20 000` frames in 2 s): `nexigen-cantx` falls from ~201 % to **2.5 %**, `nexigen-canrx` from ~101 % to **2.0 %**, `nexigen-canvbus` from ~100 % to **1.2 %**, and `canview` stays at ~5.5 %. The CAN submit worker itself is ~1 %.
- Latency stays bounded by the existing 100-message / 10 ms flush rule. A packet is submitted as one driver batch instead of being re-cut by the worker. The waitset fix adds one non-blocking `try_wait` per fired attachment.
- Memory allocation: none. The ring is a fixed member array; `try_wait` uses stack callbacks.

**Memory**

- Per channel, the ring changes from 1024 × 112 B = 114 688 B to 16 × 11 216 B = 179 456 B, an increase of 64 768 B (+56 %) per configured channel.
- Capacity 16 holds 15 usable SPSC slots, up to 1500 messages, about 150 ms of traffic at 10 000 frames/s, compared with about 102 ms for the old 1024-message ring.
- `CanMessageBatch` stays 11 216 B and 8-byte aligned; no layout change.
- The waitset fix adds no storage to `EventConsumerSet`; it reuses the existing listener guards.

**Code**

- `include/nexigen/hw/can/com/can_transmitter.hpp`
- `src/hw/can/com/can_transmitter.cpp`
- `tools/nexigen-cantx/main.cpp`
- `tools/detail/can_tool_common.hpp`
- `include/nexigen/com/i_event_consumer.hpp`
- `include/nexigen/com/event_consumer.hpp`
- `include/nexigen/com/event_consumer_set.hpp`
- `include/nexigen/synchro/sync_consumer.hpp`
- `tests/hw/can/` sender, vocabulary and e2e fixtures
- `tests/com/event_system_test.cpp`
- `docs/architecture.md`, `docs/articles/hw/can.md`

**Compatibility**

- Additive C++ API: `enqueue_batch` is new, `enqueue` and `to_string` keep their signatures.
- `IEventConsumer` gains a pure virtual `consume_activation()`; in-tree consumers are updated. Out-of-tree implementers must add the same non-blocking listener drain.
- No change to `CanMessageBatch`, the `tx`/`rx` services, aliases or the virtual bus.
- Mixed-version behaviour is unchanged from `replace-can-service-packet`: all CAN endpoints must already speak the packet contract.

## Risks

- A packet ring couples queue depth to packet size. With 15 usable slots and 100-message packets, one stalled consumer can hold 1500 messages; the drop policy must count the whole packet.
- Validation must run per message even though the packet is pushed once; skipping it would let invalid frames reach the driver.
- The caller thread now copies 11 KiB per packet instead of 100 × 112 B. The byte count is identical, but the copy must stay allocation-free and bounded.
