# Design

## Context

See `proposal.md` - Why. The approach builds on the existing project infrastructure:

- `nexigen::com` provides typed event channels, `EventProducer::publish` with `EventTime` and `HardwareMetadata`, `EventConsumerSet<N>`, bounded `ChannelConfig`, loss reporting and service names.
- `com::EventHeader` already carries an optional canonical timestamp, native ticks, hardware sequence, hardware event code and application id.
- `nexigen::app::Infrastructure` owns the time base, logging, metrics, tracing and signal handling; `App<Policy>` currently owns one input subscription and a bounded `EventConsumerSet<1>`, which is the only reason it cannot host a multi-channel CAN application as-is.
- The project forbids exceptions, RTTI and hot-path allocation. All failures use `core::Result`/`core::Status`, and concurrent state uses explicit atomics with bounded preallocated storage.

The CAN module is part of the hardware layer: headers live under `include/nexigen/hw/can/`, sources under `src/hw/can/`, tests under `tests/hw/can/`, the namespace is `nexigen::hw::can`, and the CMake target is `nexigen_hw_can`. The tools stay under `tools/nexigen-can*` because they are executables, not library headers.

## Goals / Non-Goals

**Goals:**

- One vendor-neutral CAN contract that preserves the driver's hardware timestamps, sequences, error flags and heartbeat stream.
- One unified message type that can represent every CAN/CAN FD frame kind plus driver heartbeats and errors.
- A multi-channel `App<Policy>` with up to sixteen subscriptions, used directly by every CAN console application.
- A multi-channel application-side client where one process can consume several `rx` channels and produce several `tx` channels.
- A virtual-hardware mode that exercises the real `canrx`/`cantx` applications and the standard application-side topics.
- Deterministic, allocation-free receive and transmit paths with bounded queues and explicit overload behavior.

**Non-Goals:**

- Implementing a concrete vendor hardware driver in this change. The driver contract and the virtual backend are in scope; the hardware adapter follows when the module API is known.
- Replacing `App<Policy>` or changing existing `com`/`app` requirements.
- CAN gateway routing, signal decoding (DBC), CANopen/J1939 interpretation, or diagnostic protocols.
- A full interactive TUI for the monitor; v1 is line-oriented with JSONL.
- More than sixteen channels per process in v1.

## Decisions

### D1 - Vendor-neutral `CanDriver`, not an OS CAN adapter

`CanDriver` is an abstract contract with `capabilities()`, `open_channel()`, `submit()` and `close_channel()`. Messages arrive through an `ICanMessageSink` callback owned by the driver. The contract names no operating-system API; a hardware adapter wraps the interface module and `VirtualCanDriver` wraps iceoryx2.

Alternative rejected: build directly on SocketCAN. It would couple the library to Linux, contradict the module's existing driver, and make the hardware path untestable without `vcan`.

### D2 - One `CanMessage` value with an explicit kind

The message model is a single fixed-size value, not a hierarchy and not separate data/error/heartbeat types:

```text
CanMessage (112 bytes, alignas(8), trivially copyable)
 0   u64 hardware_timestamp_ns
 8   u64 hardware_sequence
16   u64 idle_duration_ns
24   u32 can_id
28   u32 error_flags
32   u16 frame_flags        (BRS, ESI, presence bits)
34   u16 channel_id
36   u8  kind               (ClassicData, FdData, Remote, Error, Overload, BusIdle)
37   u8  dlc                (0..8 classic, 0..64 FD)
38   u8  timestamp_source   (Hardware, DriverFallback, Simulated)
39   u8  origin             (LocalTransmit, ExternalDevice, Unknown)
40   u8  data[64]
104  u8  reserved[8]
```

`kind` encodes CAN versus CAN FD and the remote/error/overload/heartbeat variants. `frame_flags` carries FD-specific BRS/ESI and presence bits. This makes invalid states (`Remote` + FD) impossible by construction and keeps one ordered stream for the driver's frames, errors and heartbeats.

Alternative considered: a `CanFrame` plus separate `CanError` and `CanHeartbeat` types. Rejected because the driver delivers one ordered stream and separate topics would reorder heartbeats relative to frames.

### D3 - Driver metadata is authoritative

The bridge never converts timestamps or renumbers sequences:

- `CanMessage.hardware_timestamp_ns` is copied unchanged from the driver.
- `CanMessage.hardware_sequence` is copied unchanged; `EventHeader.sequence_id` remains the transport sequence.
- `error_flags` and `origin` are copied unchanged.
- `timestamp_source` records whether the driver supplied hardware, fallback or simulated time.

Transport mapping uses `EventTime{hardware_timestamp_ns, TimestampDomain::Hardware}` and `HardwareMetadata{has_sequence, sequence, has_ticks, native_ticks = hardware_timestamp_ns, has_canonical, canonical_ns = hardware_timestamp_ns, domain_id}`. For fallback or simulated time the domain becomes `MonotonicRaw`. The mapping is one function so no application reimplements it.

### D4 - Per-channel services and channel binding

Each channel owns three services:

