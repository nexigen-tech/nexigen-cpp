# Spec Delta

## MODIFIED Requirements

### Requirement: Suite manifest has a fixed schema

The manifest SHALL be a JSON object with `version: 1`, `suite_id`, `run_id`, absolute `run_dir` and `artifacts_path`, optional absolute `global_settings_path`, optional `runner_name`, `log_level`, `suite_restart_attempts`, `suite_restart_delay_s`, and a non-empty `apps` array. Each entry SHALL define its identity, command and arguments, impact and restart policy, timeouts, and optional environment, affinity, settings paths, and log level.

#### Scenario: Complete manifest is accepted

- **WHEN** every required suite and application field is supplied with a valid value
- **THEN** the manifest is accepted and each application carries exactly the supplied command, arguments, environment, CPU list, timeouts, restart counts, settings paths, log level, and impact level

#### Scenario: Optional fields default deterministically

- **WHEN** an application omits `env`, `cpus`, `global_settings_path`, `local_settings_path`, and `log_level`
- **THEN** it launches with no additional environment entries, no CPU restriction, the suite global settings path, no local settings source, and the suite log level

#### Scenario: Runner name is optional

- **WHEN** the manifest omits `runner_name`
- **THEN** the runner uses its default runner name for its own telemetry

#### Scenario: Unknown fields are ignored

- **WHEN** the manifest contains fields the runner does not recognize
- **THEN** parsing succeeds and known fields keep their supplied values
