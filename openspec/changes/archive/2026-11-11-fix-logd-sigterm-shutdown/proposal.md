# Proposal

## Why

`nexigen-logd` does not shut down when it receives `SIGTERM`: a probe against the current build exits with code **143** (killed by the default signal disposition), never prints `nexigen-logd: stopped`, and never reaches the final flush. The pending batch — up to half of the 64 KiB buffer — is lost, and the file is not `fsynced` or closed.

This is not a missing feature: `openspec/specs/logging/log-daemon` already requires it.

> **Scenario: Flush empties the buffer on shutdown** — WHEN the daemon receives a `SIGTERM` or a stop request THEN all buffered lines are flushed to disk and `fsynced` before the daemon exits.

So the implementation violates an established requirement. `nexigen-metricsd` proves the intended shape works: it builds its `WaitSet` with `create_without_signal_handling()`, owns the handlers, and exits 0 after flushing.

Root cause: `tools/nexigen-logd/main.cpp` calls `com::WaitSet::create()`, which hands signal handling to iceoryx2, and only then installs its own `std::signal` handlers. The iceoryx2-managed wait swallows the signal, so `g_stop` is never observed and the process is terminated by the default disposition.

## What Changes

- Build the daemon's `WaitSet` with `com::WaitSet::create_without_signal_handling()`, keeping the daemon-owned `SIGTERM`/`SIGINT` handlers and the interval timer that bounds exit latency, exactly as `nexigen-metricsd` does.
- Add a lifecycle regression test (`tools/nexigen-logd/smoke_test.sh`, registered as a CTest `tools` entry): start the daemon, send `SIGTERM`, require exit code 0, the `listening on` and `stopped` lines, and a freshly created log directory.
- No CLI, configuration, log-format or rotation change.

## Capabilities

No capability deltas: the change restores conformance with the existing `logging/log-daemon` requirement rather than changing what the system must do, so `.openspec.yaml` sets `skip_specs: true`.

## Impact

- **Code:** `tools/nexigen-logd/main.cpp` (one constructor call) plus the new smoke test and its CMake registration.
- **Behavior:** the documented shutdown path now runs — buffered lines flushed, `fsync`, `close`, `nexigen-logd: stopped`, exit code 0 instead of 143. This is the observable difference and the point of the change.
- **Performance / latency / memory:** none. The wait still blocks in the kernel until a frame or the batch timer fires; exit latency stays bounded by one batch interval (default 500 ms). No allocation or layout change.
- **Risk:** a daemon that stops honoring signals if the handler installation order regresses — covered by the new CTest entry.
