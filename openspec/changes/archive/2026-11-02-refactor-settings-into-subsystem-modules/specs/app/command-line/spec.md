# Spec Delta

## MODIFIED Requirements

### Requirement: Fixed file names produce the four settings pairs

When directories are supplied, the system SHALL join each directory with the fixed names `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json` and populate the corresponding `AppSettingsPaths` global/local pairs. A trailing slash in a directory MUST NOT produce a double slash. An absent directory SHALL leave that source pair empty.

#### Scenario: Both directories produce eight paths

- **WHEN** the global directory is `/etc/nexigen` and the local directory `./hil-config`
- **THEN** the logging, metrics, tracing, and chrono pairs point at the four fixed file names under each directory

#### Scenario: Trailing slash is normalized

- **WHEN** the global directory is `/etc/nexigen/`
- **THEN** the resulting global paths contain `/etc/nexigen/logging.json` and no `//`

#### Scenario: Absent directory disables that source

- **WHEN** only the local directory is supplied
- **THEN** every global path is empty and only the four local paths are populated

### Requirement: CLI log level overrides loaded settings

After settings are loaded, applying the parsed options to `Settings` SHALL set `logging.options.initial_log_level` to the CLI value when present, overriding global, local, and environment sources. When no CLI level is present, the loaded value MUST remain unchanged.

#### Scenario: CLI level wins over files and environment

- **WHEN** a settings file or `NEXIGEN_LOG_LEVEL` selects `debug` and `--log-level warning` is supplied
- **THEN** the applied settings carry `warning`

#### Scenario: Absent CLI level preserves the loaded value

- **WHEN** no `--log-level` is supplied and the loader selected `error`
- **THEN** the applied settings keep `error`

### Requirement: Command-line contract is documented

`README.md` SHALL document the four options, the fixed file names, the absolute/relative path rules, the CLI log-level precedence, and a minimal parse → map → load → apply example.

#### Scenario: README shows the complete flow

- **WHEN** a reader opens the `nexigen::app` section
- **THEN** it shows how to parse arguments, build `AppSettingsPaths`, load `Settings`, and apply the CLI log level
