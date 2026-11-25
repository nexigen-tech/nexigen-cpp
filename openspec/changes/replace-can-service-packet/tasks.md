# Tasks

## 1. Public packet contract

- [x] 1.1 Change `rx` and `tx` service payloads to `CanMessageBatch` and apply `batch_channel_config()` to their service endpoints; verify a one-message and a 100-message packet publish/open successfully
- [x] 1.2 Validate packet count, channel id, contained message ids and `first_sequence + count`; verify empty, oversized, mismatched-channel and malformed-message rejection tests

## 2. Publisher and receiver edges

- [x] 2.1 Batch `CanPublisher` sends into 100-message/10 ms packets while keeping `send(CanMessage)` source-compatible; verify `CanClient` publisher tests and backpressure behavior
- [x] 2.2 Batch `CanReceiver` ring output into packet publications while preserving per-message order and metadata; verify `nexigen-canrx` and receiver transport tests
- [x] 2.3 Preserve per-channel `first_sequence` and report packet loss as a number of CAN messages; verify sequence-gap and no-double-count scenarios

## 3. Transmitter and client integration

- [x] 3.1 Subscribe `nexigen-cantx` and `can_tool_common` to `CanMessageBatch` `tx` packets and enqueue every contained message; verify hardware and virtual sender tests
- [x] 3.2 Expand `CanClient` receive batches into the existing per-message callbacks and publish through batch-capable `CanPublisher`; verify `nexigen_hw_can_client_tests`
- [x] 3.3 Expand `nexigen-canview` capture batches into existing per-record ingest while preserving timestamps and loss handling; verify canview unit and GUI suites

## 4. Verification and documentation

- [x] 4.1 Update virtual e2e, fault-seed and heartbeat fixtures for packet payloads and assert a full batch reaches `canmon`; verify all three scripts pass
- [x] 4.2 Update `docs/architecture.md` and `docs/articles/hw/can.md` for public packet payloads and the breaking compatibility boundary; verify documented build/demo commands run
- [x] 4.3 Run `graphify update .` and verify graph generation completes without errors
- [x] 4.4 Compile and pass 100% of unit tests under Debug, Release, ASan and TSan using `scripts/build.sh -j 64`; verify affected CTest labels pass
- [x] 4.5 Run complexity, clang-format and Doxygen checks for touched CAN headers; verify no new warnings and no function above CCN 15

## Workflow follow-up

- Archive the change after the compatible A change has been archived and review is complete.
- Verify mixed-version endpoint rejection before release.
