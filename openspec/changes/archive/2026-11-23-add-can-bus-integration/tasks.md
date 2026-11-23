# Tasks

## 1. Extend `App<Policy>` to sixteen subscriptions

- [x] 1.1 Replace the single subscription slot and `EventConsumerSet<1>` in `include/nexigen/app/app.hpp` with a subscription vector and `EventConsumerSet<16>`, verify multiple subscriptions with different payload types are accepted and all callbacks run on the `run()` thread
- [x] 1.2 Reject a seventeenth subscription with a diagnostic; verify the first sixteen remain active and `run()` still gates on diagnostics
- [x] 1.3 Update `tests/app/app_runtime_test.cpp`: replace the second-subscription rejection expectation with acceptance, add 16/17-limit and mixed-payload-type tests, and keep the idle-stop, output-identity and double-shutdown coverage; verify `nexigen_app_runtime_com_tests` reports 0 failures
- [x] 1.4 Update the `App` Doxygen and `docs/articles/app.md` from one input to up to sixteen subscriptions with independent payload types; verify Doxygen reports no warnings for `include/nexigen/app/`


## 2. CAN module and vocabulary

- [x] 2.1 Add the `nexigen_hw_can` CMake target, `include/nexigen/hw/can/`, `src/hw/can/` and `tests/hw/can/` scaffolding; verify the empty target builds with `scripts/build.sh build-debug` and a placeholder unit test is discovered by CTest
- [x] 2.2 Add `CanMessageKind`, `CanTimestampSource`, `CanMessageOrigin`, `CanChannelState`, `CanErrorFlags` and `CanFrameFlags` with `to_string`/`from_string`, non-throwing JSON and `std::formatter`; verify each value round-trips through text and JSON and `std::format` in `tests/hw/can/can_vocabulary_test.cpp`
- [x] 2.3 Add the fixed-size `CanMessage` and `CanChannelStatus` values with the layout from design D2/D6; verify `static_assert` sizes, trivially-copyable checks, field round-trips and hostile-JSON handling
- [x] 2.4 Add `CanChannelConfig` and channel-name validation (`[a-z0-9_-]{1,32}`, unique names, 16-channel cap); verify valid, duplicate, invalid-name and over-cap cases return the documented statuses
- [x] 2.5 Add `CanChannelStatus` counter composition and state transitions; verify bus-off, warning, error-passive, recovery and sequence-gap counters in unit tests
- [x] 2.6 Add exhaustive Doxygen to every new `include/nexigen/hw/can/` header and verify `doxygen Doxyfile` reports no warnings for the module

## 3. Driver contract and test backends

- [x] 3.1 Add `CanDriverCapabilities`, `ICanMessageSink` and the vendor-neutral `CanDriver` contract from design D1; verify a compile-only fake implementation satisfies the contract without OS headers
- [x] 3.2 Add a deterministic `FakeCanDriver` that emits data, remote, FD, error, overload and heartbeat messages and records submissions; verify capability gating, open failure, submit backpressure and close idempotence
- [x] 3.3 Add a driver-stream test that preserves an arbitrary hardware timestamp, hardware sequence, error flags, origin and idle duration through the fake driver; verify the exact values reach the sink unchanged
- [x] 3.4 Verify the driver contract rejects FD on classic channels and remote on FD channels with `UnsupportedFormat`/`InvalidArgument` as specified
- [x] 3.5 Document in the driver header that the vendor adapter is out of scope for this change and verify the header contains no OS-specific include

## 4. Transport mapping and application client

- [x] 4.1 Add channel service-name derivation (`nexigen/hw/can/<channel>/rx|tx|status`) and CAN event-code constants; verify valid, duplicate and invalid names in tests
- [x] 4.2 Add the `CanMessage` to `EventTime`/`HardwareMetadata`/`EventHeader` mapping from design D3 and verify timestamps, hardware sequence, presence bits and transport sequence remain distinct
- [x] 4.3 Add `CanPublisher` and the `App<Policy>`-based `CanClient` with up to 16 channels, per-channel `rx` subscriptions, `tx` publishers and `status` subscriptions; verify one process can consume two channels and produce on two other channels
- [x] 4.4 Add the client diagnostics gate, missing-`--app-id` rejection, duplicate-channel rejection and idempotent shutdown; verify `run()` refuses to start after a failed publisher/subscription
- [x] 4.5 Verify App callbacks are serialized on the `run()` thread and only enqueue into bounded per-channel or display queues; verify no callback runs on a transport or driver thread and a slow callback does not lose queued messages
- [x] 4.6 Add loss, per-channel counters, status callbacks and publisher backpressure; verify counters are independent and a full publisher queue returns a busy status
- [x] 4.7 Add Doxygen to the client and transport headers and verify the COM-gated client test target builds

