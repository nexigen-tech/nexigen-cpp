# Proposal

## Why

The project has all the primitives for a complete observability stack — three iceoryx2-consuming daemons (`nexigen-logd`, `nexigen-metricsd`, `nexigen-traced`), a `com` publish-subscribe transport with cross-process trace propagation (`TraceHeader`), and an `ExecutionManager` that launches applications from manifests with CPU affinity — but no single example wires them together. A newcomer cannot see how an application publishes its logs/metrics/traces to the daemons, how a trace rides across a data exchange, or how the manifest drives launch order and core placement.

## What Changes

- Add `examples/integration/suite_2_apps/` — a two-application integration example:
  - `ex_generator` — produces one `DataSample` (a monotonically increasing sequence number plus a fixed string) every second, publishes it over `nexigen::com` with a `TraceHeader`, and starts a fresh trace per sample.
  - `ex_receiver` — subscribes to the data stream, inherits the incoming trace into its own spans, logs and meters each received sample.
  - Both applications initialize `nexigen::app::Infrastructure` and publish their telemetry to the three daemons through the iceoryx2 sinks (`Iceoryx2Logger`, `Iceoryx2MetricSink`, `Iceoryx2TraceSink`).
  - `run_suite_2_apps.sh` — starts the three daemons, then the `ExecutionManager` with a generated `suite.json` manifest; generates the manifest and all daemon/settings config into a demo root (no machine-specific absolute paths committed).
  - `clean_suite_2_apps.sh` — removes the generated demo root idempotently.
- Register the two executables in the examples catalogue, CTest smoke tests, and README.

## Capabilities

### New Capabilities

- `examples/suite-2-apps`: the runnable two-application integration demonstration — `com` data exchange with trace propagation, iceoryx2 telemetry sinks feeding the three daemons, `ExecutionManager` launch with dependencies and CPU affinity, and the driver/cleanup scripts.

### Modified Capabilities

- `examples`: the runnable catalogue gains the two new executables (`ex_generator`, `ex_receiver`) and their `run_suite_2_apps.sh` / `clean_suite_2_apps.sh` scripts, raising the executable count from seventeen to nineteen.

## Impact

- **Code**: new files under `examples/integration/suite_2_apps/` (two application TUs, one shared `data_sample.hpp`, two shell scripts, one `CMakeLists.txt`); one `add_subdirectory(integration)` line in `examples/CMakeLists.txt`; README updates.
- **Dependencies**: the two executables link `nexigen::app` and `nexigen::com` (transitively `core`/`logging`/`metrics`/`tracing`/`chrono` and iceoryx2). No new third-party dependency.
- **Performance / latency / allocation**: none on the library. This is an example built only under `NEXIGEN_BUILD_EXAMPLES=ON`; its runtime loop uses the pre-allocated `Infrastructure` hot-path APIs (fixed-size trivially-copyable `DataSample`, `send`/`receive` on the zero-copy transport) and never allocates, locks, or formats on the producer path. Background worker threads are pinned via `ThreadSpec` so the demo does not perturb other work.
- **Breaking changes**: none.
