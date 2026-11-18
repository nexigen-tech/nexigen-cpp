# Tasks

## 1. Module scaffold and build integration

- [x] 1.1 Create `include/nexigen/synchro/` and `src/synchro/` with the planned headers and CMake target `nexigen_synchro` gated by `NEXIGEN_ENABLE_COM`; verify configure succeeds with COM OFF and ON.
- [x] 1.2 Register `nexigen_synchro` in the install/export set and add `nexigen_synchro_tests`; verify a clean COM-enabled configure/build creates both targets.
- [x] 1.3 Add the module to `docs/architecture.md` dependencies and record the target's COM gate; verify the documented build command produces the library.

## 2. Protocol types and serialization

- [x] 2.1 Add `SyncPayload` with the 32-byte layout and `static_assert` size/alignment/trivial-copy checks; verify the layout unit test fails on field drift.
- [x] 2.2 Add `SyncChannelConfig`, `SyncConfig`, and `SyncConsumerConfig` with validation for positive period, positive phases, 1 ms minimum phase period, spin threshold, and channel limits; verify valid/invalid configuration tests pass.
- [x] 2.3 Add `StartMode`, `PastTargetPolicy`, `StartPolicy`, `LateEventPolicy`, `SyncState`, and `SyncConsumerState`; verify `to_string`, `from_string`, JSON, and formatter tests pass for every enumerator.
- [x] 2.4 Add `SyncErrorKind`, `SyncError`, `SyncResult`, and `SyncStatus`; verify transport, metrics, clock, eventfd, and lifecycle cause fields round-trip and hostile JSON leaves the destination unchanged.
- [x] 2.5 Add `SyncGeneratorStats`, `SyncConsumerStats`, and `SyncContext`; verify deterministic `to_string`, formatter, JSON round-trip, and `static_assert` layout checks pass.
- [x] 2.6 Document every new public type and helper with Doxygen `@brief`, `@tparam`, `@param`, `@return`, and OpenSpec `@note`; verify `doxygen Doxyfile` reports no new synchro warnings.

## 3. Schedule and continuity logic

- [x] 3.1 Add `SyncSchedule` with absolute start resolution, `T_scheduled(c, p)`, phase-period arithmetic, and cycle/phase lookup for a target instant; verify auto-align, exact target, aligned target, past-target realign, and reject-mode unit tests pass.
- [x] 3.2 Add `SyncSchedule` overrun classification that advances to the next future phase and counts every skipped slot across cycle boundaries; verify unit tests cover one-phase, multi-phase, and cycle-boundary skips.
- [x] 3.3 Add `SyncTracker` with `absolute_phase`, resync on epoch/phases_total change, backward-cycle handling, and duplicate suppression; verify positive, negative, and resync unit tests pass.
- [x] 3.4 Add transport-loss reconciliation to `SyncTracker`; verify tests distinguish pure transport loss from transport loss plus generator skips.
- [x] 3.5 Document `SyncSchedule` and `SyncTracker` with Doxygen contracts and add the corresponding `synchro/protocol` tests; verify the new unit tests pass.

## 4. Sync generator

- [x] 4.1 Implement eventfd creation, `EFD_NONBLOCK | EFD_CLOEXEC`, and the `std::stop_callback` wake bridge; verify stop wakes a pending `ppoll` and `EINTR` recomputes the deadline in unit tests.
- [x] 4.2 Implement the hybrid wait loop with relative `ppoll` timeout and final raw-clock spin; verify the spin threshold is not exceeded, stop latency is bounded, and the idle wait consumes no busy-poll CPU.
- [x] 4.3 Implement `SyncGenerator::create`, lifecycle states, `start`, `start_at`, `stop`, and restart epoch handling; verify state transitions, second-start rejection, restart epoch, sequence zero, and cycle zero tests pass.
- [x] 4.4 Implement phase publication with actual header timestamp, scheduled payload time, jitter histogram update, and `Notified`/`NotifyFailed` handling; verify publication, jitter, notify-failure, and send-failure tests pass.
- [x] 4.5 Implement overrun skipping and one warning per episode; verify skipped-slot counters, no stale burst, and no schedule drift tests pass.
- [x] 4.6 Implement generator metrics registration through `Meter` with `MultiThreaded` instruments and a dedicated meter contract; verify metric failure returns `SyncError{kind=Metrics}` and the five instruments exist.
- [x] 4.7 Implement structured lifecycle/overrun/fault logging through `Logger`; verify `NullLogger` and a recording logger see the expected events without hot-path allocation.
- [x] 4.8 Document `SyncGenerator` and its types with Doxygen; verify the generator tests and Doxygen warning check pass.

