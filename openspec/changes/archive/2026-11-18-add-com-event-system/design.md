# Design

## Context

See `proposal.md` for motivation. The existing `nexigen::com` module already
provides zero-copy `Publisher<T, H>`/`Subscriber<T, H>`, `EventNotifier`/
`EventListener`, and a kernel-blocking `WaitSet`. What it lacks is a sequence
contract, loss/restart reporting, hardware-origin metadata, and service options
for safe overflow, history, and higher endpoint limits. The event system is
therefore a new capability inside `nexigen::com`, not a new transport.

Constraints:

- `NEXIGEN_ENABLE_COM=ON`; iceoryx2 only, no media/MCAP/FFmpeg.
- `-fno-exceptions -fno-rtti`, `core::Result`/`com::Result` error handling.
- No heap allocation or blocking in the publish loop after initialization.
- v1 consumer limit is 16; `EventConsumerSet<MaxConsumers>` stays templated so
  the limit can be raised later.
- The gateway is the only publisher; FPGA ingress is
  `ISR -> SPSC ring -> gateway thread -> EventProducer`.

## Goals / Non-Goals

**Goals:**

- 1:N event delivery with transport sequence, exact gap counts, duplicate and
  unexpected-reset reporting, and epoch-based restart reporting.
- Preserve FPGA hardware sequence, hardware timestamp, and raw hardware event
  code without pretending they are host time or transport sequence.
- Keep the application API small: `EventChannel<T>` as a factory, inline
  callbacks by default, `std::function` accepted as a callable.
- Bounded, allocation-free hot path and a kernel-blocking wait path.

**Non-Goals:**

- Host calibration of the FPGA clock in v1; hardware timestamps remain in the
  `Hardware` domain. The raw value is preserved so calibration can be added
  later without a header change.
- Hardware sequence loss detection; `hardware_sequence` is trace-only.
- More than 16 consumers in v1, or multi-publisher channels.
- A daemon, background converter, or network transport.

## Decisions

### D1 — Capability lives in `nexigen::com`

The event system is built on `com::Publisher`, `Subscriber`, `EventNotifier`,
`EventListener`, `WaitSet`, `Node`, `ServiceName`, and `com::Error`. A separate
`nexigen::ipc` module would either duplicate those wrappers or depend on `com`
for no isolation benefit. The new capability path is `com/event-system`.

### D2 — `EventHeader` is a 64-byte user header

`EventHeader` is the custom user header `H` of `Publisher<T, EventHeader>` and
`Subscriber<T, EventHeader>`. Exact layout:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 8 | `sequence_id` — transport sequence |
| 8 | 8 | `timestamp_ns` — canonical event time |
| 16 | 8 | `producer_epoch` — gateway/source lifecycle |
| 24 | 8 | `ingest_timestamp_ns` — host monotonic raw |
| 32 | 8 | `hardware_sequence` — optional, trace-only |
| 40 | 8 | `hardware_timestamp_ns` — optional, driver ns |
| 48 | 4 | `event_code` — canonical platform code |
| 52 | 4 | `hardware_event_code` — optional raw FPGA code |
| 56 | 2 | `timestamp_domain` |
| 58 | 2 | `flags` |
| 60 | 4 | `reserved` (zero in v1) |

`static_assert(sizeof(EventHeader) == 64)`, `alignas(8)`,
`std::is_trivially_copyable_v<EventHeader>`. Flags:

```
bit 0 has_payload
bit 1 has_hardware_sequence
bit 2 has_hardware_timestamp
bit 3 has_hardware_event_code
bit 4..15 reserved
```

`timestamp_domain` uses a `uint16_t`-backed enum to match the wire field:
`Unknown`, `MonotonicRaw`, `Monotonic`, `Realtime`, `Hardware`. For FPGA
events in v1, `timestamp_ns == hardware_timestamp_ns` and the domain is
`Hardware`; `ingest_timestamp_ns` is always host monotonic raw.

Alternative: 40-byte header without a reserved word and with a smaller epoch.
Rejected because the 64-byte layout keeps optional hardware metadata and future
calibration fields without re-packing the ABI.

### D3 — Service options are additive

Existing `Publisher::create` always selects `enable_safe_overflow(false)` and
does not set history or endpoint limits. Add:

```cpp
struct PublishSubscribeOptions {
    std::uint64_t max_subscribers{1};
    std::uint64_t max_publishers{1};
    std::uint64_t max_nodes{0};          // 0 = transport default
    std::uint64_t subscriber_max_buffer_size{0};
    std::uint64_t subscriber_buffer_size{0};
    std::uint64_t history_size{0};
    std::uint64_t subscriber_history_request{0};
    bool enable_safe_overflow{false};
};

struct EventOptions {
    std::uint64_t max_listeners{1};
    std::uint64_t max_notifiers{1};
    std::uint64_t max_nodes{0};
};
```

New overloads accept the options; existing overloads delegate with the old
defaults. Both endpoints must pass compatible service-level options because
iceoryx2 verifies settings when an existing service is opened. Event system
uses `max_subscribers = max_consumers`, `max_publishers = 1`,
`max_nodes = max_consumers + 1`, history 16, queue 512, safe overflow true, and
`max_listeners = max_consumers`.

