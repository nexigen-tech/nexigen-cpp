# Tasks

## 1. Configuration, ring, and snapshot writer

- [x] 1.1 Add `include/nexigen/tracing/trace_daemon_config.hpp` and `src/tracing/trace_daemon_config.cpp` with the span/recovery/event service names, subscriber depth, drain interval, ring capacity, dump directory, max dump count, recovery-trigger flag, application name, and `core::ThreadSpec`, plus validation and the `to_string`/`formatter`/JSON trio; verify invalid names, interval, capacity, relative directory and zero limit are rejected, a valid config round-trips through JSON, and formatter output equals `to_string()`
- [x] 1.2 Add `include/nexigen/tracing/detail/span_ring.hpp` with the pre-allocated `SpanRing` (head/count/overwrite counters, drop-oldest, oldest-first iteration); verify capacity enforcement, chronological order, overwrite counting and zero heap allocations across pushes with the armed allocation counter
- [x] 1.3 Add `include/nexigen/tracing/detail/trace_dump.hpp` with the 96-byte `TraceDumpHeader` layout pins, the snapshot writer (temp file + `fsync` + rename, header fields, 384-byte frames) and a test-side reader; verify a known ring round-trips every header/frame field, unknown magic/version is rejected, and a failed write leaves no partial file

## 2. Snapshot naming and retention

- [x] 2.1 Add the UTC snapshot name builder (`incident_trace_<YYYYMMDDTHHMMSSZ>.nxtr`) with a collision suffix; verify names sort chronologically and two snapshots in the same second receive distinct names
- [x] 2.2 Add dump-directory creation and pruning: scan `incident_trace_*.nxtr`, keep the newest `max_dump_files`, delete the oldest; verify pruning leaves exactly the configured count, removes the oldest file first, and a missing directory is created component by component

## 3. Daemon session

- [x] 3.1 Add `include/nexigen/tracing/detail/trace_daemon_session.hpp` with `start`, `wait_and_process`, `run(stop)` and counters; open the node, span subscriber, ring, dump directory and readiness reporter; verify an end-to-end test publishes frames through `Iceoryx2TraceSink`, the session drains them into the ring within the interval, and arrival order is preserved
- [x] 3.2 Add the recovery channel: `Subscriber<sm::RecoveryNotification>`, an `EventListener` on the recovery event, `WaitSet` interval + notification attachments, trigger coalescing, snapshot-after-drain and clear-on-success; verify one notification produces one snapshot naming the group, two notifications in one wake produce one snapshot naming the last group, and `trigger_on_recovery=false` writes nothing
- [x] 3.3 Add lifecycle handling: report `Running` through `ApplicationStateReporter` after ports open, apply `ThreadSpec` affinity, attempt `mlockall` best-effort, and stop on a flag without writing a snapshot; verify a listener observes the `Running` event, an `mlockall` failure does not abort startup, and a stop request leaves the dump directory unchanged

## 4. Tool and build

- [x] 4.1 Add `tools/nexigen-traced/main.cpp` (CLI `--config`, JSON load/validation, signal handling, session run) and register the `nexigen-traced` target inside `if(NEXIGEN_ENABLE_COM)`; verify the tool builds and runs with `-DNEXIGEN_ENABLE_COM=ON` and is absent with the gate off
- [x] 4.2 Add a lifecycle smoke test (`tools/nexigen-traced/smoke_test.sh`) starting the daemon, confirming it stays alive, sends `SIGTERM`, and expects exit code 0 with no snapshot created; register it in CTest and verify it passes
- [x] 4.3 Register `tests/tracing/trace_daemon_test.cpp` as `nexigen_trace_daemon_tests` (COM-gated, links `nexigen_tracing_com` and the tracing test support) and verify `ctest -N` lists it

## 5. Documentation

- [x] 5.1 Document the daemon in the README tracing section (and, if useful, a short `docs/trace-daemon.md`): span subscription, ring capacity/window, recovery trigger, `.nxtr` header, retention, zero steady-state disk I/O, and the deferred session/ftrace/Perfetto work; verify the documented option, target and config field names match the build and headers

## 6. Integration verification

- [x] 6.1 Run the full matrix with the change in place — Debug, Release, TSan, ASan and a Clang `-Werror` build — and confirm 100% of tests pass, including `nexigen_trace_daemon_tests` and the daemon smoke test
- [x] 6.2 Confirm the hot path is unchanged: the daemon only consumes `SpanFrame` and does not add work to `ScopedSpan`, `Tracer`, `TracerProvider` or the sink publish path; verify the existing tracing and trace-sink latency gates still pass in Release
- [x] 6.3 Run `openspec validate add-trace-daemon --strict` and the repository formatting/complexity gates; verify both are clean
- [x] 6.4 Run `graphify update .` after the code lands and confirm the knowledge graph is refreshed

## Workflow follow-up

- Archive the change after review, then open `add-trace-session-control` (on-demand sessions, category gating) and `add-perfetto-export` against the now-stable `.nxtr` snapshot.