```text
nexigen/hw/can/<channel>/rx      CanMessage stream (all kinds)
nexigen/hw/can/<channel>/tx      CanMessage transmission requests
nexigen/hw/can/<channel>/status  CanChannelStatus
```

The channel name is validated to `[a-z0-9_-]{1,32}` and is part of the service name. `CanMessage.channel_id` is a numeric guard: a consumer subscribes by name but rejects a payload whose id does not match its configured channel. This catches misrouted or misconfigured publishers without putting a variable-length string in the hot-path payload.

The status service is created with `ChannelConfig::max_publishers = 2` because both the receiver and transmitter bridges publish channel health. `ChannelConfig::max_publishers` is a validated public limit with default 1.

### D5 - Extend `App<Policy>` to sixteen subscriptions

`App<Policy>` keeps its existing public shape but replaces the single subscription slot and `EventConsumerSet<1>` with a `std::vector<std::unique_ptr<SubscriptionBase>>` and `EventConsumerSet<16>`. `subscribe<T>` appends a type-erased subscription, accepts a second and later subscription with its own payload type, and rejects the seventeenth with a diagnostic. No type token or RTTI is needed because each subscription keeps its own type-erased consumer and callback.

All callbacks still run on the `run()` thread. This deliberately keeps the existing lifecycle, signal handling and diagnostics gate and avoids a second application runtime next to `App`. The trade-off is head-of-line blocking between subscriptions; CAN callbacks therefore only enqueue into bounded per-channel queues or the display queue and never perform driver I/O or rendering.

Alternative rejected: give `CanClient` its own multi-input runtime. It would duplicate `App`'s bootstrap, time base, signal handling and shutdown behavior, and the tools would stop using the platform facade the project is standardizing on.

### D6 - Driver-side bridge and application-side client are separate layers

```text
driver side                          application side
+----------------------+             +---------------------------+
| canrx / cantx        |  iceoryx2   | CanClient / canmon / user |
| one process per      |<----------->| one process, N channels,  |
| channel or all       |             | mixed rx and tx           |
+----------------------+             +---------------------------+
```

- `CanReceiver` / `CanTransmitter` own a `CanDriver`, a per-channel ring and `EventProducer`/`EventConsumer` endpoints.
- `CanClient` is a CAN-specific facade over `App<Policy>`: it adds channel binding, event-code selection, status subscriptions and per-channel counters while `App` owns the lifecycle and the subscription set. It never opens a driver. `nexigen-canmon` is built on `CanClient`, not directly on `App`.
- `nexigen-canrx`/`nexigen-cantx` are thin tools over the bridge classes and accept either one channel or a channel list; both use `App<Policy>` for bootstrap, telemetry, signals and publishing.
- `CanClient` is the shared building block for `nexigen-canmon` and user applications. It supports up to 16 channels, each with an optional `rx` subscription, `tx` publisher and `status` subscription. All CAN callbacks are dispatched on the App `run()` thread and therefore enqueue work instead of blocking.

Alternative rejected: a separate multi-input runtime for `CanClient`. `App` now covers the required subscription count, so a second runtime would duplicate lifecycle and identity behavior.

### D7 - Thread and memory model

- **Receive ring:** per channel, SPSC, `std::array<CanMessage, 4096>` (448 KiB). The driver callback is the only producer; the publishing thread is the only consumer.
  - producer publishes `head` with `std::memory_order_release`, consumer reads it with `acquire`;
  - consumer publishes `tail` with `release`, producer reads it with `acquire`;
  - counters are `std::atomic<uint64_t>` updated with `relaxed`.
  - `head` and `tail` are `alignas(64)` to keep the two cache lines apart.
  - full ring drops the newest message by default and increments `dropped`; a per-channel policy may select drop-oldest.
- **Transmit ring:** per channel, SPSC, 1024 entries (112 KiB); the iceoryx2 consumer callback is the producer, the driver-submit thread is the consumer. Full ring returns `BufferFull` and increments `dropped`.
- **Monitor:** a bounded display ring of 8192 `DisplayEntry` values; the render thread drains it at <= 10 ms. When full, the oldest display entry is dropped and `display_dropped` increases.
- **Publisher queue:** each `CanPublisher` owns a bounded 1024-entry SPSC queue and one publish worker. `send()` enqueues and returns `BufferFull` when the queue is full; the worker performs the actual iceoryx2 publication. A successful `send()` therefore means "accepted by the queue", not "already on the wire"; actual failures are reflected in `CanPublisher::stats()`.
- **Threads:** one App `run()` thread dispatches all subscriptions; `canrx` adds one receive/publish worker per channel, `cantx` adds one tx-submit worker per channel, and `canmon` adds one render thread. Up to 16 channels means at most 17 long-lived workers plus the App thread; channels never share a receive ring. App callbacks only copy into bounded queues, so head-of-line blocking is bounded by callback work, not by driver I/O or rendering.
- **No hot-path allocation or locking:** no `std::mutex`, no `std::function` per message, no formatting, no JSON. Callbacks are stored at registration time.

`CanChannelStatus` is a cold-path value (state, tx/rx error counters, bus-off count, error-frame count, rx/tx counts, drops, sequence gaps, last error flags, timestamp). It is copied into a status event on change or at the configured interval.

