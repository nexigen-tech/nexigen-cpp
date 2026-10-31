# Proposal

## Why

A nexigen application instance needs a standard launch, supervision, and data-collection layer so every instance can be started, monitored, and diagnosed without per-application infrastructure code. Today the telemetry-output contract (`app/telemetry-layout`) defines how an instance *discovers* its output paths, but nothing *creates* those paths, *launches* the instance, or *collects* its lifecycle data. A dedicated runner closes the gap: it creates the instance directory, writes generated settings with absolute sink paths, launches the child process under a process group, captures its stdout/stderr, records start and exit metadata, and exposes run/stop/status commands.

## What Changes

- Add the `nexigen::runner` module: a cold-path library that implements the instance lifecycle contract
- Add a `nexigen-run` binary that wraps the library into a command-line tool
- Define `resource.json` (start-time metadata) and `exit.json` (completion metadata) JSON schemas
- On `start`: create `<run-dir>/<app>/<instance>/`, write generated `settings/*.json` with absolute `sink.path`; set `NEXIGEN_*` identity; `setsid` launch; capture stdout/stderr; write `resource.json`
- On `stop`: send SIGTERM (or configured signal), wait, write `exit.json`
- On `status`: report process group state (running/stopped/exited with code)
- No hot-path changes, no new external dependencies, no network transport

## Capabilities

### New Capabilities
- `runner/instance-lifecycle`: Per-run, per-app, per-instance directory layout, `resource.json`/`exit.json` schema, status tracking, and roll-up of the four JSONL streams (`app.jsonl`, `metrics.jsonl`, `traces.jsonl`, `chrono.jsonl`) under a single instance root
- `runner/process-supervision`: Process-group launch via `setsid`, stdio capture to files, signal-based stop, exit-code collection, and idle-timeout monitoring

### Modified Capabilities
(none — all new)

## Impact

- New `include/nexigen/runner/` headers and `src/runner/` implementation files
- New `nexigen-run` binary target in CMake (links `nexigen_runner` + `nexigen_app`, which brings `nexigen_logging` and `nexigen_core` transitively)
- Build matrix (Debug/Release/TSan/ASan) and test suite for the runner
- No changes to existing producer, tick, snapshot, or sink hot paths
- No new external dependencies (Linux syscalls only: `fork`, `exec`, `setsid`, `pipe2`, `dup2`, `waitpid`, `kill`, `nanosleep`, plus `open`/`mkdir`/`rename` for the cold-path file layout and `/proc` probes for status)
- Performance and latency: every runner operation is cold-path (process start/stop/status boundary work); `start` returns after filesystem setup and one fork, and `stop` blocks only its control thread for at most the grace period (default 5 s) plus a 5 s settle window. The supervised application's tick, producer, snapshot, and sink paths are untouched
- Memory allocation: metadata, settings, and JSON serialisation allocate `std::string`/`nlohmann::json` on the cold path only; no per-tick, per-record, or steady-state allocation is introduced