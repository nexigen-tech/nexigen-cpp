# Design

## Context

See `proposal.md` for motivation. The current sender path after `replace-can-service-packet`:

- `nexigen-cantx` receives a `CanMessageBatch` on `tx`, loops over `batch.count` and calls `CanTransmitter::enqueue` once per message.
- `CanTransmitter::Channel` owns `detail::CanMessageRing<1024>` (1024 × 112 B = 114 688 B).
- `enqueue` validates and normalizes one `CanMessage` and copies it into that ring.
- `submit_loop` pops messages one by one, accumulates up to 100 messages or 10 ms, then calls `driver_->submit_batch`.

The public packet boundary is therefore destroyed on arrival and recreated on submission.

## Goals / Non-Goals

**Goals:**

- Carry one `CanMessageBatch` from the `tx` callback to the driver without per-message queue traffic.
- Keep packet admission atomic and message order exact.
- Keep `CanTransmitter::enqueue(CanMessage)` source-compatible.
- Remove the submit thread's per-message pop/copy/re-batch work.

**Non-Goals:**

- Change `CanMessageBatch`, `CanMessage`, service names or the public `tx`/`rx` payload.
- Change `CanDriver` or add a no-validation driver entry point.
- Change `CanPublisher`, `CanClient`, `CanReceiver`, `canrx`, `canmon` or `canview`.
- Add heap allocation or a dynamically sized queue.
- Change the 100-message packet limit or the 10 ms batching used by producers.

## Decisions

### D1 — The channel queue holds `CanMessageBatch` elements

`CanTransmitter::Channel` replaces `detail::CanMessageRing<1024>` with `detail::SpscRing<CanMessageBatch, 16>`.

Exact layout per channel:

| Field | Size | Alignment |
|---|---:|---|
| `std::array<CanMessageBatch, 16> buffer_` | 179 456 B | 8 B |
| `alignas(64) std::atomic<std::size_t> head_` | 8 B | 64 B |
| `alignas(64) std::atomic<std::size_t> tail_` | 8 B | 64 B |

179 456 B is an exact multiple of 64, so `head_` starts on its own cache line; `tail_` is aligned to the next cache line. Total ring size is 179 584 B per channel, up from 114 688 B (+64 896 B, +57 %). With the 16-channel maximum the rings total about 2.87 MB; `Channel` objects are allocated once at construction, never on a hot path.

Capacity 16 is chosen because 15 usable SPSC slots × 100 = 1500 messages, roughly 150 ms at 10 000 frames/s, compared with about 102 ms for the old 1024-message ring. Capacity 8 would halve the memory but cut the worst-case buffer to 70 ms; a dynamically sized or heap-backed pool is rejected because it violates the no-allocation mandate and adds indirection to the hot path.

`CanMessageBatch` stays 11 216 B and 8-byte aligned, so the ring needs no per-element padding.

### D2 — One atomic admission path

`enqueue_batch` runs on the transport callback thread:

1. Find the channel (existing linear scan over at most 16 channels).
2. Check `count` in 1..`max_messages`, `reserved == 0`, `first_sequence + count` does not overflow, packet `channel_id` matches the target, and every contained message matches the channel id, validates, is `ClassicData`/`FdData`/`Remote`, and respects the FD setting.
3. Copy the packet into a local `CanMessageBatch pending` and normalize each message there: fill a zero `hardware_timestamp_ns` from the canonical clock and set `origin = LocalTransmit` when unknown.
4. Call `ring.try_push(pending)` exactly once.

The local 11 216 B copy is stack-resident; it avoids mutating the caller's const packet and avoids a partially written ring slot. The callback runs on the App run thread with the default stack, and the copy is a single contiguous `memcpy`, so it stays allocation-free and bounded.

Admission is all-or-nothing. Any invalid message rejects the whole packet; the ring never sees a partial packet. On `try_push` failure, `dropped` increases by `count` and the call returns `BufferFull`. On validation failure, `rejected` increases by `count` and the first `core::ErrorCode` is returned.

`enqueue(CanMessage)` keeps its signature and becomes a thin wrapper: validate and normalize through the existing single-message rules, build a one-message packet, and call the same admission function. Only production producers use `enqueue_batch`; single-message producers keep working but are bounded by 16 packets rather than 1024 messages, which is recorded as a trade-off.

### D3 — The submit worker forwards packets

`submit_loop` now pops one `CanMessageBatch` and calls `submit_packet(channel, packet)` directly:

- `count == 1`: call `driver_->submit` with the existing bounded retry loop (8 yields) so single-frame backpressure behaviour is unchanged.
- `count > 1`: call `driver_->submit_batch` once.
- On success, `submitted_ += count`; on failure, `failed += count`; a `SystemError` still sets `Warning` and publishes status.
- The packet is not cleared; the next `try_pop` overwrites the local copy.

The 100-message / 10 ms accumulation disappears from the transmitter because producers already form packets. `CanPublisher` and `CanReceiver` keep their existing 100-message / 10 ms policy. A single-message caller now reaches the driver immediately instead of waiting for the worker's 10 ms window; the frame content is unchanged and the driver call shape for that caller becomes one message per submit.

