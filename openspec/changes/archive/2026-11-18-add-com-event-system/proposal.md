# Proposal

## Why

The platform has zero-copy publish-subscribe and a multiplexed wait set, but no
event-delivery contract that combines 1:N fan-out, monotonic sequence numbers,
gap/restart reporting, and hardware-originated events. Applications currently
have to reconstruct loss detection and hardware metadata themselves, which
makes slow-consumer and FPGA-ingress behaviour inconsistent.

## What Changes

- Add the `com/event-system` capability: a 64-byte `EventHeader`, an
  `EventPayload` concept, `EventChannel<T>` as the application-facing factory,
  `EventProducer<T>`, `EventConsumer<T>`, `EventConsumerSet<MaxConsumers>`, and
  the supporting `ChannelConfig`, `EventTime`, `HardwareMetadata`,
  `EmptyPayload`, `PublishOutcome`, `WaitOutcome`, `LossInfo`, `LossReason`,
  `EventProducerStats`, and `EventConsumerStats` value types.
- Preserve FPGA-originated metadata: hardware sequence (trace-only), hardware
  timestamp in nanoseconds, and raw hardware event code alongside the canonical
  platform event code. The host ingest timestamp is always recorded separately.
- Define the gateway ingress contract: `FPGA -> ISR -> SPSC ring -> gateway
  thread -> EventProducer`, so ISR code never calls iceoryx2 and the gateway is
  the single publisher.
- Add `producer_epoch` lifecycle handling: the gateway owns the epoch and
  reports a source reset through `EventProducer::on_source_reset()`.
- Add `PublishSubscribeOptions` and `EventOptions` to `nexigen::com` and expose
  options on `Publisher`, `Subscriber`, `EventNotifier`, and `EventListener`
  for safe overflow, history, subscriber/publisher/node limits, and listener
  limits. Existing overloads keep their current behaviour.
- Limit `max_consumers` to 16 in v1; `EventConsumerSet<MaxConsumers>` is a
  template with a fixed-capacity array and no heap allocation in the wait loop.
- Add Doxygen contracts and COM-enabled unit/integration tests for sequence
  gaps, duplicate samples, unexpected resets, history startup, slow consumers,
  event multiplexing, wake-up, and hardware metadata preservation.

## Capabilities

### New Capabilities

- `com/event-system`: 1:N event delivery over iceoryx2 with sequence
  integrity, loss reporting, hardware-originated metadata, a bounded
  no-allocation producer path, a templated multiplexed consumer set, and an
  application-facing `EventChannel<T>` factory.

### Modified Capabilities

- `com/publish-subscribe`: port creation gains optional service settings for
  `max_subscribers`, `max_publishers`, `max_nodes`, `history_size`,
  `subscriber_max_buffer_size`, and `enable_safe_overflow`, plus matching event
  service settings (`max_listeners`, `max_notifiers`, `max_nodes`). Defaults and
  existing call sites remain unchanged.

## Impact

- **Affected code**: `include/nexigen/com/` (new event-system headers and
  concepts), `src/com/` (compiled definitions), `tests/com/` (new suites),
  `tools/` only if an example or benchmark is added later, and
  `docs/architecture.md` for the module map.
- **APIs**: additive `com` options; new `com::event_system` public surface. No
  existing `com` API is removed or re-signatured.
- **Dependencies**: iceoryx2 only, behind `NEXIGEN_ENABLE_COM`. No media, MCAP,
  FFmpeg, daemon, or network dependency is introduced.
- **Performance**: producer publish is one shared-memory loan/write/send plus
  one event notification, `O(1)` sequence accounting, and no heap allocation or
  blocking in the publish loop. Consumer drain is `O(records-in-queue)` with
  fixed state per consumer. The target workload is 1 kHz; `EventConsumerSet`
  waits in the kernel and does not poll.
- **Memory**: user header grows to 64 B; consumer state is fixed; the v1
  consumer limit is 16; history defaults to 16 samples and queue depth to 512.
- **Compatibility**: non-COM builds are unaffected. Existing `com` callers keep
  the transport defaults; event-system users opt into safe overflow, history,
  and the higher service limits explicitly.