Alternative: private `detail::EventChannel` that calls iceoryx2 directly.
Rejected because it creates a second service-creation path and duplicates
`Sample`/`SampleMut` and error mapping.

### D4 — `EventChannel<T>` is a factory, not a service owner

```cpp
template <concepts::EventPayload T>
class EventChannel {
public:
    static Result<EventChannel> create(Node& node, ServiceName name,
                                       ChannelConfig config = {});

    Result<EventProducer<T>> producer();
    template <class OnEvent, class OnLoss>
    Result<EventConsumer<T, OnEvent, OnLoss>> consumer(OnEvent, OnLoss);

    const ServiceName& name() const noexcept;
    const ChannelConfig& config() const noexcept;
};
```

The channel stores a `Node&`, a `ServiceName`, and a copy of `ChannelConfig`.
`create()` validates the configuration and creates no service itself.
`producer()` creates only the publisher and notifier; `consumer()` creates only
the subscriber and listener.
This avoids consuming subscriber/listener slots for the unused role.

`ChannelConfig` defaults: `max_consumers = 16`, `queue_size = 512`,
`history_size = 16`, `default_timestamp_domain = MonotonicRaw`. Validation
rejects `max_consumers` outside `1..16`, zero queue, and history greater than
queue.

### D5 — Payload and callback contracts

`EventPayload<T>` is a named concept equal to `com::concepts::Message<T>`,
which is exactly `std::is_trivially_copyable_v<T>`. The event-specific name
gives the event system a place to add constraints later without changing every
signature. `EmptyPayload` satisfies the concept.

Callbacks are template parameters stored inline:

```cpp
template <concepts::EventPayload T, class OnEvent, class OnLoss>
class EventConsumer;
```

`EventChannel::consumer` deduces the callback types, so the application writes
`auto consumer = channel.consumer(on_event, on_loss)`. `std::function` is
accepted because it is a callable type; it is not a separate code path. Inline
callbacks avoid type erasure and registration allocation. `EventConsumerSet`
stores `IEventConsumer*`, so heterogeneous callback types coexist in one set.

### D6 — Producer epoch and source reset

`EventProducer::create` generates a nonzero epoch from a cold unique source
(realtime nanoseconds, process id, and a process-wide counter).
`on_source_reset()` generates a new epoch and resets the transport sequence to
zero. It is called only on the publishing thread, after the gateway has
quiesced the SPSC ring. It does not publish anything itself.

`sequence_id` advances only after a successful `send()`. `publish()` returns
`Result<PublishOutcome>`: `Notified` when the sample was sent and notified,
`NotifyFailed` when the sample was sent but the event notification failed. The
error arm is reserved for send failure. `notify()` retries the wake without
republishing. `stats().notify_failures` and `last_notify_error` retain the
diagnostic.

`max_publishers = 1` prevents a second writer from attaching with a different
epoch. A gateway restart produces a new epoch because a new `EventProducer` is
created. An FPGA reset is visible only if the gateway calls `on_source_reset()`;
hardware sequence is not used to infer it.

### D7 — Loss model

The consumer state machine is:

```
first sample: first_sequence = seq; expected = seq + 1; no loss callback
seq == expected: deliver; expected++
seq > expected: TransportGap(lost = seq - expected); expected = seq + 1; deliver
seq < expected, seq != 0: Duplicate; drop; expected unchanged
seq == 0, expected > 1, same epoch: UnexpectedSequenceReset; expected = 1; deliver
epoch changed: ProducerRestart; expected = seq + 1; deliver
```

`history_skipped = first_sequence` for the first epoch; a late join with history
does not report a loss callback for events before the history window.
`LossInfo` contains only the reason, expected/actual sequence, exact lost count
(only for `TransportGap`), old/new epoch, and the actual event timestamp/domain.
FPGA metadata stays in `EventHeader`, which the event callback receives.

### D8 — Wait model and consumer-set ownership

`EventConsumer` does not own a `WaitSet`. It exposes:

```cpp
Status drain() noexcept;  // non-blocking drain
Status wait() noexcept;   // EventListener::blocking_wait, then drain
```

`EventConsumer::wait()` has no timeout and no `wake_up`; managed stop,
timeout, and multiplexing are provided by `EventConsumerSet`. A consumer is
either standalone-waiting or attached to a set, never both. `EventConsumerSet`
marks the attachment state and `EventConsumer::wait()` returns `InvalidState`
while attached. This avoids the iceoryx2 `AlreadyAttached` conflict: a listener
can be attached to only one wait set.

`EventConsumerSet<MaxConsumers>`:

- `static_assert(1 <= MaxConsumers <= 16)` in v1;
- fixed `std::array<Slot, MaxConsumers>` with no heap allocation;
- one `com::WaitSet`, created without signal handling;
- one private wake pair (`EventNotifier`/`EventListener`) with
  `max_listeners = 1`, `max_notifiers = 1`, `max_nodes = 1`;
