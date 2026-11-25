# Design

## Context

See `proposal.md` for the problem and scope. The current virtual path is:

```text
sender --tx(CanMessage)--> cantx --vbus/tx(CanMessage)--> canvbus
       --vbus/rx(CanMessage)--> canrx --rx(CanMessage)--> consumer
```

Every arrow is one `com` sample and one `EventProducer` loan/send/notify. The public application-facing services are specified by `hw/can/transport`; `vbus/*` is an internal virtual-hardware contract consumed only by `VirtualCanDriver`. The change therefore batches only the virtual links and keeps the public message contract unchanged.

## Goals / Non-Goals

**Goals:**

- Move the virtual links from one sample per CAN frame to one sample per bounded batch.
- Preserve every `CanMessage` value exactly: timestamp, hardware sequence, kind, error flags, origin, payload and idle duration.
- Keep `CanClient`, `nexigen-canmon`, `nexigen-canview`, standard `rx`/`tx`/`status` payloads and session files unchanged.
- Make loss visible as a number of lost CAN messages, not as one lost transport sample.
- Bound latency and memory explicitly.

**Non-Goals:**

- Do not change the public `hw/can/transport` payloads or names.
- Do not introduce a generic `com::Batch<T>` for all event types.
- Do not require batch support from hardware CAN drivers.
- Do not change `CanMessage` layout, validation or per-message timestamp semantics.
- Do not optimize the public `rx` publish path in this change; that remains a later option if `canrx -> rx` is still hot after measurement.

## Decisions

### D1 — Fixed `CanMessageBatch` envelope

`CanMessageBatch` lives in `hw/can` and is a plain payload, not a second CAN message model:

```text
CanMessageBatch: alignas(8), trivially copyable
offset  size  field
0        8    uint64_t first_sequence
8        2    uint16_t channel_id
10       2    uint16_t count
12       4    uint32_t reserved (zero)
16    11200  CanMessage messages[100]
------------------------------------------
total 11216 bytes; max_messages = 100
```

`CanMessage` remains 112 B, align 8. The envelope has no atomics and no owning members. `count` is 1..100. `first_sequence` is the logical sequence of `messages[0]` in the producing stream and increments by `count` between batches. `reserved` must be zero on publish and is ignored on receive.

`validate()` rejects an empty batch, a count above 100, a channel mismatch, an overflow of `first_sequence + count`, and any contained message that `CanMessage::validate()` rejects. Validation never mutates the batch.

Alternative rejected: an array of `EventHeader` plus `CanMessage` per item. It would add another 80 B per message and duplicate sequence/time semantics already carried by `CanMessage`.

### D2 — Driver contract: optional batch with a safe fallback

`CanDriver` gains:

```text
virtual Status submit_batch(std::string_view channel,
                            const CanMessageBatch& batch) noexcept;
```

The default implementation validates the batch and calls `submit(channel, message)` for every contained message. It returns the first failure after stopping, or `OK` when all messages were accepted. It performs no allocation. A driver that can transmit batches overrides it.

`CanDriverCapabilities` gains `bool batch_submission{false}`. Existing hardware adapters compile without overriding `submit_batch`; their capability stays false. `VirtualCanDriver` sets it to true.

The existing `submit()` remains the single-message entry point and must behave identically when called directly.

### D3 — Batching policy is per channel and bounded in time

`CanTransmitter` keeps its existing per-channel SPSC message ring. Its submit worker accumulates messages into a local `CanMessageBatch` and flushes when any condition holds:

- 100 messages are accumulated;
- 10 ms have elapsed since the first message of the current batch;
- the worker is stopping.

The 10 ms flush is a latency ceiling, not a fixed delay: a full batch flushes immediately. The channel's existing `tx_queue` remains the admission limit; batch assembly adds no second unbounded queue.

`first_sequence` is a per-channel producer counter incremented once per accepted message. It is independent of `CanMessage.hardware_sequence` and of the `com::EventHeader.sequence_id` (which counts batches).

Alternative rejected: timer-driven flush at a fixed 10 ms regardless of fill. It would add 10 ms to small/low-rate traffic and waste transport samples on low-rate channels.

### D4 — Batch only on the virtual driver links

`VirtualCanDriver` publishes `CanMessageBatch` on `vbus/tx` and subscribes to `CanMessageBatch` on `vbus/rx`. The public `rx`, `tx`, `status` services keep `CanMessage`/`CanChannelStatus` payloads.

On receive, `VirtualCanDriver` validates each batch, checks the channel id, expands the contained messages in order, and calls the existing `ICanMessageSink::on_message()` once per message. No new sink interface is required in this change.

On transmit, `CanTransmitter` calls `submit_batch()`; `VirtualCanDriver` publishes one batch sample. When `CanTransmitter` has one message or a driver does not support batch, a one-message batch or the default per-message fallback is used.

