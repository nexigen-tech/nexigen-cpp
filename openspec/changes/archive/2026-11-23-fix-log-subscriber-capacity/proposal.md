# Proposal

## Why

The log service is currently created with iceoryx2 defaults rather than the values already present in
`Iceoryx2LoggerConfig`: `max_publishers = 2`, `max_subscribers = 8`, `subscriber_max_buffer_size = 2`,
and `com` explicitly disables safe overflow. Two consequences block realtime log viewing:

1. A 2-slot subscriber queue covers about 40 us at 50 000 msg/s. A subscriber that misses one WaitSet
   wakeup (scheduler jitter, page fault, GC-less but still preemptible) fills it, `send_copy` returns
   `ReceiveBufferFull`, and the logger silently drops the frame; the loss is not in `dropped_counter`.
2. A third process with an `Iceoryx2Logger` cannot create a publisher at all
   (`ExceedsMaxSupportedPublishers`), because the service only allows two publishers.

The upcoming realtime log viewer targets 50 000 msg/s and must not lose frames because the transport
was sized for a two-application demo. This change makes the log service's quality of service explicit,
shared, and large enough for the platform, without changing the `LogFrame` wire format.

## What Changes

- Add `include/nexigen/logging/log_service_capacity.hpp` with COM-free shared constants and
  `include/nexigen/logging/log_service_options.hpp` with `log_service_options()` returning
  `nexigen::com::PublishSubscribeOptions`:
  - `max_subscribers = 4` (log daemon, recorder, viewer, one spare),
  - `max_publishers = 64` (one per logging application process),
  - `max_nodes = 64`,
  - `subscriber_max_buffer_size = 16384`,
  - `history_size = 4` (the existing config default; four frames are kept for late subscribers but no
    backlog is promised),
  - `enable_safe_overflow = true` (the transport evicts the oldest unread sample instead of failing the
    publisher).
- `detail::Iceoryx2Sink::start()` SHALL create its publisher with these options instead of
  `Publisher::create(node, name)`.
- `nexigen-logd`, `nexigen-recd`, and the future `nexigen-logview` SHALL create their log subscribers
  with the same options, so whichever participant creates the service first installs the same shape.
- Align `Iceoryx2LoggerConfig::max_subscribers` and `history_size` with the shared constants: an
  application SHALL NOT be able to require a service shape that a running daemon created with
  different values. The values stay in the config and in JSON for diagnostics, but the shared
  constants win when the service is opened or created.
- Add regression coverage: three publishers plus three subscribers on one service; a 16 384-frame
  burst without `ReceiveBufferFull`; overwrite-oldest reported as sequence gaps through
  `Sample::lost_before()`; and a clear startup error when an existing service carries incompatible
  options.
- No **BREAKING** API changes: `Publisher::create` and `Subscriber::create` already accept
  `PublishSubscribeOptions`; only the log service's choices change.
- Explicit non-goals: no `LogFrame` v3, no wire-format change, no history guarantee, no global change
  to the `com` default overflow policy. The `com` requirement "Non-blocking overflow" already
  specifies eviction of the oldest sample; the log service opts into that behavior explicitly.

## Capabilities

### New Capabilities

- None. The change uses the existing `com` options API and fixes the log service's use of it.

### Modified Capabilities

- `logging/iceoryx2-logger`: the logger SHALL create the log service with the shared capacity options
  (publisher count, node count, subscriber depth, safe overflow) and SHALL keep publishing without a
  `ReceiveBufferFull` failure when a subscriber lags.
- `logging/log-daemon`: the daemon, normally the first participant, SHALL open the log service with the
  same shared capacity options so late publishers and subscribers observe one service shape.

## Impact

- **Code:** new `include/nexigen/logging/log_service_capacity.hpp` (COM-free constants) and
  `include/nexigen/logging/log_service_options.hpp` (options factory);
  `include/nexigen/logging/detail/iceoryx2_sink.hpp`; `include/nexigen/logging/iceoryx2_logger_config.hpp`
  (default alignment/validation); `tools/nexigen-logd/main.cpp`; `tools/nexigen-recd/main.cpp`; the
  future `tools/nexigen-logview/`; `tests/logging/iceoryx2_logger_test.cpp` and
  `tests/logging/log_daemon_test.cpp`.
- **Performance and latency:** the producer hot path is untouched (no added instructions, 0 ns delta);
  the worker's publish path keeps one transport call per frame and no new allocation. The change moves
  an overflow from "failed send, frame lost" to "transport evicts the oldest unread sample", which is
  the existing `com` contract and does not add blocking.
- **Memory:** the service data segment is allocated per publishing process as
  `max_subscribers * (subscriber_max_buffer_size + subscriber_max_borrowed_samples) + history + loaned`
  samples. At 4 subscribers, 16 384 depth, and a `LogFrame` plus `TransportHeader` plus chunk header of
  roughly 350 bytes, that is about 23 MB of shared memory per publishing application. The viewer maps
  the segment read-only; its own footprint is unchanged.
- **Behavior:** a service created by an older binary keeps its old options until the last participant
  exits. Starting with an incompatible live service SHALL fail with a diagnostic naming the required
  and existing values instead of silently degrading.
- **Dependencies:** none new; `com::PublishSubscribeOptions` is already public and pinned by
  `com/publish-subscribe`.
- **Docs:** `docs/architecture.md` and the logging article gain a short table of the log-service
  options and the memory formula.
