# Tasks

## 1. Shared payload and application scaffold

- [x] 1.1 Add `examples/integration/simulation_suite/vector_sample.hpp` with the `VectorSample` POD (sequence, `sent_ns`, ten `double` values) and a `static_assert` that it is trivially copyable; verify the assertion compiles and that `sizeof(VectorSample) == 96` on the target ABI.
- [x] 1.2 Give `VectorSample` `to_string()`, the free `to_string`, `std::formatter`, and the ADL `to_json`/`from_json` pair that survives `JSON_NOEXCEPTION=1`; verify a round-trip test covers every field and that hostile input leaves the destination unchanged.
- [x] 1.3 Add `examples/integration/simulation_suite/sim_common.hpp` with the shared `Infrastructure` policy, settings bootstrap, environment knobs (`NEXIGEN_SIM_PERIOD_MS`, `NEXIGEN_SIM_EXIT_AFTER_S`, `NEXIGEN_SIM_RUN_FOREVER`, `NEXIGEN_SIM_WORKER_SECONDS`), and the Function Group state reader; verify two different binaries can include it without defining duplicate symbols.
- [x] 1.4 Document every public type and helper in the two example headers with `/// @brief`, `@param`, `@return`, `@tparam`, and the owning spec requirement, matching the surrounding examples; verify `cmake --build --target ex_sim_generator` emits no Doxygen-style warnings once the targets exist.

## 2. Vector generator

- [x] 2.1 Implement `ex_sim_generator.cpp` publishing one `VectorSample` per `NEXIGEN_SIM_PERIOD_MS` over `Nexigen/Simulation/Vectors`, filling ten random `double` values and a strictly increasing sequence; verify a two-second bounded run publishes the expected count and the sequence increases.
- [x] 2.2 Attach a fresh `TraceHeader` to every published vector and open a `publish-vector` producer span; verify two consecutive vectors carry distinct non-zero trace ids.
- [x] 2.3 Report readiness to the Execution Manager, publish checkpoints to `Nexigen/PHM/Simulation/vector_generator`, and honour the published `Simulation` state (`Off` stops publishing, `SafeState` logs and waits); verify readiness reaches the EM and that switching the group to `Off` stops publication.
- [x] 2.4 Emit logs, the `vectors_sent` counter, and the `last_sequence` gauge through the iceoryx2 sinks with the generator's own `app_id`; verify each instrument appears in the daemons' output during a bounded run.

- [x] 2.5 Ring `Nexigen/Simulation/VectorsEvent` after every successful publish and verify two consecutive publishes produce two notifications.

- [x] 2.6 Keep one chunk loaned ahead of the loop and pre-loan the next one immediately after each send, with a synchronous fallback when the pool is empty; verify `publish_cost_us` no longer contains the loan and end-to-end latency drops.

## 3. Working receiver

- [x] 3.1 Implement `ex_sim_receiver.cpp` subscribing to `Nexigen/Simulation/Vectors` and printing the vector index, its ten values, and the measured latency in microseconds; verify the printed index and values equal what was received.
- [x] 3.2 Link each received vector's span to the incoming trace header and log the trace id; verify the receiver's span carries the generator's trace id and the log line names it.
- [x] 3.3 Report readiness, publish checkpoints to `Nexigen/PHM/Simulation/vector_receiver`, and honour the `Simulation` state; verify readiness reaches the EM and the checkpoint service matches the health manifest.
- [x] 3.4 Emit logs, the `vectors_received` counter, and a latency gauge; verify all three appear in the daemons' output.
- [x] 3.5 Replace the fixed receive sleep with a WaitSet blocked on the publish event, keeping a timeout for the state check and a poll fallback; verify the printed latency drops to hundreds of microseconds and the receiver stays idle when nothing is published.

- [x] 3.6 Add the opt-in `NEXIGEN_SIM_SPIN=1` busy-wait mode with a bounded spin slice, and verify it reports a lower latency than the blocking mode while consuming one core.

- [x] 3.7 Add the opt-in `NEXIGEN_SIM_RT_PRIORITY` policy switch (ThreadSpec FIFO, `allow_rt_priority` in the manifest) and verify the refusal path logs a warning and keeps the default policy.

## 4. Flaky receiver

