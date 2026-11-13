# Spec Delta

## Purpose

Defines the runnable two-application integration demonstration that wires the `com` publish-subscribe transport, cross-process trace propagation, the three telemetry daemons, and `ExecutionManager`-driven launch with CPU affinity into one end-to-end scenario.

## ADDED Requirements

### Requirement: Two self-contained application executables

The project SHALL provide `ex_generator` and `ex_receiver` under `examples/integration/suite_2_apps/`, each with its own `main`, no test-harness dependency, and each registered as a CTest smoke test named after the executable and labeled `examples`. In their default bounded mode each SHALL run a short workload and exit 0 without requiring a peer, a daemon, or a manifest.

#### Scenario: Default bounded mode exits zero

- **WHEN** `ex_generator` or `ex_receiver` is built and launched directly (or via CTest) with a valid settings directory
- **THEN** it completes a bounded workload, writes its telemetry, and exits 0

#### Scenario: Self-contained and harness-free

- **WHEN** either executable runs outside any suite or daemon
- **THEN** it creates its own `Infrastructure` from the supplied settings and depends only on the public `nexigen` API

### Requirement: One-hertz fixed-shape data generation

`ex_generator` SHALL emit one sample every second while running. Each sample SHALL carry a monotonically increasing sequence number and a fixed string payload, and the payload SHALL be a fixed-size, trivially-copyable value safe for a zero-copy shared-memory exchange.

#### Scenario: One sample per second

- **WHEN** `ex_generator` runs for several seconds
- **THEN** the number of published samples is consistent with one per second and the sequence numbers are strictly increasing

#### Scenario: Payload is fixed and copyable

- **WHEN** a sample is published and received
- **THEN** the received sequence number and fixed string equal what the generator produced, with no pointer or owning member crossing the exchange

### Requirement: Data exchange over the com transport

The two applications SHALL exchange samples through the `nexigen::com` publish-subscribe transport using a single shared service name: `ex_generator` publishes and `ex_receiver` subscribes. Discovery SHALL be daemon-less, so the receiver may start before any publisher appears and still receive samples once the generator publishes.

#### Scenario: Receiver obtains published samples

- **WHEN** both applications run against the shared service
- **THEN** `ex_receiver` receives the samples `ex_generator` publishes, in order, without the receiver polling another process or reading a file

#### Scenario: Receiver started first still connects

- **WHEN** `ex_receiver` subscribes before `ex_generator` creates its publisher
- **THEN** the receiver still receives the generator's subsequent samples

### Requirement: Cross-process trace propagation

`ex_generator` SHALL start a fresh trace for each published sample and attach a propagation header carrying the trace id and the publishing span id. `ex_receiver` SHALL extract that header and link its own processing span to the incoming trace, so a single trace id spans the generator's publish span and the receiver's processing span.

#### Scenario: Fresh trace per sample

- **WHEN** `ex_generator` publishes two consecutive samples
- **THEN** each sample's header carries a distinct non-zero trace id

#### Scenario: Receiver links to the incoming trace

- **WHEN** `ex_receiver` processes a received sample
- **THEN** the span it opens for that sample carries the same trace id as the sample's header and records that header's source span as its parent

### Requirement: Telemetry reaches the daemons

Both applications SHALL publish their logs, metric batches, and spans to the three daemons through the iceoryx2 sinks (`Iceoryx2Logger`, `Iceoryx2MetricSink`, `Iceoryx2TraceSink`) using the service names the daemons subscribe to. The shared trace id between generator and receiver SHALL be observable in the log daemon's output, because both applications log the per-sample trace id.

#### Scenario: Logs, metrics, and traces reach the daemons

- **WHEN** the daemons and both applications run together
- **THEN** the log daemon writes the applications' log records, the metrics daemon aggregates their metrics, and the trace daemon receives their spans

#### Scenario: Shared trace id is observable in the logs

- **WHEN** a full run completes
- **THEN** the log daemon's output contains the generator's publish record and the receiver's receive record carrying the same trace id

### Requirement: Manifest drives launch order and configuration

The demonstration SHALL ship a suite manifest that `ExecutionManager` accepts via `nexigen-exec start --config`, launching the two applications under distinct `app` names with `ex_receiver` listed before `ex_generator` and `ex_generator` declaring `ex_receiver` as a dependency so the receiver reports running first. The manifest SHALL supply the command-line settings argument and a per-suite `debug` log level.

#### Scenario: Receiver reports running before the generator launches

- **WHEN** the manifest is loaded
- **THEN** `ex_receiver` is launched first and `ex_generator` does not start until the receiver reports running

#### Scenario: Manifest is accepted by the execution_manager

- **WHEN** `nexigen-exec start --config` runs with the shipped manifest
- **THEN** the suite supervisor starts and reports both applications running

### Requirement: CPU affinity and thread placement

The demonstration SHALL pin the `ExecutionManager` control and logger threads to cores 2 and 3, the three daemons to cores 4 and 5, `ex_generator` (process and its background workers) to cores 6 and 7, and `ex_receiver` (process and its background workers) to cores 8 and 9. Worker threads SHALL be pinned within their owning application's core range through `ThreadSpec`.

#### Scenario: Process placement follows the manifest

- **WHEN** the suite runs with the shipped manifest and settings
- **THEN** the generator's process affinity is restricted to cores 6-7 and the receiver's to cores 8-9

#### Scenario: Background workers are pinned within their application

- **WHEN** an application's logging, metrics, and tracing worker threads start
- **THEN** each worker's requested CPU set lies inside its application's core range

### Requirement: Daemon configuration and debug level

The demonstration SHALL configure the three daemons through generated JSON configs: the log daemon with a `debug` storage level, the metrics daemon with a Prometheus HTTP endpoint and JSON-lines history, and the trace daemon with a RAM flight-recorder ring, each with its service name matching the corresponding sink and its background affinity on cores 4-5. Logging everywhere SHALL be at `debug`.

#### Scenario: Daemons consume the applications' streams

- **WHEN** all three daemons start with the generated configs and both applications run
- **THEN** each daemon subscribes to its service and consumes the frames the matching sink publishes

#### Scenario: Debug level is applied throughout

- **WHEN** the suite, applications, and daemons run
- **THEN** debug-level records are emitted and stored rather than dropped by a higher threshold

### Requirement: Driver and cleanup scripts

The project SHALL provide `examples/integration/suite_2_apps/run_suite_2_apps.sh` and `examples/integration/suite_2_apps/clean_suite_2_apps.sh`. The driver SHALL build the example when needed, generate the manifest and all daemon and settings config into a demo root under `/tmp/opencode`, start the three daemons, then start the suite via `nexigen-exec`. The cleanup script SHALL stop the daemons and remove the demo root idempotently.

#### Scenario: Driver generates configs and starts everything

- **WHEN** `run_suite_2_apps.sh` is invoked
- **THEN** it generates the manifest and daemon/settings configs into the demo root, starts the three daemons, and starts the suite via `nexigen-exec start --config`

#### Scenario: No machine-specific absolute paths are committed

- **WHEN** the repository is inspected
- **THEN** the committed example files contain no absolute paths specific to a developer machine

#### Scenario: Cleanup removes generated state and is idempotent

- **WHEN** `clean_suite_2_apps.sh` runs after a run (or when the demo root is already absent)
- **THEN** the daemons are stopped and the demo root is removed, exiting successfully either way
