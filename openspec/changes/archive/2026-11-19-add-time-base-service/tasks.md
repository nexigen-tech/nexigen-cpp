# Tasks

## 1. Shared-memory block primitives

- [x] 1.1 Define `TimeBaseStatus`, `DomainKind` and `LockState` in `include/nexigen/chrono/` with `to_string`/`from_string`; verify enum round-trip and formatter tests pass
- [x] 1.2 Define `DomainCoefficient`, `TimeBaseBlock` and `TimeBaseHistoryEntry` with the 64-byte header / 128-byte row layout and `alignas(64)` from design D-C; verify `static_assert` size/offset tests and a layout pinning test pass
- [x] 1.3 Add `to_string`, free `to_string`, `std::formatter` and non-throwing `to_json`/`from_json` for the value types; verify serialization and hostile-JSON tests pass under `JSON_NOEXCEPTION=1`
- [x] 1.4 Implement the seqlock writer and reader with the memory orders from design D-C; verify a concurrent reader/writer test observes only complete blocks and a late reader sees the latest block
- [x] 1.5 Implement segment create/attach/unlink, permissions and the crash-takeover protocol; verify tests for stale-owner takeover, live-owner rejection and reader-detected frozen heartbeat pass
- [x] 1.6 Add Doxygen contracts (`@brief`, `@param`, `@return`, `@tparam`) to every touched header; verify the Doxygen build emits no warnings for the new headers

## 2. Time-base daemon

- [x] 2.1 Add the daemon settings document (suite identity, publish interval, declared hardware domains, estimator windows) with a cold-path loader; verify default, invalid and empty-domain tests pass
- [x] 2.2 Implement host-domain sampling: canonical identity row, TSC edge measurement, `current_clocksource` check and cross-core TSC probe; verify tests that a non-TSC clocksource withdraws the TSC row and reports `Degraded`
- [x] 2.3 Implement the estimator: min-residual pair selection, scale/offset fit, uncertainty, and the change thresholds from design D-F; verify synthetic-pair tests for lock, drift, step and holdover
- [x] 2.4 Implement the publish loop: status transitions, generation bump, heartbeat, and history append with explicit gaps; verify status/ready/degraded/lost and gap tests pass
- [x] 2.5 Implement the history file writer with the `chrono.jsonl` line schema including the full boot UUID; verify a recorded run yields one line per change and no lines for unchanged intervals
- [x] 2.6 Add `tools/nexigen-chronod` and its CMake target, built on `chrono` and the logger without `app::Infrastructure`; verify the binary starts with no hardware configured and publishes a `Ready` host-only block
- [x] 2.7 Update `docs/architecture.md` module map and Mermaid diagrams for the time-base service and daemon; verify the diagrams render and match the new dependency edges
- [x] 2.8 Add Doxygen contracts for the new daemon headers and the daemon settings type; verify the Doxygen build emits no warnings

## 3. Client and application integration

- [x] 3.1 Implement the read-only client: attach, cached rows, generation refresh, `coefficient()`, `canonical_from_native()` and `utc_from_canonical()`; verify unit tests against a fake publisher
- [x] 3.2 Implement required-domain declaration, the holdover state machine and fail-fast, with the deadline on the canonical scale; verify holdover expiry, silent recovery and non-required degradation tests pass
- [x] 3.3 Replace provider construction in `Infrastructure` with the client attach; add `InfrastructureStopReason::TimeBaseLost` and the reserved exit code 75; verify initialization fails without a base and the stop reason round-trips
- [x] 3.4 Add the in-process fake publisher (anonymous shared mapping, same seqlock) and migrate `Infrastructure` and app tests to it; verify no test requires a running daemon
- [x] 3.5 Remove per-instance `chrono.json` sink configuration and per-instance `chrono.jsonl` generation from `instance_lifecycle`; verify telemetry-layout and exec settings tests reflect the suite-level history file
- [x] 3.6 Add Doxygen contracts to the client and stop-reason headers; verify the Doxygen build emits no warnings

## 4. Execution gates

