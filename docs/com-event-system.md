# Event system (`com/event-system`)

`nexigen::com` event delivery is a 1:N channel for software- and
hardware-originated events. It preserves a transport sequence for loss
detection, a producer epoch for restart reporting, a host ingest timestamp, and
optional FPGA metadata. The transport is iceoryx2, gated by
`NEXIGEN_ENABLE_COM=ON`.

## Channel model

```cpp
#include <nexigen/com/event_channel.hpp>

auto channel = nexigen::com::EventChannel<Telemetry>::create(node, service_name);
if (!channel) {
    // invalid ChannelConfig
}

auto producer = channel->producer();
auto consumer = channel->consumer(
    [](const nexigen::com::EventHeader& header, const Telemetry& value) {
        // header.sequence_id, header.event_code, header.timestamp_ns, ...
    },
    [](const nexigen::com::LossInfo& loss) {
        // loss.reason, loss.lost_count, loss.old_epoch/new_epoch
    });
```

`EventChannel<T>` is a factory: `producer()` creates only the publisher and
notifier, `consumer()` creates only the subscriber and listener. The default
configuration is 16 consumers, a 512-sample queue, and 16 history samples.

## Header

`EventHeader` is 64 bytes and is used as the custom iceoryx2 user header. It
contains:

- `sequence_id` — transport sequence, authoritative for loss detection;
- `timestamp_ns` + `timestamp_domain` — event time (for FPGA this is
  `Hardware`);
- `producer_epoch` — gateway/source lifecycle;
- `ingest_timestamp_ns` — host `CLOCK_MONOTONIC_RAW` at publication;
- `hardware_sequence`, `hardware_timestamp_ns`, `hardware_event_code` — optional
  FPGA metadata, each guarded by a flag;
- `event_code` — canonical platform code;
- `hardware_event_code` — raw FPGA code, separate from the canonical code.

`hardware_sequence` is trace-only. It is not used for loss or ordering because
an FPGA may omit it or produce it out of order.

## Publishing

```cpp
auto outcome = producer->publish_now(payload, platform_event_code);
if (!outcome) {
    // send failed; sequence was not consumed
} else if (*outcome == nexigen::com::PublishOutcome::NotifyFailed) {
    // sample is in shared memory; retry only the wake, do not republish
    producer->notify();
}
```

`publish_now` uses one host clock sample for both the event time and the ingest
time. `publish(payload, event_code, EventTime, HardwareMetadata)` is for
gateway/FPGA events and preserves the supplied hardware metadata.

`on_source_reset()` starts a new producer epoch and resets the transport
sequence to zero. The gateway calls it after quiescing its SPSC ring when the
FPGA or source lifecycle restarts.

## Loss handling

The consumer tracks the transport sequence:

- first sample establishes the baseline (history startup is not a loss);
- `seq > expected` reports `TransportGap` with an exact `lost_count`;
- `seq < expected` reports `Duplicate` and drops the sample;
- sequence zero with a larger expected value reports
  `UnexpectedSequenceReset` and recovers;
- an epoch change reports `ProducerRestart`.

`EventConsumerStats` exposes received, lost, duplicate, reset, restart, first
and last sequence counters.

## Waiting

```cpp
consumer->wait();  // single channel, no timeout/wake

nexigen::com::EventConsumerSet<16> set{node};
set.attach(*consumer);
set.wait_for(std::chrono::milliseconds{100});
set.wake_up();
```

`EventConsumer::wait()` blocks on its listener and drains all queued events. It
has no timeout or programmatic wake. `EventConsumerSet` owns one wait set, a
private wake pair, timeout support, and multiplexing. Attach/detach/wait run on
the waiting thread; a consumer is either standalone-waiting or set-attached.

## FPGA gateway

Recommended ingress:

```
FPGA descriptor -> ISR -> SpscRingBuffer<HardwareEvent<T>> -> gateway thread
                -> EventProducer::publish
```

The ISR must not call iceoryx2 or allocate. The driver converts FPGA ticks to
nanoseconds; the gateway preserves the hardware sequence/timestamp/event-code
and supplies the canonical `event_code`. Hardware timestamps are in the
`Hardware` domain and must not be compared with host time without a separate
calibration.

## Limits

- `max_consumers` is 16 in v1;
- `EventConsumerSet<MaxConsumers>` is fixed-capacity and allocation-free in the
  wait loop;
- the data and event services request 16 subscribers/listeners, one
  publisher/notifier, and `max_consumers + 1` nodes.
