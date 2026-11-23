# Proposal

## Why

`nexigen-cpp` has no CAN ISO-11898 data path: applications can exchange events over iceoryx2, but they cannot ingest hardware CAN traffic, transmit frames, observe bus errors or heartbeats, and cannot exercise a CAN control path without physical hardware. HIL and ECU bring-up therefore need an external gateway and ad-hoc logging instead of a first-class, timestamped, multi-channel CAN contract.

## What Changes

- Add a COM-free `nexigen::hw::can` module with a unified `CanMessage` stream, per-channel configuration, channel state/counters, and a vendor-neutral `CanDriver` contract. The driver already supplies hardware timestamps in nanoseconds, hardware sequence numbers, error flags, and bus-idle heartbeats; the library preserves them unchanged.
- Model every CAN message kind explicitly: classic data, CAN FD data, remote, error, overload, and bus-idle heartbeat. `CanMessageKind` encodes CAN versus CAN FD and the remote/error/overload/heartbeat variants so invalid combinations cannot be constructed.
- Define the iceoryx2 data plane: one `rx`, one `tx` and one `status` service per uniquely named channel. `rx` carries the full driver stream including error and bus-idle messages; `status` carries channel state and counters.
- Extend `App<Policy>` from one subscription to up to sixteen subscriptions, with all callbacks dispatched on the `run()` thread. The existing single-subscription behavior stays valid, and the second subscription is no longer a diagnostic error.
- Add an application-side `CanClient` and build every CAN console application on `App<Policy>`: one process can consume `rx` from several channels and publish `tx` to several channels, and the driver-side bridge tools can run one process per channel or one process for all channels.
- Add four console applications:
  - `nexigen-canrx` receives the driver stream and publishes it on the channel `rx` services;
  - `nexigen-cantx` subscribes to the channel `tx` services and submits frames through the driver;
  - `nexigen-canmon` subscribes to `rx`/`status`, renders messages and errors in real time, and supports filters plus a JSONL mode;
  - `nexigen-canvbus` emulates a multi-channel CAN bus as virtual hardware, including loopback, periodic traffic, heartbeat generation, latency/jitter, drops, bus-off and error injection.
- Add `VirtualCanDriver`, which implements the same `CanDriver` contract over dedicated `vbus/rx` and `vbus/tx` services. `nexigen-canrx` and `nexigen-cantx` run unchanged with `--backend virtual`, and the application side stays identical to production.
- Add configuration and CLI support for multiple uniquely named channels, strict validation and diagnostics.
- Add unit, integration and end-to-end tests, including a virtual-hardware round trip through `canrx`/`cantx` to `canmon`; document the architecture and operating recipes.

## Assumptions

- The concrete hardware driver is supplied by the interface module and is out of scope for this change; this change defines its contract, the virtual backend, and compile-ready `nexigen-canrx-hw`/`nexigen-cantx-hw` skeletons around `VendorCanDriver`.
- v1 supports up to 16 channels per process. Larger deployments can run several clients or bridge processes.
- `nexigen-canmon` renders a line-oriented table in v1, with JSONL as a machine-readable alternative; an interactive TUI is deferred.
- Classic CAN and CAN FD are both modeled in v1; a channel configuration selects whether FD frames are accepted.
- `App<Policy>` dispatches all subscriptions on its `run()` thread. CAN callbacks therefore stay short and push work to bounded per-channel queues; the tools do not perform driver I/O or rendering inside the callback.

## Capabilities

### New Capabilities
- `hw/can/bus`: the unified `CanMessage` model, message kinds and formats, timestamp/sequence/error flags, heartbeat semantics, channel configuration, channel state/counters, and the vendor-neutral `CanDriver` contract.
- `hw/can/transport`: the iceoryx2 service naming, payload/header mapping, channel binding, bounded queues, loss reporting, and the status topic shared by all CAN applications.
- `hw/can/client`: the application-side multi-channel client that can consume `rx` and produce `tx` for several channels in one process.
- `hw/can/receiver-app`: `nexigen-canrx` behavior, driver-to-iceoryx2 forwarding, heartbeats/errors, overload policy, observability and shutdown.
- `hw/can/sender-app`: `nexigen-cantx` behavior, iceoryx2-to-driver transmission, multi-channel queues, error handling and shutdown.
- `hw/can/monitor-app`: `nexigen-canmon` behavior, real-time rendering, filters, loss reporting and exit semantics.
- `hw/can/virtual-bus`: `nexigen-canvbus` and `VirtualCanDriver`, which replace hardware and the real CAN bus for debug runs, including deterministic fault injection.

### Modified Capabilities
- `app/runtime`: `App<Policy>` accepts up to sixteen subscriptions, each with its own payload type, plus loss-callback and explicit-output-configuration overloads. All subscriptions are dispatched on the `run()` thread through a bounded `EventConsumerSet`.
- `com/event-system`: `ChannelConfig` carries a validated `max_publishers` limit (default 1) so the CAN status service can accept both bridge publishers.

## Impact

- **Code:** new `include/nexigen/hw/can/`, `include/nexigen/hw/can/detail/`, `src/hw/can/`, tests under `tests/hw/can/`, tools under `tools/nexigen-can*`, new CMake targets (`nexigen_hw_can`, optional COM client/bridge layer, four executables) and installation entries; `include/nexigen/app/app.hpp` and `tests/app/app_runtime_test.cpp` gain multi-subscription support.
- **APIs:** new `nexigen::hw::can` types, `CanDriver`, `CanClient` and `VirtualCanDriver`; `app::App<Policy>` keeps its existing signatures and gains multi-subscription behavior. Existing `nexigen::com` and `nexigen::chrono` APIs are unchanged; no existing API is removed.
- **Behavior:** CAN channels become observable and controllable through iceoryx2. Hardware and virtual runs use the same application-side topics; only the driver backend changes.
- **Dependencies:** no new third-party library; iceoryx2 remains the only transport. Hardware-specific SDKs stay behind the `CanDriver` contract.
- **Performance:** all configuration, channel opening and service creation is cold path. The receive hot path performs one bounded driver callback, one copy into the per-channel SPSC ring and one iceoryx2 loan per message: no heap allocation, no locks, no formatting and no JSON. Target driver-to-publish latency is p99 <= 100 us per message on the reference machine. `CanMessage` is 112 bytes; per channel the default ring is 4096 entries (448 KiB), the TX queue is 1024 entries (112 KiB), and the monitor backlog is 8192 display entries. With 16 channels the worst-case static footprint stays below 16 MiB. The monitor renders at a <= 10 ms cadence and never blocks the receive path.
- **Verification:** unit tests for every message kind, timestamp/sequence/error preservation, channel configuration and status; a fake driver for deterministic tests; a virtual-hardware end-to-end test through `canrx`/`cantx`/`canmon`; CLI/help and hostile-config tests.
