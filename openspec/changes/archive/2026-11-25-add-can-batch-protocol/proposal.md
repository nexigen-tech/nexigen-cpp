# Proposal

## Why

The virtual CAN demo now runs at about 10 000 frames/s, and each frame crosses five separate `com` publish-subscribe samples from sender to viewer. Profiling showed the resulting CPU cost is dominated by per-message transport work in `nexigen-cantx`, `nexigen-canvbus` and `nexigen-canrx`, not by the viewer. The sender-side 100-message burst reduced the sender's timer overhead but did not reduce the number of transport samples, so the receiver and bridge processes still process one sample per CAN frame.

## What Changes

- Add a fixed-size, trivially copyable `CanMessageBatch` envelope carrying a bounded array of `CanMessage` values, a count, and a per-channel logical `first_sequence`. Each contained `CanMessage` keeps its own timestamp, hardware sequence, kind, flags, origin and payload; batching must not rewrite or synthesize those values.
- Add `CanDriver::submit_batch()` with a default implementation that submits each message individually, so existing hardware adapters remain source-compatible. Add a `batch` capability flag to `CanDriverCapabilities`.
- Use batch transport only on the virtual-hardware links `vbus/tx` and `vbus/rx`. The public application-facing `rx`, `tx` and `status` services continue to carry the existing payload types and remain compatible with `CanClient`, `nexigen-canmon`, `nexigen-canview` and existing examples.
- Batch at `CanTransmitter`: collect up to 100 messages, or flush after 10 ms, and submit one batch to the driver. `CanReceiver` accepts a batch and expands it into the existing per-message `rx` publishing path.
- Make `nexigen-canvbus` batch-aware: receive batches on `vbus/tx`, preserve per-message timestamps and ordering, expand for fault injection/loopback, and publish batches on `vbus/rx`.
- Account for batch loss explicitly: a lost transport sample must expand to the number of messages derived from the next batch's `first_sequence` and the previous batch count, rather than reporting one lost CAN frame.
- Keep per-message hardware timestamps and sequence semantics unchanged; the 100 µs/10 ms demo timing remains a generator concern and is not baked into the transport contract.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `hw/can/bus`: the driver contract gains an optional batch submission path and a batch capability; `CanMessageBatch` preserves the existing `CanMessage` value and per-message metadata.
- `hw/can/virtual-bus`: virtual `vbus/tx` and `vbus/rx` may carry `CanMessageBatch`; loopback, ordering, heartbeat and fault-injection behavior must remain per-message equivalent.

## Impact

- **Code**: `include/nexigen/hw/can/` (`can_message_batch`, driver/capability declarations), `src/hw/can/com/` (`can_transmitter`, `can_receiver`, `virtual_can_driver`), `tools/nexigen-canvbus/main.cpp`, and the related unit/transport/e2e tests.
- **Public contract**: `rx`/`tx`/`status` and `CanClient` remain unchanged. `CanDriver` gains a non-pure-virtual `submit_batch()` and `CanDriverCapabilities::batch`; out-of-tree adapters continue to compile and fall back to per-message submission.
- **Performance**: one transport sample carries up to 100 messages. At 10 000 frames/s the hot virtual links move from 10 000 samples/s to about 100 batches/s; expected CPU reduction is largest in `cantx`, `canvbus` and the virtual-driver receive path. Batch collection adds at most one 10 ms flush interval (≈10 ms worst-case, ≈5 ms average) to the tx path. Batch payload is 112 B × 100 ≈ 11.2 KiB plus a small header; batch queues use a small bounded depth (target 16–32 samples) so per-subscriber memory stays comparable to the current message queues.
- **Memory/allocation**: `CanMessageBatch` is fixed-size and trivially copyable; batches are stored in preallocated SPSC rings and published zero-copy. No per-message heap allocation is introduced on the hot path.
- **Compatibility**: no migration of serialized session files, public CAN service names, `CanMessage` layout, `nexigen-canmon` flags or `nexigen-canview` session behavior is required. The batch path is opt-in at the virtual driver/capability level.
- **Testing**: unit tests for batch invariants and timestamp preservation, virtual transport/e2e tests with fault injection and batch loss accounting, and the existing Debug/Release/ASan/TSan matrices.