- [x] 4.1 Implement `ex_sim_flaky_receiver.cpp` as the working receiver plus a timer that shuts the infrastructure down and returns 1 after `NEXIGEN_SIM_EXIT_AFTER_S` (a normal exit, so iceoryx2 unregisters its node instead of leaking registrations); verify a three-second bounded run exits with code 1.
- [x] 4.2 Keep the display, trace linking, readiness, and checkpoint behaviour identical to the working receiver, using its own checkpoint service `Nexigen/PHM/Simulation/flaky_receiver`; verify the checkpoint service and the printed format match the working receiver.
- [x] 4.3 Verify the platform relaunches it: run the suite with `NEXIGEN_SIM_EXIT_AFTER_S=3` and confirm `suite-state.json` shows a new attempt and the console shows vectors again after the restart.

## 5. Generated stack configuration

- [x] 5.1 Add `config/` and `settings/` for the three applications (logging, metrics, tracing, chrono) mirroring the suite-2-apps layout; verify each application starts with them standalone.
- [x] 5.2 Implement `run_simulation_suite.sh` generating `suite.json` with Function Group `Simulation` (`Running`/`SafeState`/`Off`), `RootFG`, and the three applications with `restart_attempts: 100`, `restart_delay_s: 1`, receiver-before-generator dependency, and per-app CPU sets; verify `nexigen-exec start --config` accepts the generated manifest.
- [x] 5.3 Generate `sm.json` with the `Simulation` group, its allowed transitions, and requester priorities; verify `nexigen-sm --config` starts and publishes `Running` for the group.
- [x] 5.4 Generate `phm.json` with one entry per application, checkpoint services `Nexigen/PHM/Simulation/<app>`, a 2 s alive window, tolerance 1, empty recovery rules, and `recovery_timeout_ms: 1000`; verify `nexigen-phmd` accepts it and reports supervising three processes.
- [x] 5.5 Generate the `logd`/`metricsd`/`traced` configs with their service names matching the sinks and output directories under the demo root; verify each daemon starts and consumes frames.
- [x] 5.6 Start the daemons, then `nexigen-exec`, then `nexigen-sm`, then `nexigen-phmd`, waiting for readiness markers between stages; verify the script reaches the running state on a fresh demo root.
- [x] 5.7 Implement `clean_simulation_suite.sh` stopping every started process and removing the demo root; verify it succeeds both after a run and when the root is already absent.

## 6. Build integration and smoke tests

- [x] 6.1 Register the three executables under `examples/integration/simulation_suite/CMakeLists.txt` behind `if(NEXIGEN_ENABLE_COM)` and add the directory to `examples/integration/CMakeLists.txt`; verify the targets appear in the build graph.
- [x] 6.2 Register one CTest smoke test per executable with a bounded two-second run, an isolated settings directory, and the `examples` label; verify all three pass in a COM-enabled build.
- [x] 6.3 Verify the committed example contains no absolute machine paths and that all paths are derived from the build tree and the demo root.

## 7. Verification

- [x] 7.1 Compile and pass 100% of the unit tests under Debug (`build-asan-com`) and Release (`-O3 -march=native -flto`); verify `scripts/build.sh` reports green for the touched trees.
- [x] 7.2 Compile and pass the suite under ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`); verify no data-race report appears in the example paths.
- [x] 7.3 Compile and pass the suite under AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`); verify no leak or overflow report appears in the example paths.
- [x] 7.4 Run the full stack for at least two crash cycles and verify: vectors print with latency in both receivers, the flaky receiver disappears and returns, PHM keeps all three `Ok`, logs and metrics land in the demo root, the trace daemon's shutdown counter shows received spans (a flight-recorder dump additionally needs a recovery trigger), and `nexigen-exec status` reports the suite running.
- [x] 7.5 Confirm the change introduces no new module, thread, or dependency in `docs/architecture.md`; verify its module map already covers `examples/integration/` and therefore no Mermaid diagram update is required for this change.
- [x] 7.6 Run `graphify update .` after the example lands and verify the graph rebuilds with the three new executables represented.

- [x] 7.7 Write `docs/low-latency-guide.md` as a step-by-step latency guide from the measurements taken while building the example, link it from `README.md`, and register it in the `mkdocs.yml` navigation.

## Workflow follow-up

- Archive the change after review.
- Verify the archived delta landed in `openspec/specs/examples/simulation-suite/`.
