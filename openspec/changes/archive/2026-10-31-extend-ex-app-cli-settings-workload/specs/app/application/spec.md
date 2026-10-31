# Spec Delta

## MODIFIED Requirements

### Requirement: Runnable command-line settings example

The project SHALL provide an `ex_app_cli_settings` executable that creates a local settings directory and an output directory in the current working directory, writes the four local settings files (with `collect_interval_ns` set to one second for metrics and chrono), parses the standard command-line options, initializes `Infrastructure` through `initialize(options, loaded)`, runs a paced fifteen-second workload, writes logs, metrics, several nested traces, and periodic chrono snapshots, and exits within one minute.

#### Scenario: Inputs are prepared before parsing

- **WHEN** the example starts in a clean directory
- **THEN** it creates `./nexigen-example-settings/` with `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json`, and `./nexigen-example-output/` before parsing any arguments, with the global settings path left empty, and `metrics.json` and `chrono.json` set `collect_interval_ns` to `1000000000`

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
- **THEN** `app.jsonl` contains several log records, `metrics.jsonl` contains updated counter and gauge values, `traces.jsonl` contains the recorded spans across several traces, and `chrono.jsonl` contains timestamp snapshots

#### Scenario: Periodic collection runs every second

- **WHEN** the example runs its fifteen-second workload with the generated settings
- **THEN** `metrics.jsonl` contains at least ten metric batches and `chrono.jsonl` contains at least ten timestamp snapshots produced by the background collection workers configured through the settings files

#### Scenario: Several traces with nested spans

- **WHEN** the example completes its workload and shuts down
- **THEN** `traces.jsonl` contains at least three distinct trace ids, and the recorded spans include parent-child edges, at least two different span kinds, and numeric or string attributes

#### Scenario: Run is bounded

- **WHEN** the example runs normally
- **THEN** its workload spans roughly fifteen seconds of wall time (no less than ten seconds) and the process exits with status 0 within 60 seconds

#### Scenario: Failures are reported

- **WHEN** directory setup, command-line parsing, settings loading, infrastructure initialization, or flushing fails
- **THEN** the example prints a diagnostic to stderr and exits with a non-zero status
