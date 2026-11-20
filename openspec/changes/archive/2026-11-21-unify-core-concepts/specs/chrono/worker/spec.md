# Spec Delta

## MODIFIED Requirements

### Requirement: Worker logger binding

`ChronoWorker` SHALL support an optional logger binding for the daemon thread: a caller MAY bind a logger before `start()`, and the worker SHALL log its diagnostics (startup, attribute result, delivery failures, shutdown) through a `log(core::LogLevel, std::string_view)` call shape constrained by `core::concepts::LevelLogger`. The worker SHALL start and run normally without a bound logger.

#### Scenario: Worker logs through the bound logger

- **WHEN** a worker is started with a bound logger and shut down
- **THEN** the logger receives the worker's startup and shutdown diagnostics

#### Scenario: Worker runs without a logger

- **WHEN** a worker is started without a logger
- **THEN** snapshots still flow and no logging call occurs

#### Scenario: Non-conforming logger is rejected

- **WHEN** a probe logger lacks `log(LogLevel, std::string_view) noexcept`
- **THEN** binding it fails at compile time against `core::concepts::LevelLogger`
