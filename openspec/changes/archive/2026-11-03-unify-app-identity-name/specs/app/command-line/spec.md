# Spec Delta

## ADDED Requirements

### Requirement: App name option

The system SHALL accept `-an`/`--app-name` to name the application for telemetry. When supplied, the value SHALL populate `CommandLineOptions::app_name` and be applied to `Settings::app_name`. When omitted, no app name SHALL be recorded and the application keeps its own default or empty identity.

#### Scenario: App name is parsed

- **WHEN** `--app-name sim-controller` is supplied
- **THEN** the parsed options carry `app_name == "sim-controller"`

#### Scenario: App name accepts all value forms

- **WHEN** `-an sim-controller`, `-ansim-controller`, or `--app-name=sim-controller` is supplied
- **THEN** the parsed `app_name` equals `"sim-controller"` in each form

#### Scenario: Omitted app name records nothing

- **WHEN** no `--app-name` option is present
- **THEN** the parsed `app_name` is empty and no identity is imposed

### Requirement: App name round-trips through serialization

`CommandLineOptions` SHALL serialize `app_name` through `to_json`/`from_json`, include it in `to_string()`, and format it through `std::formatter`.

#### Scenario: App name survives a JSON round-trip

- **WHEN** parsed options with a non-empty `app_name` pass through `to_json` then `from_json`
- **THEN** the recovered `app_name` equals the original value

## MODIFIED Requirements

### Requirement: Long and short option forms are supported

The system SHALL accept `-gs/--global-settings`, `-ls/--local-settings`, `-lgl/--log-level`, `-an/--app-name`, and `-h/--help`. Long options SHALL accept `--name value` and `--name=value`; short options SHALL accept `-o value`, `-ovalue`, and `-o=value`, including multi-character short names. The program name in `argv[0]` SHALL be skipped.

#### Scenario: Long options accept both value forms

- **WHEN** the arguments contain `--global-settings /etc/nexigen` or `--global-settings=/etc/nexigen`
- **THEN** both forms yield the same global directory value

#### Scenario: Short options accept all value forms

- **WHEN** the arguments contain `-lgl info`, `-lglinfo`, or `-lgl=info`
- **THEN** all three forms yield the `info` log level

#### Scenario: Multi-character short names are recognized

- **WHEN** the arguments contain `-gs /etc/nexigen` or `-ls./hil-config`
- **THEN** the global and local directories are populated respectively

#### Scenario: Single-letter aliases are rejected

- **WHEN** `-g`, `-l`, or `-L` is supplied
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming the unknown option

#### Scenario: Program name is not parsed

- **WHEN** `argv` is `{"my-app", "--help"}`
- **THEN** parsing succeeds with the help flag set and no diagnostic about `my-app`
