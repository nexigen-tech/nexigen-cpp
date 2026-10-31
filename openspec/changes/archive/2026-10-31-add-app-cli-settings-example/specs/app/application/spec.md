# Spec Delta

## ADDED Requirements

### Requirement: Runnable command-line settings example

The project SHALL provide an `ex_app_cli_settings` executable that creates a local settings directory and an output directory in the current working directory, writes the four local settings files, parses the standard command-line options, initializes `Infrastructure` through `initialize(options, loaded)`, writes logs, metrics, traces, and chrono snapshots, and exits within one minute.

#### Scenario: Inputs are prepared before parsing

- **WHEN** the example starts in a clean directory
- **THEN** it creates `./nexigen-example-settings/` with `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json`, and `./nexigen-example-output/` before parsing any arguments, with the global settings path left empty

#### Scenario: Existing output directory is cleared

- **WHEN** `./nexigen-example-output/` already contains files from a previous run
- **THEN** the example removes the previous contents before writing new artifacts

#### Scenario: Command-line options drive initialization

- **WHEN** the example is run with `--local-settings <dir> --log-level debug`
- **THEN** parsing uses that local directory, supplies no global directory, and the CLI level overrides the level loaded from the settings files

#### Scenario: Help prints usage without initializing

- **WHEN** the example is run with `--help`
- **THEN** it prints the command-line usage and exits successfully without creating the infrastructure

#### Scenario: All four artifact kinds are written

- **WHEN** the example completes its workload and shuts down
- **THEN** `app.jsonl` contains several log records, `metrics.jsonl` contains updated counter and gauge values, `traces.jsonl` contains a completed span, and `chrono.jsonl` contains at least one timestamp snapshot

#### Scenario: Run is bounded

- **WHEN** the example runs normally
- **THEN** it exits with status 0 within 60 seconds

#### Scenario: Failures are reported

- **WHEN** directory setup, command-line parsing, settings loading, infrastructure initialization, or flushing fails
- **THEN** the example prints a diagnostic to stderr and exits with a non-zero status