## 5. Driver-side bridge classes

- [x] 5.1 Add the fixed-capacity SPSC `CanMessageRing` with `alignas(64)` indices and `release`/`acquire` ordering; verify single-producer/single-consumer order, wraparound and full/empty behavior
- [x] 5.2 Add `CanReceiver` with one ring and one `EventProducer` per channel; verify a fake driver stream reaches the producers in order with all metadata preserved
- [x] 5.3 Add `CanTransmitter` with one bounded ring and one `EventConsumer` per channel; verify valid frames reach the fake driver and error/overload/heartbeat requests are rejected
- [x] 5.4 Add receiver heartbeat/error forwarding and status publication on state change and at the configured interval; verify a bus-off transition is published immediately
- [x] 5.5 Add overload policies (drop-newest default, drop-oldest option), drop counters and channel isolation; verify a full ring does not block the driver sink
- [x] 5.6 Add Doxygen to the bridge headers and verify the bridge unit tests pass under Debug and TSan

## 6. `nexigen-canrx` and `nexigen-cantx`

- [x] 6.1 Implement shared CAN tool CLI/config loading (`--channel`, `--config`, `--app-id`, `--backend hardware|virtual`, `--help`); verify help, missing id, invalid channel and hostile JSON tests
- [x] 6.2 Implement `nexigen-canrx` on `App<Policy>`: startup, driver open/rollback, receive loop, heartbeat/error forwarding and graceful signal shutdown; verify with `FakeCanDriver` and a one-channel smoke test
- [x] 6.3 Implement `nexigen-cantx` on `App<Policy>`: startup, tx subscriptions, bounded submission, rejection rules and graceful shutdown; verify with `FakeCanDriver` and a one-channel smoke test
- [x] 6.4 Verify one process handles one channel and all configured channels with identical command shapes, and that a second channel failure rolls back the first
- [x] 6.5 Add the `--backend virtual` wiring for both tools through `VirtualCanDriver`; verify the tool help and startup paths without hardware
- [x] 6.6 Document the tool invocations in `docs/articles/hw/can.md` and verify every documented command is accepted by `--help` or a smoke run

## 7. `nexigen-canmon`

- [x] 7.1 Implement `nexigen-canmon` on `App<Policy>` with a separate render thread and line-oriented output for every message kind, timestamp source, origin, identifier, flags, dlc and payload; verify golden output for data, remote, error, overload and heartbeat rows
- [x] 7.2 Implement channel, identifier/mask, kind, origin and errors-only filters with runtime changes; verify each filter against a scripted message sequence
- [x] 7.3 Implement JSONL output that round-trips `CanMessage` and `CanChannelStatus`; verify no field is lost and hostile input cannot throw
- [x] 7.4 Implement the bounded display queue, dropped-display counter and <= 10 ms render cadence; verify a slow renderer does not block the receive callback
- [x] 7.5 Implement multi-channel monitor startup, status display, transport-loss summary and graceful signals; verify a two-channel virtual run and exit code 0 on `SIGTERM`
- [x] 7.6 Document monitor usage and filters in `docs/articles/hw/can.md` and verify the documented JSONL command round-trips

## 8. `nexigen-canvbus` and `VirtualCanDriver`

