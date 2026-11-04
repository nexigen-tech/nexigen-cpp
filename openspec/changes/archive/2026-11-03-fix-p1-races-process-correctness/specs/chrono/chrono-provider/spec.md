# chrono/chrono-provider delta spec

## MODIFIED Requirements

### Requirement: Optional worker logger

`ChronoProvider` SHALL support an optional logger binding for its background
worker: a caller MAY bind a logger before `start()` so the worker logs its
diagnostics (startup, affinity result, snapshot delivery failures, shutdown)
through it. The worker SHALL start and run normally without a bound logger. A
`start(Logger&)` call on a provider whose worker is already running SHALL be
rejected with `core::ErrorCode::Conflict` and SHALL NOT touch the logger
binding (no torn half-applied rebinding, no data race with the worker's
binding loads); rebinding is only possible on a provider whose worker is not
running. `start()` without a logger stays idempotent on an already-started
worker.

#### Scenario: Worker logs through the bound logger

- **WHEN** a provider is started with a bound logger and then shut down
- **THEN** the bound logger receives the worker's startup and shutdown
  diagnostics

#### Scenario: Worker runs without a logger

- **WHEN** a provider is started without a logger
- **THEN** snapshots still flow and no logging call occurs

#### Scenario: Double start with a logger is rejected without rebinding

- **WHEN** `start(Logger&)` completes successfully and is called again with a
  logger while the worker runs
- **THEN** the call returns `Conflict`, the second logger handle is NOT
  installed (the worker keeps using the first binding), and no data race is
  possible on the binding (TSan-clean)

#### Scenario: Clock log level serializes

- **WHEN** each `Clock_log_level` value passes through `to_string`/`from_string`,
  `to_json`/`from_json`, and `std::format`
- **THEN** the value round-trips and the formatter renders the canonical name
