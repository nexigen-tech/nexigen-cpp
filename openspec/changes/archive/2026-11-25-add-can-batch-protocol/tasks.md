# Tasks

## 1. Batch value and vocabulary

- [x] 1.1 Add `include/nexigen/hw/can/can_message_batch.hpp` with the fixed 11 216-byte envelope (`first_sequence`, `channel_id`, `count`, reserved, `CanMessage[100]`), `max_messages`, `validate()` and `clear()`; verify size/alignment/trivial-copy `static_assert`s and bounds tests
- [x] 1.2 Implement `CanMessageBatch::to_string`, `std::formatter`, non-throwing `to_json`/`from_json` and ADL helpers; verify round-trip, hostile JSON and canonical formatting tests
- [x] 1.3 Document the public batch type and its per-message preservation rules with Doxygen comments; verify the header documentation builds without new warnings
- [x] 1.4 Add unit tests covering empty/oversized batches, channel mismatch, `first_sequence + count` overflow, mixed message kinds, distinct timestamps and hostile JSON; verify the new tests pass in the model test binary

## 2. Driver contract

- [x] 2.1 Add `bool batch_submission` to `CanDriverCapabilities` and include it in `to_string`, formatter and JSON; verify capability round-trip and formatting tests
- [x] 2.2 Add virtual `CanDriver::submit_batch(std::string_view, const CanMessageBatch&)` with a no-allocation per-message default fallback; verify a test driver without batching accepts a batch and reports the first per-message failure
- [x] 2.3 Update `CanDriver` and capability Doxygen comments and the `hw/can/bus` vocabulary tests; verify existing hardware adapter tests still compile without overriding `submit_batch`

## 3. Virtual driver batch path

- [x] 3.1 Change `VirtualCanDriver` virtual endpoints to `CanMessageBatch` on `vbus/tx` and `vbus/rx`, use the batch `ChannelConfig` queue/history limits, and set `batch_submission=true`; verify service creation and a one-message batch delivery test
- [x] 3.2 Expand received batches into the existing `ICanMessageSink::on_message()` in original order, validate channel/count and reject malformed batches without partial delivery; verify order, channel-mismatch and malformed-batch tests
- [x] 3.3 Track batch-level `first_sequence` loss and expose the missing-message count through diagnostics without double-counting hardware-sequence gaps; verify lost-batch and mixed hardware-sequence tests
- [x] 3.4 Add transport tests for multiple messages, mixed kinds, distinct timestamps, heartbeat batches and single-message compatibility; verify the virtual transport test suite passes

## 4. Transmitter batching

- [x] 4.1 Accumulate up to 100 messages or flush after 10 ms in the existing per-channel `CanTransmitter` worker, assign `first_sequence` per accepted message and use `submit_batch()`; verify size-trigger, timeout-trigger and shutdown-flush tests
- [x] 4.2 Preserve the existing `tx` public payload and `CanTransmitter::enqueue()` behavior while a driver without batch support falls back to per-message submission; verify `CanClient`/`CanPublisher` tests remain unchanged
- [x] 4.3 Add queue-full, driver-busy, retry and stop tests for batch submission, including the 10 ms latency ceiling and no heap allocation on the worker path; verify the hot-path guard test

## 5. Virtual bus loopback and faults

- [x] 5.1 Update `nexigen-canvbus` to publish and consume `CanMessageBatch` on `vbus/tx`/`vbus/rx`, assign a receive-stream `first_sequence`, and emit heartbeats/external frames as one-message batches; verify basic loopback and heartbeat tests
- [x] 5.2 Apply latency, jitter, drop, duplicate, reorder, burst, error, overload and bus-off injection per contained message while preserving order and splitting batches at fault boundaries; verify every injected fault and seed reproducibility
- [x] 5.3 Ensure `nexigen-canrx` expands batches into the standard per-message `rx` stream and `nexigen-canmon`/`nexigen-canview` require no source changes; verify the existing virtual e2e, fault-seed and viewer capture tests
- [x] 5.4 Add batch-specific acceptance tests for 100-message input, per-message timestamps, logical sequence gaps and mixed queued traffic; verify the acceptance script passes

## 6. Integration, performance and documentation

- [x] 6.1 Measure the 10 000 frames/s virtual demo before and after batching, recording per-process CPU, ingress queue depth and end-to-end frame visibility; verify the documented comparison and that no CAN messages are lost when queues are not overflowed
- [x] 6.2 Update `docs/architecture.md` Mermaid diagrams for the batched `vbus/*` path and the unchanged public `rx`/`tx` path; verify the documented build and demo commands run as written
- [x] 6.3 Run `graphify update .` and verify graph generation completes without errors
- [x] 6.4 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto` with latency gates), TSan (`-DNEXIGEN_ENABLE_TSAN=ON`) and ASan (`-DNEXIGEN_ENABLE_ASAN=ON`) using `scripts/build.sh -j 64`; verify all affected CTest labels pass
- [x] 6.5 Run the complexity gate, clang-format check and Doxygen build for touched headers; verify no new warnings and no function above CCN 15

## Workflow follow-up

- Review the measured CPU reduction with the user before deciding whether a later change should batch the public `rx` path.
- Archive the change after the project's review requirements are satisfied.
