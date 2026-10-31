# Proposal

## Why

`ex_app_lifecycle` is currently a full integration demo: it writes three global/local settings pairs, configures file sinks, raises SIGINT from a helper thread and verifies JSONL/trace files. That is useful as a lifecycle test, but it is too much for a first-contact example of `Infrastructure`: a reader has to understand settings loading, file sinks and signal orchestration before seeing the core six-line flow. The current example also still carries a stale "base-class surface" framing even though `App` was removed.

## What Changes

- Replace `examples/app/ex_app_lifecycle.cpp` with a minimal direct `main`:
  - define one small stateless `Simple_policy` (`ConsoleLogger`, `ConsoleMetricSink`, `ConsoleTraceSink`),
  - `make_infrastructure<Simple_policy>()`,
  - `initialize(App_settings{})` with defaults (no settings files),
  - log through `logger()`, create/increment a counter through `meter()`, open/finish a span through `tracer_for()`,
  - run a few ticks and stop via `request_stop(Infrastructure_stop_reason::Requested)`,
  - `shutdown()` and exit 0.
- No application class at all; the executable name stays `ex_app_lifecycle`, the full settings/SIGINT coverage remains in unit tests.
- Update the `app/application` runnable-example requirement: remove the settings-file/SIGINT expectations and add the direct-`main` contract.
- Update README example description; the example catalogue name/count does not change.

**BREAKING**: none. This is an example-only change.

## Capabilities

### New Capabilities
None.

### Modified Capabilities

- `app/application`: replaces the runnable example requirement (old full settings/SIGINT demo removed; new direct-`main` minimal example added).

## Impact

- **Code:** `examples/app/ex_app_lifecycle.cpp` only; no library/API changes.
- **Docs:** README example bullet; no module-map change.
- **Performance/memory:** none in production; the example becomes smaller and faster (no file I/O, no settings parsing, no SIGINT helper thread). It still exercises one cold heap allocation via `make_infrastructure`, logger/metric/tracer initialization, a few ticks and the full shutdown flush.