### D4 — Atomics and memory ordering

The ring keeps the existing SPSC protocol, which is already proven in `SpscRing`:

- Producer: `head_.load(relaxed)`, `tail_.load(acquire)`, write `buffer_[head]`, `head_.store(next, release)`.
- Consumer: `tail_.load(relaxed)`, `head_.load(acquire)`, read `buffer_[tail]`, `tail_.store(next, release)`.
- `size()` uses acquire loads on both indices.

`alignas(64)` on `head_` and `tail_` prevents false sharing between the callback thread and the submit worker. The payload array is written only by the producer before the release store and read only by the consumer after the acquire load, so no per-message atomics are needed.

The new `rejected` counter is `std::atomic<std::uint64_t>` updated with `relaxed`; it is diagnostic only and never orders payload access.

### D5 — Counters and diagnostics

`Channel` gains `std::atomic<std::uint64_t> rejected{0}` next to `dropped` and `failed`. `dropped` and `failed` keep their current meanings; both are now incremented in message units, not packet units.

`CanTransmitter::to_string()` keeps its current format (`CanTransmitter(channels=..., submitted=...)`), so existing log parsing does not change. Rejected/dropped values stay available through the channel counters for tests and future diagnostics.

### D6 — Compatibility

- `CanTransmitter::enqueue`, `start`, `stop` and `to_string` keep their signatures.
- `enqueue_batch` is additive.
- The `tx`/`rx` payload stays `CanMessageBatch`.
- No change to `CanMessageBatch` layout or serialization.
- Version mixing rules are unchanged from `replace-can-service-packet`: all endpoints already speak packets.

### D7 — Wait-set consumers consume their listener activations

The CPU measurement exposed a second bottleneck: every `EventConsumerSet` wait loop kept one thread at ~100 % because iceoryx2 reports a listener as fired in `epoll_wait`, and the set drained only the subscriber queue. The level-triggered listener activation was never consumed, so `wait_and_process_once_with_timeout` returned immediately forever.

The fix is to acknowledge the attachment before draining:

- `IEventConsumer` gains `consume_activation()`. `EventConsumer` implements it with the non-blocking `listener_.try_wait()`, which consumes all pending activations for that listener. `SyncConsumer` forwards to its endpoint.
- `EventConsumerSet::process_attachment` calls `consume_activation()` before `drain()`, and records `Events` only when `received()` actually advanced.
- The private wake listener is consumed with `try_wait()` too. `WokenUp` is reported only when `wake_requested_` was set, so a stale wake activation is consumed silently.
- Errors from `try_wait()` are mapped into the existing `Status` result and returned from `wait()`/`wait_for()`.

`try_wait()` uses a stack callback, allocates nothing and does not block. One extra call runs per fired attachment. Memory layout and atomics are unchanged.

Alternative rejected: adding a timeout sleep when the wait returns early. It hides the leaked activation and adds latency; consuming the activation is the contract iceoryx2 documents for wait-set attachments.

## Risks / Trade-offs

- **Packet granularity reduces single-message depth** → Production producers use 100-message packets, where 15 usable slots hold 1500 messages. Single-message callers are limited to 15 queued frames and should migrate to `enqueue_batch`; tests are updated to exercise the packet path.
- **11 KiB stack copy per packet** → One contiguous copy on the run thread, no heap. If stack pressure ever appears, the copy can move to a per-transmitter scratch buffer because admission is single-threaded.
- **Whole-packet loss is coarser than per-message loss** → The dropped counter still advances by the message count, so totals remain comparable with the old ring.
- **Driver re-validates the batch** → `CanDriver::submit_batch` keeps its contract validation; the sender simply stops doing a second unpacking pass of its own.
- **Larger per-channel ring** → +64 896 B per channel, allocated once; the hot working set is only the head/tail cache lines plus the slot being copied.
- **Immediate single-message submission** → Only affects callers of the legacy `enqueue`; `CanPublisher` and `cantx` send packets and keep their batching cadence.
- **Extra `try_wait` per fired attachment** → Non-blocking, stack-only, and paid once per notification instead of spinning the whole core.
- **Stale activation becomes a normal timeout** → After traffic or a wake-up, the next idle wait consumes the leftover activation and returns `Timeout` instead of a synthetic `Events`/`WokenUp`.

## Migration Plan

1. Add the packet ring and `enqueue_batch`; keep `enqueue` as a wrapper.
2. Switch `nexigen-cantx` and `can_tool_common` to `enqueue_batch`.
3. Update bridge, vocabulary and e2e fixtures to publish packets and assert one driver batch.
4. Add `consume_activation()` to the consumer interface and consume fired listener and wake activations in `EventConsumerSet`; add the no-spin regression test.
5. Re-measure the 10 000 frames/s demo and record the before/after CPU in `docs/architecture.md`.
6. Rollback is a revert of the transmitter and consumer-set changes; no persisted data or wire format is affected.
