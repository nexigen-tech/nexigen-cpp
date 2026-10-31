# Proposal

## Why

The library now exposes a command-line parser and `Infrastructure::initialize(options, loaded)`, but there is no runnable end-to-end example that shows how an operator supplies settings, how the program prepares local configuration, and how logs, metrics, traces, and chrono snapshots are written to files. A self-contained example with run and cleanup scripts closes that gap and doubles as a CTest smoke check for the whole startup path.

## What Changes

- Add `examples/app/ex_app_cli_settings.cpp`, a runnable example that:
  - creates `./nexigen-example-settings/` in the current working directory and writes `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json` there before applying command-line arguments;
  - creates `./nexigen-example-output/` and clears any previous contents before writing new artifacts;
  - parses the standard `-gs/--global-settings`, `-ls/--local-settings`, `-lgl/--log-level`, and `-h/--help` options, keeping the global settings path empty by default;
  - initializes `Infrastructure` through `initialize(options, loaded)` with a stateless policy whose sinks write `app.jsonl`, `metrics.jsonl`, `traces.jsonl`, and `chrono.jsonl`;
  - performs several log writes, counter/gauge updates, a span, and chrono snapshots, then shuts down and flushes within a bounded run (well under one minute).
- Add `examples/app/run_ex_app_cli_settings.sh` to locate or build the example and run it under a 60-second timeout.
- Add `examples/app/clean_ex_app_cli_settings.sh` to remove the generated settings and output directories idempotently.
- Register the example in `examples/app/CMakeLists.txt` for the default build and CTest.
- Document the example, its output files, and both scripts in the README catalogue and update the `examples` catalogue requirement from fifteen to sixteen executables.

**Performance, latency, and memory impact:** none on library hot paths. The example is entirely cold path: directory setup, JSON writing, CLI parsing, settings loading, and sink construction run once before the tick loop. The tick loop only updates pre-created instruments and logs; it adds no library allocations or locks. The example targets a run time of a few seconds and is additionally bounded by a 60-second script timeout.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `app/application`: adds the runnable command-line settings example requirement, covering directory preparation, CLI-driven initialization, artifact output, and bounded runtime.
- `examples`: adds `ex_app_cli_settings` to the runnable catalogue and documented invocation, and adds the runner/cleanup script requirement.

## Impact

- Affected code: `examples/app/ex_app_cli_settings.cpp`, `examples/app/CMakeLists.txt`, two new shell scripts under `examples/app/`, and the README examples section.
- Affected specs: `openspec/specs/app/application/spec.md`, `openspec/specs/examples/spec.md`.
- Public API: no changes; the example consumes the existing command-line and `Infrastructure` APIs.
- Dependencies: no new dependencies; uses the standard library and existing `nexigen` modules.
- Systems: example build and CTest only; no library runtime behavior changes.
