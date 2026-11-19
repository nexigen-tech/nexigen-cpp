# Spec Delta

## MODIFIED Requirements

### Requirement: Initialization follows a fixed order

`Infrastructure::initialize(const Settings&)` SHALL claim the process-global signal ownership, block `SIGINT`/`SIGTERM`, attach to the time-base daemon and fail initialization when no usable base is published, create the logger, construct the metric provider and check its required worker-start result, construct and initialize the tracing provider, and start the signal watcher. A failure at any step, including a `Required` thread-attribute failure or an unusable time base, SHALL release ownership, tear down partial state in reverse dependency order, restore the signal mask, and return a `core::Status`. The signal watcher SHALL retain its existing configuration; no separate app settings document is introduced.

#### Scenario: Logger is ready before providers start

- **WHEN** initialization begins with valid logging, metrics, tracing, and chrono settings and a running daemon
- **THEN** the time base is attached first, the logger is configured before the metric or tracing worker threads start, and every later diagnostic can be logged

#### Scenario: Missing time base fails initialization

- **WHEN** initialization runs while the time-base daemon is absent or not usable
- **THEN** it returns a failure status before creating the logger, metric provider, or tracer provider

#### Scenario: Failure rolls back partial initialization

- **WHEN** the tracer provider fails to initialize after the logger and metric provider were constructed
- **THEN** `initialize` releases signal ownership, destroys the started components, restores the signal mask, and returns the failure status

#### Scenario: Required worker-attribute failure starts no non-RT fallback

- **WHEN** any logger/provider worker cannot apply a required FIFO/RR or affinity attribute after earlier components started
- **THEN** that worker does not enter its main loop, earlier components are stopped and destroyed in the existing rollback order, and initialization returns `SystemError`

#### Scenario: Best-effort partial application does not trigger rollback

- **WHEN** a worker explicitly uses `BestEffort` and one attribute is rejected
- **THEN** initialization continues with the partial state exposed in that worker's startup report

#### Scenario: Repeated initialization is idempotent

- **WHEN** `initialize` is called again on the same initialized `Infrastructure`
- **THEN** it returns OK without recreating components or the signal watcher

### Requirement: Infrastructure exposes logger, metric and tracer factories

`Infrastructure` SHALL expose `logger()` for logging, `metrics()` plus `meter(name, version)` returning `core::Result<metrics::Meter*>`, `tracer()` plus `tracer_for(scope)` returning `core::Result<Tracer_type*>`, and `chrono()` returning the attached time-base client handle. Factory conveniences SHALL return `NotInitialized` before initialization; handles SHALL remain valid while the infrastructure is alive.

#### Scenario: Logger access is the application logging path

- **WHEN** application code calls `infra.logger().info("tick {}", n)`
- **THEN** the record goes through the configured logger and is flushed on shutdown

#### Scenario: Metric creation goes through the infrastructure

- **WHEN** application code calls `infra.meter("sim", "1.0")` and creates a counter
- **THEN** the counter belongs to the infrastructure's `MetricProvider` and its value is visible in the provider snapshot

#### Scenario: Tracer creation goes through the infrastructure

- **WHEN** application code calls `infra.tracer_for("sim.control")` and starts a span
- **THEN** the span is accepted and flushed on shutdown

#### Scenario: Chrono access exposes the clock service

- **WHEN** application code calls `infra.chrono()` after initialization
- **THEN** it returns a handle reporting the current time-base status and the coefficient of each required domain, and the handle stops being usable at shutdown

#### Scenario: Factory access before initialization is rejected

- **WHEN** `meter()`, `tracer_for()`, or `chrono()` is called before successful initialization
- **THEN** it returns `ErrorCode::NotInitialized` without dereferencing an empty provider

### Requirement: Infrastructure stop reason is serializable

`InfrastructureStopReason` SHALL provide `None`, `Requested`, `Sigint`, `Sigterm` and `TimeBaseLost` with `to_string`/`from_string` (`none`, `requested`, `sigint`, `sigterm`, `time_base_lost`), non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`, and a `std::formatter` specialization.

#### Scenario: Reason round-trips through JSON

- **WHEN** each `InfrastructureStopReason` value passes through `to_json` then `from_json`
- **THEN** the JSON is the lowercase name and the recovered value equals the original enumerator

#### Scenario: Reason formats directly

- **WHEN** `std::format("{}", InfrastructureStopReason::Sigint)` is evaluated
- **THEN** the result is `"sigint"`

#### Scenario: Lost time base is a distinct reason

- **WHEN** the client stops after holdover expiry
- **THEN** the recorded reason is `time_base_lost` and the final log record names it
