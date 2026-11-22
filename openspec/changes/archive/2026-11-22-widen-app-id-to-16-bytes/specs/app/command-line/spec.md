# Spec Delta

## MODIFIED Requirements

### Requirement: Long and short option forms are supported

The system SHALL accept `-gs/--global-settings`, `-ls/--local-settings`,
`-lgl/--log-level`, `-an/--app-name`, `-ai/--app-id`, and `-h/--help`. Long options SHALL accept
`--name value` and `--name=value`; short options SHALL accept `-o value`, `-ovalue`, and
`-o=value`, including multi-character short names. The program name in `argv[0]` SHALL be skipped.

#### Scenario: Long options accept both value forms

- **WHEN** the arguments contain `--global-settings /etc/nexigen` or
  `--global-settings=/etc/nexigen`
- **THEN** both forms yield the same global directory value

#### Scenario: Short options accept all value forms

- **WHEN** the arguments contain `-lgl info`, `-lglinfo`, or `-lgl=info`
- **THEN** all three forms yield the `info` log level

#### Scenario: Multi-character short names are recognized

- **WHEN** the arguments contain `-gs /etc/nexigen` or `-ls./hil-config`
- **THEN** the global and local directories are populated respectively

#### Scenario: App identifier accepts all value forms

- **WHEN** the arguments contain `-ai CN_LOOP_1`, `-aiCN_LOOP_1`, or `--app-id=CN_LOOP_1`
- **THEN** all three forms yield the identifier `CN_LOOP_1`

#### Scenario: Single-letter aliases are rejected

- **WHEN** `-g`, `-l`, `-L`, or `-a` is supplied without a recognized multi-character name
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming the unknown option

#### Scenario: Program name is not parsed

- **WHEN** `argv` is `{"my-app", "--help"}`
- **THEN** parsing succeeds with the help flag set and no diagnostic about `my-app`

## ADDED Requirements

### Requirement: App identifier option

The system SHALL accept `-ai`/`--app-id` to set the application's canonical telemetry identifier.
When supplied, the value SHALL populate `CommandLineOptions::app_id` and be applied to
`Settings::app_id`. A value that is empty, longer than 16 bytes, or contains a byte outside
`A-Z`, `a-z`, `0-9`, `_`, and `-` SHALL fail parsing with `InvalidArgument` and a diagnostic naming
the offending value. When omitted, no identifier SHALL be recorded and the policy keeps its
fallback identity.

#### Scenario: Identifier is parsed

- **WHEN** `--app-id CN_LOOP_1` is supplied
- **THEN** the parsed options carry the identifier `CN_LOOP_1`

#### Scenario: Identifier is applied to settings

- **WHEN** parsed options carrying `--app-id CN_LOOP_1` are applied to `Settings`
- **THEN** `Settings::app_id` renders as `CN_LOOP_1`

#### Scenario: Oversized identifier is rejected

- **WHEN** `--app-id ABCDEFGHIJKLMNOPQ` is supplied
- **THEN** parsing fails with `InvalidArgument` and no truncated identifier is recorded

#### Scenario: Invalid characters are rejected

- **WHEN** the supplied identifier contains a space, quote, backslash, slash, dot, or non-ASCII
  byte
- **THEN** parsing fails with `InvalidArgument` and the offending value is named

#### Scenario: Omitted identifier records nothing

- **WHEN** no `--app-id` option is present
- **THEN** `CommandLineOptions::app_id` is unset and no identity is imposed

### Requirement: App identifier round-trips through serialization

`CommandLineOptions` SHALL serialize `app_id` through `to_json`/`from_json`, include it in
`to_string()`, and format it through `std::formatter`. Invalid JSON SHALL leave the destination
unchanged.

#### Scenario: Identifier survives a JSON round-trip

- **WHEN** parsed options with `app_id` `CN_LOOP_1` pass through `to_json` then `from_json`
- **THEN** the recovered identifier equals the original value

#### Scenario: Invalid identifier JSON is ignored

- **WHEN** `from_json` receives `app_id` as a number, an oversized string, or an invalid string
- **THEN** the destination keeps its previous value and no exception escapes

#### Scenario: Formatter matches the diagnostic text

- **WHEN** `std::format("{}", options)` is evaluated for options with a set identifier
- **THEN** the output equals `options.to_string()` and contains the identifier
