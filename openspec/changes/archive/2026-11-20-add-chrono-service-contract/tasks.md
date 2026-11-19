# Tasks

## 1. Domain vocabulary and 128-bit arithmetic

- [x] 1.1 Add `TimeDomainClass` and `TimeDomain` with well-known ids, remove `ClockId`, and verify enum round-trip, `std::format`, and hostile-JSON tests pass
- [x] 1.2 Replace `clock_domain<T>` with `time_domain<T>` and verify undeclared components default to `TimeDomain::MonotonicRaw`
- [x] 1.3 Add `detail::int128` with portable 64x64->128 fallback and `__SIZEOF_INT128__` fast path; verify multiply/shift/divide tests against known vectors
- [x] 1.4 Add Q32 forward/reverse anchored map helpers and verify round-trip conversion error stays within the documented bound for TSC, TAI, and a 100 MHz device counter
- [x] 1.5 Update Doxygen contracts for the new vocabulary and arithmetic headers; verify the Doxygen build emits no warnings

## 2. Block layout and publication

- [x] 2.1 Rewrite `DomainCoefficient` and `TimeBaseBlock` (5120-byte target, `alignas(64)`, `TimeSnapshot` in block) and verify size/offset `static_assert`s and layout pinning tests pass
- [x] 2.2 Rewrite history entry and serialization for the new row and block; verify non-throwing JSON round-trips under `JSON_NOEXCEPTION=1`
- [x] 2.3 Extend the seqlock reader/writer to copy the header, rows, and snapshot around the generation word; verify the concurrent torn-block test and TSan annotations
- [x] 2.4 Add `SyncMode`, `ServiceFlags`, and snapshot fields; verify every value and flag round-trips through JSON and `std::format`
- [x] 2.5 Publish at 10 Hz with per-row re-anchoring and change-only history; verify status/heartbeat/gap/history tests at the new cadence
- [x] 2.6 Add the fixed-point 2-state Kalman filter for TAI/REALTIME/MONOTONIC and verify lock, step, drift, and holdover synthetic tests
- [x] 2.7 Update block/publisher design notes and Doxygen; verify `docs/architecture.md` diagrams match the new table and snapshot

## 3. PTP, TAI, and daemon execution

- [x] 3.1 Add TAI/REALTIME/MONOTONIC sampling and `adjtimex` status; verify `Standalone`/`PtpSynced`/`Holdover` transitions with fake clock sources
- [x] 3.2 Add `ServiceFlags` population and missing-service tests; verify absent `ptp4l`/`phc2sys`/PHC still produces a usable `Standalone` base
- [x] 3.3 Wire `TimeSyncMonitor` into the 10 Hz loop and map step/slew verdicts to flags and `Realtime` lock state; verify step/backwards/rate tests
- [x] 3.4 Add `ThreadSpec` config, `SCHED_FIFO`, best-effort `mlockall`, and absolute sleep deadlines to `nexigen-chronod`; verify required-failure and best-effort report tests
- [x] 3.5 Document PTP ownership, master clocks per mode, flags, and RT setup; verify the documented `nexigen-chronod` config runs

## 4. Hardware domains

- [x] 4.1 Wire every declared `HardwareDomainConfig` to its own backend, estimator, epoch, row, and history; verify multi-domain publication and declared-absent `Degraded` tests
- [x] 4.2 Add PHC cross-timestamp access (`PTP_SYS_OFFSET_PRECISE`) and a fake PHC backend; verify pair acquisition, errors, and row publication
- [x] 4.3 Add latched register-pair access and verify the D1-D3 sequence/CRC contract against the simulated device
- [x] 4.4 Add `ManualPair` access for legacy modules without PTM: read counter and `MONOTONIC_RAW`, record read latency, mark `manual_pair`/`ptm_capable=false`; verify uncertainty and row flags
- [x] 4.5 Update the firmware register-map contract and hardware docs; verify every contract field is exercised by the simulator

## 5. Client conversion and snapshots

- [x] 5.1 Add `DomainInstant` and any-to-any `convert()` through `MonotonicRaw`; verify same-domain, forward/reverse, epoch, and invalid-interval tests
- [x] 5.2 Add `last_snapshot()` and TAI absolute-deadline helpers; verify snapshot freshness and `clock_nanosleep(CLOCK_TAI)` deadline construction
- [x] 5.3 Switch `TimeBaseClock` and the UTC sinks to the `Realtime` reverse row; verify calibrated, uncalibrated, and step-detected JSONL tests
- [x] 5.4 Update `Infrastructure` chrono handle and examples to the new conversion API; verify app lifecycle and telemetry-layout tests
- [x] 5.5 Add Doxygen contracts for the client and conversion helpers; verify the Doxygen build emits no warnings

## 6. Recorder and export

- [x] 6.1 Replace `TimeBasePayload` with the new row/table payload and built-in schema; verify write/read, CRC, and payload round-trip tests
- [x] 6.2 Add the `TimeSnapshot` record type, payload, built-in schema, and session manifest fields; verify snapshot write/read and backward-incompatible schema tests
- [x] 6.3 Add any-domain resolution to `TimeBaseResolver` using history + snapshots, with explicit gaps; verify resolved, gap, and legacy-session rejection tests
- [x] 6.4 Bind channel declarations to `TimeDomain`/`TimeDomainClass` and access mode; verify hardware channel round-trip and unknown-domain rejection
- [x] 6.5 Update `docs/recorder.md` and exporter docs; verify the documented `nexigen-export` commands on a newly recorded session

## 7. Legacy removal and integration gates

- [x] 7.1 Delete `ClockId`, v1 block/payload readers, and old-session acceptance paths; verify the build has no references and old files are rejected as `UnsupportedFormat`
- [x] 7.2 Add an end-to-end suite test: chronod publishes TAI/device rows, app converts any-to-any, recorder stores snapshots, exporter resolves a device event to TAI and UTC
- [x] 7.3 Verify the loss path with PTP loss: holdover, expiry, exit code 75, and held retry after recovery
- [x] 7.4 Add an allocation-arena test for 10 Hz publication, client conversion, and sink UTC conversion; verify no steady-state allocation
- [x] 7.5 Run Debug, Release (`-O3 -march=native -flto` with latency gates), TSan, and ASan; verify all four runs pass
- [x] 7.6 Update `docs/architecture.md`, safety traceability, and Doxygen; verify diagrams render, guards pass, and the matrix names the new tests

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived result.