## 5. Sync consumer

- [x] 5.1 Implement `SyncConsumer<Logger>` as an `IEventConsumer` wrapper over `EventConsumer<SyncPayload>`; verify listener, drain, attachment state, received count, and set attachment tests pass.
- [x] 5.2 Implement phase-handler storage sized from `phases_per_cycle`, `on_any_phase`, `on_phase`, and `on_cycle_start` aliasing phase zero; verify registration, dispatch order, and phase isolation tests pass.
- [x] 5.3 Implement latency computation (`ipc_latency_ns`, `e2e_latency_ns`) and late classification against the configured or payload-derived maximum; verify signed values and `is_late` tests pass.
- [x] 5.4 Implement `DropAndWarn` and `DeliverWithFlag` behavior, including counters and warning logs; verify dropped events update continuity and late counters without false skips.
- [x] 5.5 Implement watchdog state, `sync_loss_timeout_ns`, `initial_sync_timeout_ns`, `check_timeout`, and lock/unlock logging; verify first-lock, loss, reacquisition, and initial-timeout tests pass.
- [x] 5.6 Implement consumer metrics through `Meter` with `MultiThreaded` instruments; verify metric registration failure and all seven metric names.
- [x] 5.7 Document `SyncConsumer` and `SyncContext` with Doxygen; verify consumer tests and Doxygen warning check pass.

## 6. Sync runner

- [x] 6.1 Implement `SyncRunner<Logger>` owning one `SyncConsumer` and one `EventConsumerSet<16>`; verify creation attaches the consumer and rejects a second runner for the same ownership slot.
- [x] 6.2 Implement `run_once` and `run(stop_token)` with `wait_for`, event draining, timeout watchdog check, and stop-callback `wake_up`; verify Events, Timeout, WokenUp, and stop tests pass.
- [x] 6.3 Implement next-watchdog-deadline calculation and millisecond timeout conversion; verify no busy polling and timeout rounding tests pass.
- [x] 6.4 Document `SyncRunner` with Doxygen and add runner unit tests; verify the runner tests and Doxygen warning check pass.

## 7. Integration and performance

- [x] 7.1 Add a real generator/consumer integration test with a 100 ms cycle and 10 phases; verify monotonic phase positions, no false gaps, and bounded jitter over a multi-second run.
- [x] 7.2 Add a restart/resync integration test; verify a producer restart yields `ProducerRestart`, cycle zero, sequence zero, and no false generator skip.
- [x] 7.3 Add an allocation-counter test around steady-state generation, publication, consumer drain, and handler dispatch; verify zero heap allocations.
- [x] 7.4 Add a 16-consumer boundary test and a multi-channel runner test; verify the 17th attach returns `OutOfSamples` and multiple channels use separate runners.
- [x] 7.5 Add a logging/metrics integration test; verify recorded lifecycle/anomaly messages and harvester-safe `MultiThreaded` counters under concurrent collection.

## 8. Final verification and architecture

- [x] 8.1 Run the complete `nexigen_synchro_tests` suite under Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer, and AddressSanitizer; verify 100% tests pass in all four trees.
- [x] 8.2 Run `nexigen_com_tests`, `nexigen_event_system_tests`, the complexity gate, and `openspec validate add-synchro-event-sync --strict`; verify all pass.
- [x] 8.3 Run `doxygen Doxyfile`; verify the synchro headers add no new warnings.
- [x] 8.4 Update `docs/architecture.md` with the `nexigen::synchro` module, its COM gate, and a Mermaid event-system interaction diagram; verify all Mermaid fences are balanced and the documented dependencies match CMake.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result and the synced `synchro/*` main specs.