- [x] 4.1 Add the optional `time_base` object (`process`, `segment`, default segment, validation) to the suite manifest; verify manifest round-trip, default-segment and rejection tests pass
- [x] 4.2 Implement the readiness gate: a dependent starts only when the daemon is `Running` and the block reports `Ready`; verify dependency-order tests cover ready, not-ready and timeout
- [x] 4.3 Implement the retry gate for exit code 75 without consuming the restart budget; verify held-retry, resumed-retry and unaffected-reason tests pass
- [x] 4.4 Update `docs/runner-suite.md` for the time-base dependency and the readiness limit; verify the documented manifest example validates

## 5. Lossless JSONL streams

- [x] 5.1 Add `mono_ns` and the 16-hex `boot` token to `format_json_line` and its call site; verify exact-schema, legacy-line and escaping tests pass
- [x] 5.2 Add `mono_ns` and `boot` to the metric batch line; verify the exact-line, legacy and overflow tests pass
- [x] 5.3 Add `start_mono_ns`, `end_mono_ns` and `boot` to the span line; verify the exact-line, legacy and attribute tests pass
- [x] 5.4 Update `nexigen-logd`, `nexigen-metricsd` and `nexigen-traced` output to the same field set; verify their CLI tests parse the new fields
- [x] 5.5 Derive UTC from the published time base in all three sinks and keep `mono_ns` authoritative; verify an uncalibrated interval still emits `mono_ns` with no fabricated UTC
- [x] 5.6 Document the v2 line schemas (fields, ordering, legacy detection) in `docs/architecture.md` or a dedicated telemetry-format section; verify the documented examples parse

## 6. Recorder and export

- [x] 6.1 Add the `TimeBase` record type and payload with the row layout and boot UUID, plus its built-in schema; verify write/read round-trip and CRC tests pass
- [x] 6.2 Record the boot token and time-base epoch binding in the session manifest; verify manifest round-trip and backward-compatibility tests pass
- [x] 6.3 Add per-channel native-domain declaration to the channel registry; verify a declared hardware channel round-trips and an unknown domain is rejected
- [x] 6.4 Implement three-step resolution in the exporter (native → canonical → UTC) with explicit unresolved gaps; verify resolved, gap and legacy-session tests pass
- [x] 6.5 Update `docs/recorder.md` for the `TimeBase` record and resolution; verify the documented `nexigen-export` commands run on a recorded session

## 7. Hardware path

- [x] 7.1 Change `EventHeader` to carry the native domain, domain epoch and raw ticks while staying 64 bytes; verify the layout pin, presence-flag and serialization tests pass
- [x] 7.2 Update the gateway ingress to pass ticks/domain/epoch and stop converting ticks; verify gateway tests and the spec scenario tests pass
- [x] 7.3 Update `HardwareMetadata` fields and serialization; verify round-trip and hostile-JSON tests pass
- [x] 7.4 Define the device backend seam (PHC cross-timestamp or register pair) and a simulated device for tests; verify the estimator consumes simulated pairs and publishes a device row
- [x] 7.5 Write the firmware register-map contract (D1–D3 fields, latch semantics, status bits, versioning) as a design document; verify every field in the contract is exercised by the simulated device

## 8. Integration and release gates

- [x] 8.1 Run the full unit suite under Debug, Release (`-O3 -march=native -flto` with latency gates), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`); verify all four runs pass
- [x] 8.2 Add an allocation-arena test covering publication, client refresh and sink conversion; verify no allocation on any steady-state path
- [x] 8.3 Add an end-to-end suite test: daemon starts, app attaches, records logs/metrics/traces, recorder captures a `TimeBase` history, exporter resolves an event; verify the resolved canonical value matches the recorded one
- [x] 8.4 Verify the time-base loss path end to end: kill the daemon, observe holdover, expiry, exit code 75 and the held retry until readiness returns
- [x] 8.5 Update `docs/safety/traceability.md` so the time-base supervision evidence points at the new tests; verify the matrix entry names a test that fails when supervision is removed

## 9. Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result.
