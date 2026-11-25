# Tasks

## 1. Packet queue and admission

- [x] 1.1 Add a `CanBatchRing<Capacity>` alias beside `CanMessageRing` and replace `CanTransmitter::Channel`'s `CanMessageRing<1024>` with `CanBatchRing<16>`; verify the header compiles and `nexigen_hw_can_bridge_tests` still builds
- [x] 1.2 Implement `CanTransmitter::enqueue_batch` with count/channel/kind/FD/sequence validation, per-message normalization, one atomic `try_push`, and `dropped`/`rejected` counters in message units; verify bridge tests cover a valid 100-message packet, mismatched channel, invalid kind, empty packet and full queue
- [x] 1.3 Reimplement `CanTransmitter::enqueue` as a one-message packet wrapper without changing its signature or error codes; verify the existing single-message normalization and bounded-retry tests pass unchanged
- [x] 1.4 Keep `CanTransmitter::to_string()` and its `std::formatter` stable and extend the Doxygen comments (`/// @brief`, `@param`, `@return`) on the touched header; verify the transmitter diagnostic scenario and a clean Doxygen run

## 2. Packet submit path

- [x] 2.1 Rewrite `submit_loop`/`flush_batch` to pop one packet, use the bounded `submit` retry for `count == 1`, else call `driver_->submit_batch` once, and account `submitted_`/`failed` in message units; verify a `FakeCanDriver` test asserts one batch submission with matching count, order and metadata
- [x] 2.2 Verify driver backpressure and stop behaviour: a rejecting driver increments `failed` by the packet count, and `stop()` joins every channel worker without dropping an admitted in-flight packet mid-submit
- [x] 2.3 Update the transmitter section of the Mermaid diagram in `docs/architecture.md` and the sender notes in `docs/articles/hw/can.md` to show the packet ring and one-batch submit path; verify every documented command copy-pastes and runs

## 3. Producer integration

- [x] 3.1 Switch `nexigen-cantx` and `tools/detail/can_tool_common.hpp` from per-message loops to one `enqueue_batch` call per `tx` packet; verify `can_virtual_e2e.sh`, `can_heartbeat_smoke.sh` and the hardware/virtual sender targets build and pass
- [x] 3.2 Extend `tests/hw/can/can_bridge_test.cpp` and `tests/hw/can/can_vocabulary_test.cpp` for packet admission, full-queue drop accounting and packet-order preservation; verify `ctest -R 'nexigen_hw_can'` passes
- [x] 3.3 Update any test fixture that enqueues 100 single messages to use one packet, and keep one single-message case for API compatibility; verify the suite no longer depends on 10 ms transmitter coalescing

## 4. Integration and performance

- [x] 4.1 Compile and pass 100% of affected unit tests under Debug, Release (`-O3 -march=native -flto`), ASan (`-DNEXIGEN_ENABLE_ASAN=ON`) and TSan (`-DNEXIGEN_ENABLE_TSAN=ON`) with `scripts/build.sh -j 64`; verify `ctest -L 'hw|canview|logview'` passes in every build directory
- [x] 4.2 Run `python3 cmake/check_complexity.py`, `clang-format --dry-run --Werror` and Doxygen; verify no function above CCN 15 and no new warnings
- [x] 4.3 Re-run the 10 000 frames/s demo (`canvbus` + `canrx` + `cantx` + `nexigen_can_test_sender` + `canview`) and record the before/after CPU in `docs/architecture.md`; verify `cantx` is near one core and `canrx`/`canvbus` do not regress
- [x] 4.4 Run `graphify update .` after the code changes; verify the graph rebuild completes and `git diff --check` is clean

## 5. Wait-set activation consumption

- [x] 5.1 Add `consume_activation()` to `IEventConsumer` and implement it in `EventConsumer` and `SyncConsumer` with the non-blocking listener `try_wait()`; verify the COM targets compile
- [x] 5.2 Consume fired consumer and wake listener activations inside `EventConsumerSet::process_attachment`, report `Events` only when `received()` advanced, and report `WokenUp` only when the wake flag was set; verify `nexigen_event_system_tests` passes
- [x] 5.3 Add a fired-consumer and wake no-spin regression test that measures thread CPU over a 100 ms idle wait; verify both CPU checks stay below 50 ms
- [x] 5.4 Update the `com/event-system` spec delta, design decision D7 and the waitset notes in `docs/architecture.md`; verify `openspec validate --strict` is clean

## Workflow follow-up

- Archive this change only after `replace-can-service-packet` is archived, so the main `sender-app` spec and the packet contract are in sync.
- Verify mixed-version endpoints remain rejected before release.
