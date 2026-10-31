# Spec Delta

## MODIFIED Requirements

### Requirement: Runnable example catalogue

The project SHALL provide self-contained executables named `ex_spsc_ring_buffer`, `ex_console_logger`, `ex_file_logger`, `ex_composite_logger`, `ex_logging_settings`, `ex_logging_settings_simple`, `ex_counters_and_gauges`, `ex_descriptive_statistics`, `ex_histograms`, `ex_metric_provider_and_sinks`, `ex_nested_and_cross_thread_spans`, `ex_perfetto_chrome_trace`, `ex_stopwatch`, `ex_stopwatch_histogram`, `ex_app_lifecycle`, and `ex_app_cli_settings`, under `examples/core/`, `examples/logging/`, `examples/metrics/`, `examples/tracing/`, `examples/diagnostics/`, and `examples/app/` respectively — each with its own `main`, no dependency on test harness code.

#### Scenario: Example executables exist after a default build

- **WHEN** a default build completes
- **THEN** all sixteen example executables exist in the build tree and can be launched directly without arguments

#### Scenario: Examples are self-contained

- **WHEN** an example is built and run outside the test harness
- **THEN** it creates its own inputs and configuration, prints its diagnostics, and does not require a test runner, fixture, or pre-existing file

### Requirement: Documented example invocation

`README.md` SHALL contain a Runnable Examples section that names all sixteen executables, shows how to configure/build with examples enabled, shows how to run each executable, documents the `run_ex_app_cli_settings.sh` and `clean_ex_app_cli_settings.sh` helper scripts and the directories they manage, and documents the `NEXIGEN_BUILD_EXAMPLES=OFF` opt-out.

#### Scenario: Reader can build and run an example from the README

- **WHEN** a reader follows the Runnable Examples section commands from a clean checkout
- **THEN** the examples are built, each named executable can be run, and the opt-out configuration is documented

#### Scenario: Reader can run and clean the command-line settings example

- **WHEN** a reader follows the documented helper-script commands
- **THEN** the runner builds and executes `ex_app_cli_settings`, and the cleanup script removes the generated settings and output directories

#### Scenario: Stale duplicated build notes are removed

- **WHEN** the README is read to its end
- **THEN** it ends with the license section and contains no leftover untranslated or duplicated build-mode appendix

## ADDED Requirements

### Requirement: Example runner and cleanup scripts

The project SHALL provide `examples/app/run_ex_app_cli_settings.sh` and `examples/app/clean_ex_app_cli_settings.sh`. The runner SHALL build the example when needed and execute it from the repository root under a 60-second timeout; the cleanup script SHALL remove the generated `nexigen-example-settings/` and `nexigen-example-output/` directories idempotently.

#### Scenario: Runner builds and executes the example

- **WHEN** `run_ex_app_cli_settings.sh` is invoked from the repository root
- **THEN** it builds `ex_app_cli_settings` if necessary, runs it under a 60-second timeout, and returns the example's exit status

#### Scenario: Runner reports build or run failures

- **WHEN** the build or the example fails
- **THEN** the runner prints the failure and exits with a non-zero status

#### Scenario: Cleanup removes generated directories

- **WHEN** `clean_ex_app_cli_settings.sh` is invoked after a run
- **THEN** both generated directories are removed

#### Scenario: Cleanup is idempotent

- **WHEN** the cleanup script runs and the generated directories are already absent
- **THEN** it exits successfully without reporting an error