### D8 - Virtual hardware is a second `CanDriver`

`nexigen-canvbus` simulates hardware and the bus. `VirtualCanDriver` implements `CanDriver` over two dedicated service families:

```text
nexigen/hw/can/<channel>/vbus/rx   virtual driver stream -> canrx
nexigen/hw/can/<channel>/vbus/tx   cantx -> virtual bus
```

`nexigen-canrx --backend virtual` and `nexigen-cantx --backend virtual` use `VirtualCanDriver`; the standard `rx`/`tx`/`status` topics and all application-side code stay unchanged. `nexigen-canvbus` performs loopback, external injection, heartbeat generation and fault injection.

Alternative rejected: make the virtual bus publish the standard `rx` directly (mode A). It is simpler but bypasses the real bridge applications, so it would not test the production path and would risk duplicate `rx` publishers if a real receiver also ran.

### D9 - Error and heartbeat semantics

- Data and remote messages carry the driver's accumulated receive error flags.
- Error and overload messages are explicit kinds with `error_flags`; they are never disguised as data.
- `BusIdle` messages carry `hardware_timestamp_ns` and `idle_duration_ns`; they are published on `rx` in driver order and carry no CAN identifier or payload.
- `CanChannelStatus` is the only place for state transitions and counters; the monitor renders both status and per-message errors.

### D10 - Configuration and CLI

One JSON shape is shared by the tools:

```json
{
  "channels": [
    {
      "name": "can0",
      "channel_id": 1,
      "driver_channel": "can0",
      "bitrate": 500000,
      "fd": false,
      "rx_ring": 4096,
      "tx_queue": 1024,
      "status_interval_ns": 1000000000,
      "heartbeat_interval_ns": 1000000000
    }
  ]
}
```

`--channel name=...` is the single-channel shortcut. `--config` loads the JSON. All tools require `--app-id`; `canrx`, `cantx` and `canvbus` also accept `--backend hardware|virtual`, and `canmon` accepts `--json`, `--duration`, filters and `--channel`.

### D11 - Failure and shutdown behavior

- Creation collects cold-path diagnostics; `run()` refuses to start when any channel, service or driver open failed.
- A failed `open_channel` closes all already opened channels and releases their services.
- `SIGINT`, `SIGTERM` and `SIGHUP` are graceful stops; telemetry drains; exit code follows `app::exit_code`.
- A full receive ring never blocks the driver callback; a full transmit ring returns `BufferFull`.
- Bus-off does not silently restart unless the channel configuration enables recovery; the status stream always reports the state.

## Risks / Trade-offs

- **Drive-by-wire latency and driver callback jitter** -> the receive callback only copies into a bounded ring; publishing runs on a separate worker per channel. No formatting, allocation or locks in the callback.
- **Driver metadata may be incomplete** -> `timestamp_source` and presence bits make fallback visible; the bridge never invents a hardware timestamp or sequence.
- **Heartbeats mixed with frames can surprise consumers** -> the kind is mandatory and specs require kind filtering; the monitor and client expose it explicitly.
- **App callbacks are serialized** -> CAN callbacks only enqueue into bounded per-channel or display queues; the tools perform driver I/O and rendering on their own workers, and user `CanClient` callbacks must stay short.
- **Virtual mode is not a real-time bus model** -> it is a debug facility. Latency/jitter and fault injection are deterministic and configurable, not a certification model.
- **Sixteen-channel cap** -> documented; larger deployments run several clients or bridge processes.
- **Hardware adapter is out of scope** -> the contract is fixed and tested with a fake driver and the virtual backend; the vendor adapter is a separate change.
- **Static memory grows with channels** -> defaults are per-channel bounded; 16 channels stay below 16 MiB and the cap is configurable downward.

## Migration Plan

1. Extend `App<Policy>` to sixteen subscriptions, then update and extend `tests/app/app_runtime_test.cpp`.
2. Add `nexigen_hw_can` (COM-free types, validation, fake driver, virtual message helpers) and its unit tests.
3. Add the COM adapter (`CanClient`, transport mapping) behind `NEXIGEN_ENABLE_COM` and test it against an in-process fake channel pair.
4. Add `nexigen-canrx`/`nexigen-cantx` with the hardware contract and the virtual backend.
5. Add `nexigen-canvbus` and the end-to-end virtual-hardware test.
6. Add `nexigen-canmon` and the CLI/JSONL tests.
7. Add documentation, CMake install entries and the optional hardware-adapter guidance.

No existing API is removed; `App<Policy>` gains multi-subscription behavior and the CAN module is additive. Rollback is deleting the new targets and reverting the App subscription storage.

## Open Questions

- The exact vendor driver API (callbacks, threading, error codes) is unknown; the contract is designed to wrap it without changing the applications. `nexigen-canrx-hw`/`nexigen-cantx-hw` and `VendorCanDriver` are compile-ready skeletons for that adapter.
- A future interactive TUI for the monitor can replace the line renderer without changing the transport or client contracts.
- The sixteen-channel process cap may be raised after measuring thread and memory footprints on the reference machine.