- constructor `EventConsumerSet<MaxConsumers> set{node}` stores `Status`; the
  API exposes `status()` and `operator bool`;
- `attach`/`detach`/`wait` on one thread;
- `wait()` returns after at least one event or wake; `wait_for(timeout)`
  returns `Events`, `Timeout`, or `WokenUp`; simultaneous event+wake yields
  `WokenUp`;
- `create()` checks `waitset.capacity() >= MaxConsumers + 1` (the wake slot) and
  returns `OutOfSamples` when the reactor cannot hold the attachments.

Alternative: one `WaitSet` per consumer for `wait_for(timeout)`. Rejected in v1
because it multiplies epoll descriptors and conflicts with set attachment; the
consumer-set is the single managed wait path.

### D9 — Hardware gateway ingress

```
FPGA descriptor { hardware_sequence?, hardware_timestamp_ns, event_code,
                  payload }
        |
        v
ISR: push into SpscRingBuffer<HardwareEvent<T>, 1024>  (no iceoryx2)
        |
        v
gateway thread: pop -> EventTime + HardwareMetadata -> EventProducer
```

The ring element is trivially copyable and carries the optional hardware fields
plus `T`. Indices are cache-line separated by the existing SPSC implementation;
the ISR push is wait-free and allocation-free. The driver converts FPGA ticks to
nanoseconds; the gateway does not re-scale them in v1.

`EventProducer::publish(payload, event_code, EventTime, HardwareMetadata)`
copies the hardware sequence, timestamp, and raw event code with their presence
flags. `hardware_sequence` is never used for loss or ordering; the transport
sequence is authoritative. On ring overflow the gateway can count drops, but
exact hardware loss is visible only if the source supplies a reliable sequence;
with trace-only sequence it is not.

### D10 — Timestamp and serialization

`publish_now` uses one host clock sample for both `timestamp_ns` and
`ingest_timestamp_ns`; `timestamp_domain` is the channel default. `publish`
uses the explicit `EventTime` for `timestamp_ns` and samples the host clock only
for `ingest_timestamp_ns`.

All public enums get `to_string`/`from_string`, non-throwing JSON, and a
formatter. `EventHeader`, `ChannelConfig`, `EventTime`, `HardwareMetadata`,
`LossInfo`, `EventProducerStats`, and `EventConsumerStats` get `to_string`,
formatter, and non-throwing JSON. Runtime classes get `to_string` and formatter
diagnostics; they are not serializable configuration values.

### D11 — Deterministic fault injection stays test-only

The real transport cannot be made to fail `loan()`, `send()`, or `notify()` on
demand, so the fault paths are exercised through `detail::EventProducerFaults`,
compiled only when `NEXIGEN_EVENT_SYSTEM_TESTING` is defined for
`nexigen_event_system_tests`. Production builds never see the flags, and the
hot path keeps its unconditional `publisher_.send()`/`notifier_.notify()` calls.

## Risks / Trade-offs

- **Hardware and host time are not comparable** → Mark FPGA timestamps as
  `Hardware`; use `ingest_timestamp_ns` only for host-path latency; document
  that cross-domain comparison requires future calibration.
- **Gateway misses an FPGA reset** → `producer_epoch` changes only when the
  gateway calls `on_source_reset()`; document the integration duty and test the
  reset path.
- **Old ring entries after a reset** → Gateway must quiesce the ring before
  `on_source_reset()`; otherwise stale events inherit the new epoch.
- **Notify failure leaves consumers asleep** → `PublishOutcome::NotifyFailed`,
  `notify()` retry, and `notify_failures` stats; consumers can also use
  `wait_for`.
- **Service option mismatch** → Both endpoints must pass the same
  `ChannelConfig`; iceoryx2 rejects incompatible opens.
- **64 consumers deferred** → v1 hard cap is 16; `EventConsumerSet` remains
  templated so the limit can rise without an API change.
- **WaitSet wake slot** → `MaxConsumers + 1` attachments; constructor checks
  reactor capacity and fails cleanly.
- **Single-thread producer** → `EventProducer` is not internally synchronized;
  `on_source_reset()` and `publish()` must run on the gateway thread.
- **Payload copy** → `Publisher::send_copy`/`write_payload` copies the payload
  once into shared memory; this is the existing zero-copy transport semantics,
  not an extra serialization step.
- **Callback lifetime** → The payload reference passed to `EventCallback` is
  valid only during the callback, while the sample lease is held.

## Migration Plan

No existing behavior is removed. The `com` option overloads are additive and
preserve current defaults, so existing publishers, subscribers, notifiers, and
listeners keep working. The event system is a new capability behind
`NEXIGEN_ENABLE_COM=ON`; non-COM builds are untouched. Rollback is deletion of
the new headers/tests and the option overloads.

## Open Questions

None blocking. Two items are deliberately deferred without changing this
design: host calibration of FPGA time (would set `timestamp_ns` to
`MonotonicRaw` and keep `hardware_timestamp_ns` raw), and raising the consumer
limit above 16.