The `vbus/*` service payload type changes from `CanMessage` to `CanMessageBatch`. Both `nexigen-canvbus` and the virtual endpoints must be from the same build; no serialized state is involved and no public service name changes.

### D5 — Loss accounting uses the logical first sequence

The batch's `first_sequence` makes a lost batch measurable. A receiver of a per-channel batch stream keeps the next expected logical sequence. If the incoming batch starts above the expected value, the difference is the number of missing CAN messages; the receiver reports that count as one logical gap and advances the expectation to `first_sequence + count`.

For the virtual driver receive path, the expanded messages still reach `CanReceiver` in order. The batch-level loss counter is surfaced in `VirtualCanDriver`/`CanReceiver` diagnostics; where the driver supplies hardware sequences, existing per-message sequence tracking remains authoritative and avoids double counting. A batch loss must not be reported as one lost frame.

The `EventConsumer` batch-level loss callback is still useful for `EventHeader.sequence_id`; it is not used as the CAN-message loss count.

### D6 — Virtual bus remains per-message equivalent

`nexigen-canvbus` subscribes to `vbus/tx` batches and processes every contained message in order. Its output on `vbus/rx` is a batch with a fresh `first_sequence` for the receive stream.

- Loopback, origin marking and latency are applied to the batch as a unit where possible; a configured per-message jitter may split the output into multiple batches while preserving order.
- Fault injection is applied per contained message. If an injected error or overload must be inserted before a data frame, the output may be split into consecutive batches; it must not reorder the affected message relative to its fault.
- Heartbeats and external traffic are emitted as one-message batches.
- A dropped message reduces the output count; a dropped whole batch is represented by the next batch's `first_sequence` gap.
- The batch payload replaces the old `CanMessage` payload only on `vbus/rx` and `vbus/tx`; `canrx`/`cantx` still expose their standard services.

### D7 — Queue and memory sizing

`CanMessageBatch` is 11 216 B. The batch-capable virtual services use a dedicated `com::ChannelConfig`:

```text
queue_size  = 32 samples   (≈ 359 KiB per subscriber)
history_size = 8 samples   (≈ 90 KiB)
```

At 10 000 frames/s and 100 messages per batch, that is about 100 batches/s, so 32 samples cover ≈320 ms of transport backlog. The existing 512-sample default would reserve ≈5.7 MiB per subscriber and is not appropriate for 11 KiB payloads.

`CanTransmitter`'s existing message ring remains `tx_queue` (default 1024 messages); it copies at most one 11 KiB batch into the driver call. The `CanMessageBatch` assembly buffer lives on the worker stack or in the `Channel` object, not heap-allocated per batch.

### D8 — Concurrency and atomics

The existing `detail::SpscRing` contract is reused for any batch queue:

- producer: `head.load(relaxed)`, `tail.load(acquire)`, write slot, `head.store(release)`;
- consumer: `tail.load(relaxed)`, `head.load(acquire)`, read slot, `tail.store(release)`;
- head and tail stay on separate cache lines with `alignas(64)`;
- `CanMessageBatch` has no mutable atomics; it is copied once into the ring slot.

The `CanTransmitter` batch assembly and `first_sequence` counter are owned by the per-channel worker thread. No cross-thread atomics are added to the payload. The public `submit_batch()` may be called from the driver's single producer thread; the virtual driver's `EventProducer` remains single-threaded.

## Risks / Trade-offs

- **Batch latency** → Flush at 100 messages or 10 ms, whichever comes first; never wait for a full batch before sending a partial one at shutdown.
- **11 KiB payload memory** → Use 32-sample virtual queues and a dedicated batch `ChannelConfig`; do not reuse the 512-sample default.
- **Fault injection ordering** → Split output into consecutive batches at fault boundaries; preserve per-message order and test drop/duplicate/reorder/burst.
- **Loss accounting double counting** → Keep hardware-sequence gaps authoritative where available; use `first_sequence` only for batch-level losses and test both together.
- **`canrx -> rx` remains per-message** → This change targets the virtual links first; measure before deciding whether a later public `rx` batch path is warranted.
- **Mixed-version virtual tools** → `vbus/*` payload layout changes; require the bus and both bridges from the same build. No disk/session migration is needed.

## Migration Plan

1. Add `CanMessageBatch`, validation and vocabulary tests; existing `CanMessage` services continue unchanged.
2. Add the driver capability flag and default `submit_batch()` fallback; compile hardware adapters unchanged.
3. Make `VirtualCanDriver` batch-aware on `vbus/tx`/`vbus/rx`; expand to the existing sink.
4. Add per-channel batching to `CanTransmitter`; keep the public `tx` service unchanged.
5. Update `nexigen-canvbus` loopback, heartbeat, external traffic and fault injection; run the virtual e2e and fault-seed tests.
6. Measure 10 000 frames/s CPU and decide whether a later change should batch the public `rx` path.

Rollback removes the batch path and restores `vbus/*` to `CanMessage`; no public API or persisted data needs migration.
