# Spec Delta

## ADDED Requirements

### Requirement: Runnable simple settings example

The project SHALL provide an `ex_logging_settings_simple` executable that demonstrates the shortest loader-to-logger flow: create its own minimal global and local JSON files, load them through explicit `Settings_paths`, initialize a logger from the merged settings, emit a record, flush, and exit 0.

#### Scenario: Example creates its own inputs

- **WHEN** `ex_logging_settings_simple` runs with no arguments and no pre-existing configuration files
- **THEN** it creates a temporary directory with one global JSON and one local JSON file and loads them through explicit paths, without requiring environment variables

#### Scenario: Logger initializes from the merged files

- **WHEN** the example has loaded non-empty global and local settings
- **THEN** `FileLogger` initialization succeeds, at least one record is logged and flushed, and the process exits 0

#### Scenario: Failures are visible

- **WHEN** loading, logger initialization, or flush fails
- **THEN** the example prints the failure (and any loader diagnostics) and exits with a non-zero status