- [x] 8.1 Add `nexigen-canvbus` on `App<Policy>` with multi-channel config, `--seed`, `--duration`, `--app-id` and the `vbus/rx`/`vbus/tx` services; verify two virtual channels start without hardware
- [x] 8.2 Add `VirtualCanDriver` implementing `CanDriver` over `vbus/rx`/`vbus/tx`; verify `canrx` and `cantx` can select `--backend virtual` without application-side changes
- [x] 8.3 Implement loopback with configurable latency/jitter and origin `LocalTransmit`; verify a transmitted frame reappears on `rx` with the original payload and timestamp policy
- [x] 8.4 Implement external/periodic frame injection with origin `ExternalDevice`; verify ordering and channel isolation
- [x] 8.5 Implement bus-idle heartbeat generation and suppression when traffic resumes; verify growing `idle_duration_ns` and no heartbeat while busy
- [x] 8.6 Implement deterministic fault injection (error, overload, bus-off, warning, passive, drop, duplicate, reorder, burst, jitter); verify the same seed produces the same sequence and the counters report each fault
- [x] 8.7 Add an end-to-end virtual-hardware test: control publisher -> `cantx --backend virtual` -> `canvbus` loopback -> `canrx --backend virtual` -> `canmon`/consumer; verify the frame, origin, timestamp, error and heartbeat paths
- [x] 8.8 Document virtual-bus scenarios and fault injection in `docs/articles/hw/can.md` and verify every documented scenario is reproducible with one command

## 9. Integration, documentation and gates

- [x] 9.1 Add CMake install/export entries for `nexigen_hw_can`, the optional COM client/bridge target and the four tools; verify an install tree contains the libraries, headers and executables
- [x] 9.2 Add the CAN module to the Mermaid module map and process/channel diagrams in `docs/architecture.md`; verify the diagrams name the driver side, application side, virtual hardware and all three standard services
- [x] 9.3 Add the CAN architecture article with the message model, timestamp/sequence/error semantics, multi-channel client and hardware-adapter guidance; verify the documented examples match the implemented CLI
- [x] 9.4 Run `cmake/check_naming.py`, `cmake/check_result_value.py`, `clang-format --dry-run --Werror` and `python3 cmake/check_complexity.py --threshold 15 include src tests tools`; verify every gate exits 0
- [x] 9.5 Run `openspec validate add-can-bus-integration --strict` and `openspec validate --specs`; verify both pass
- [x] 9.6 Run `graphify update .`; verify the graph rebuild completes without errors and includes the new CAN symbols

## 10. Full verification

- [x] 10.1 Build and run the full Debug and Release COM test suites: `scripts/build.sh build-debug build-release-com` followed by `ctest --test-dir build-debug --output-on-failure` and `ctest --test-dir build-release-com --output-on-failure`; verify 100% pass
- [x] 10.2 Build and run `build-asan-com` and `build-tsan-com` with `ctest --output-on-failure`; verify no ASan or TSan reports, including the ring-buffer and multi-channel client tests
- [x] 10.3 Build `build-verify-lto` and `build-verify-lat` and run their CTest suites; verify no latency or LTO regression
- [x] 10.4 Run the virtual-hardware CTest scenario and the monitor JSONL smoke test; verify the recorded frame stream contains data, error, overload and heartbeat kinds under the configured application id
- [x] 10.5 Run the optional `vcan` smoke test when the host supports it; when unsupported, verify the test reports a skip rather than a false pass

## 11. Verification follow-up

- [x] 11.1 Add `CanClient` dropped/loss counters, status-loss callback, `run()` registration guard, and test late registration/backpressure
- [x] 11.2 Publish `CanChannelStatus` from `CanTransmitter`/`nexigen-cantx`, including bus-off/recovery state and a shared two-publisher status service
- [x] 11.3 Normalize zero timestamps/origin in `CanTransmitter` and add bounded retry for driver backpressure
- [x] 11.4 Move `nexigen-canmon` to `CanClient`, report transport loss, and keep display-loss reporting
- [x] 11.5 Document `--backend hardware` as adapter-required in specs, usage text and `docs/articles/hw/can.md`
- [x] 11.6 Add deterministic fault-seed smoke, bridge rollback test and duplicate/drop/reorder/burst coverage
- [x] 11.7 Add `VendorCanDriver`, `nexigen-canrx-hw` and `nexigen-cantx-hw` hardware app skeletons, CMake targets and install entries

## Workflow follow-up

- Archive the change after review and a clean full verification run.
- Verify the archived main specs and the rebuilt graph.
