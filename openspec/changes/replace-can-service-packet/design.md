# Design

## Context

See `proposal.md`. The A change already provides `CanMessageBatch`, batch-capable `CanDriver`, and batched `vbus/*`. C promotes the same packet to the public `rx`/`tx` services and keeps the user-facing C++ API per-message.

## Goals / Non-Goals

**Goals:**

- Remove per-message transport publication from public `rx`/`tx`.
- Keep `CanClient` receive callbacks and `CanPublisher::send(CanMessage)` source-compatible.
- Preserve every per-message timestamp, sequence, kind, flags, origin and payload.
- Make packet boundaries and logical sequence loss explicit.

**Non-Goals:**

- Change session JSONL, `CanMessage` layout, service names or application ids.
- Preserve wire compatibility with old binaries.
- Require callers to construct packets directly.

## Decisions

### D1 — Public payload is `CanMessageBatch`

`rx` and `tx` use the existing 11 216-byte `CanMessageBatch` type. A single-message operation is represented as a one-message packet. The packet's `channel_id` and every contained message's `channel_id` must match.

Alternative rejected: a separate `CanPacket` alias with a different layout; it would duplicate validation and JSON vocabulary for no benefit.

### D2 — C++ APIs stay per-message

`CanClient::subscribe`/`subscribe_with_loss` subscribe to `CanMessageBatch` and invoke the existing per-message callback once per contained message. `CanPublisher::send(CanMessage)` enqueues into its existing message ring; its worker batches up to 100 messages and flushes after 10 ms. `CanClient::publish` returns the same `CanPublisher` type.

This keeps application source compatible while moving packet assembly into the transport boundary.

### D3 — Receivers and transmitters batch at the service edge

`CanReceiver` owns the existing per-channel SPSC message ring. Its publish worker forms `CanMessageBatch` packets using the same 100-message/10 ms rule and publishes them with `event_rx`. `CanTransmitter` subscribes to `CanMessageBatch`, validates each contained message and enqueues it individually into its existing ring; the driver batch path is unchanged.

`CanReceiver` and `CanTransmitter` retain per-channel `next_sequence` counters. `first_sequence` increments by the number of accepted messages, independent of `CanMessage.hardware_sequence` and the `EventHeader.sequence_id` (which counts packets).

### D4 — Channel and packet validation

Every packet is validated before publication:

- count 1..100;
- channel id matches the target channel;
- every contained message validates and shares the packet channel id;
- `first_sequence + count` does not overflow;
- `text`/FD/remote kinds obey the existing channel rules.

A receiver rejects the whole packet on any mismatch; it does not partially deliver.

### D5 — Memory and latency

Public packet queues use `batch_channel_config()`: 32 samples, history 8, matching the virtual services. `CanPublisher` and `CanReceiver` keep their existing message rings (1024/4096 messages) and assemble packets on the worker thread.

Packet latency is bounded by the same 100-message/10 ms rule as A. The additional copy into `CanMessageBatch` is 11 KiB per 100 messages, not per message.

## Risks / Trade-offs

- **Breaking wire compatibility** → all in-tree producers/consumers are updated together; mixed-version processes fail with an incompatible type layout. No disk/session migration is needed.
- **Large packet queues** → use 32-sample public packet configs, not the 512-sample default.
- **Double batching** → `CanPublisher`/`CanReceiver` batch once at the service edge; `CanTransmitter` batches again only at the driver edge because hardware drivers may require per-message submission.
- **Loss accounting** → packet `first_sequence` is authoritative for packet gaps; hardware sequences remain authoritative for per-message gaps.
- **Old tests publishing raw `CanMessage`** → update test fixtures to publish one-message `CanMessageBatch` packets.

## Migration Plan

1. Update `CanPublisher`, `CanReceiver`, `CanClient` and `canrx`/`cantx` to packet services.
2. Update `canmon`/`canview` through `CanClient`/capture expansion; keep callbacks per message.
3. Update transport/client tests and e2e fixtures to publish packets.
4. Remove the A-only "batch is internal" requirement and document the breaking service payload.
5. Run the full CAN matrix and the 10k demo; rollback is a coordinated revert of all packet endpoints.
