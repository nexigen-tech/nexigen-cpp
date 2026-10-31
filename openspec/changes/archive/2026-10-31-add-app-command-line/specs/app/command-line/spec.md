# Spec Delta

## Purpose

Defines the cold-path command-line contract that maps operator-supplied global/local settings
directories and a log level into the application's existing settings pipeline.

## ADDED Requirements

### Requirement: Long and short option forms are supported

The system SHALL accept `-gs/--global-settings`, `-ls/--local-settings`,
`-lgl/--log-level`, and `-h/--help`. Long options SHALL accept `--name value` and
`--name=value`; short options SHALL accept `-o value`, `-ovalue`, and `-o=value`, including
multi-character short names. The program name in `argv[0]` SHALL be skipped.

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

#### Scenario: Single-letter aliases are rejected

- **WHEN** `-g`, `-l`, or `-L` is supplied
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming the unknown option

#### Scenario: Program name is not parsed

- **WHEN** `argv` is `{"my-app", "--help"}`
- **THEN** parsing succeeds with the help flag set and no diagnostic about `my-app`

### Requirement: Option parsing honors the terminator

The system SHALL treat a standalone `--` argument as the end of option parsing. Arguments after
`--` SHALL be treated as positional arguments, and positional arguments SHALL be rejected.

#### Scenario: Arguments after the terminator are positional

- **WHEN** the arguments are `--local-settings ./config -- extra`
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming `extra`

#### Scenario: A bare terminator is accepted

- **WHEN** the arguments end with `--` and no positional argument follows
- **THEN** parsing succeeds

### Requirement: Global and local directory shapes are validated

`--global-settings` MUST accept a non-empty absolute directory path. `--local-settings` MUST
accept a non-empty relative directory path. A relative global path, an absolute local path, an
empty value, or a missing value MUST yield `ErrorCode::InvalidArgument` with a diagnostic naming
the option.

#### Scenario: Valid directory pair is accepted

- **WHEN** the global directory is `/etc/nexigen` and the local directory is `./hil-config`
- **THEN** parsing succeeds and records both directories unchanged

#### Scenario: Relative global directory is rejected

- **WHEN** `--global-settings ./config` is supplied
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming `--global-settings`

#### Scenario: Absolute local directory is rejected

- **WHEN** `--local-settings /etc/nexigen` is supplied
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming `--local-settings`

#### Scenario: Empty directory value is rejected

- **WHEN** `--global-settings=` or `--local-settings ""` is supplied
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming the option

### Requirement: At least one settings directory is required

At least one of `--global-settings` or `--local-settings` MUST be supplied, except when
`-h`/`--help` short-circuits. When neither is present and help was not requested, parsing MUST
fail with `ErrorCode::InvalidArgument` and a diagnostic whose message states that one settings
directory is required. The documented application flow MUST print that diagnostic to stderr and
exit with a non-zero status rather than loading built-in defaults.

#### Scenario: Neither directory is supplied

- **WHEN** parsing receives no `--global-settings` and no `--local-settings`
- **THEN** parsing returns `InvalidArgument` with a diagnostic stating that one settings
  directory is required

#### Scenario: Only the global directory is supplied

- **WHEN** only `--global-settings /etc/nexigen` is supplied
- **THEN** parsing succeeds and the local source remains disabled

#### Scenario: Only the local directory is supplied

- **WHEN** only `--local-settings ./hil-config` is supplied
- **THEN** parsing succeeds and the global source remains disabled

#### Scenario: Documented flow terminates on the missing-directory error

- **WHEN** the documented application entry point receives no settings directory
- **THEN** it prints the diagnostic and usage text to stderr and returns a non-zero exit status
  without calling the settings loader

### Requirement: Fixed file names produce the four settings pairs

When directories are supplied, the system SHALL join each directory with the fixed names
`logging.json`, `metrics.json`, `tracing.json`, and `chrono.json` and populate the corresponding
`App_settings_paths` global/local pairs. A trailing slash in a directory MUST NOT produce a
double slash. An absent directory SHALL leave that source pair empty.

#### Scenario: Both directories produce eight paths

- **WHEN** the global directory is `/etc/nexigen` and the local directory `./hil-config`
- **THEN** the logging, metrics, tracing, and chrono pairs point at the four fixed file names
  under each directory

#### Scenario: Trailing slash is normalized

- **WHEN** the global directory is `/etc/nexigen/`
- **THEN** the resulting global paths contain `/etc/nexigen/logging.json` and no `//`

#### Scenario: Absent directory disables that source

- **WHEN** only the local directory is supplied
- **THEN** every global path is empty and only the four local paths are populated

### Requirement: Log level accepts canonical enumerator names

`--log-level` MUST accept exactly the lowercase `nexigen::logging::LogLevel` spellings:
`verbose`, `debug`, `info`, `warning`, `error`, `fatal`, and `off`. An unknown or differently
cased value MUST yield `InvalidArgument` with a diagnostic listing the accepted spellings. When
the option is omitted, no override SHALL be recorded.

#### Scenario: Every canonical level is accepted

- **WHEN** each of the seven canonical spellings is supplied to `--log-level`
- **THEN** parsing succeeds and records the matching `LogLevel` value

#### Scenario: Unknown level is rejected

- **WHEN** `--log-level chatty` or `--log-level INFO` is supplied
- **THEN** parsing returns `InvalidArgument` and the diagnostic lists the accepted lowercase names

#### Scenario: Omitted level records no override

- **WHEN** no `--log-level` option is present
- **THEN** the parsed options report no log-level override

### Requirement: CLI log level overrides loaded settings

After settings are loaded, applying the parsed options to `App_settings` SHALL set
`logging.options.initial_log_level` to the CLI value when present, overriding global, local, and
environment sources. When no CLI level is present, the loaded value MUST remain unchanged.

#### Scenario: CLI level wins over files and environment

- **WHEN** a settings file or `NEXIGEN_LOG_LEVEL` selects `debug` and `--log-level warning` is
  supplied
- **THEN** the applied settings carry `warning`

#### Scenario: Absent CLI level preserves the loaded value

- **WHEN** no `--log-level` is supplied and the loader selected `error`
- **THEN** the applied settings keep `error`

### Requirement: Help short-circuits with success

When `-h` or `--help` is present, parsing SHALL succeed, set the help flag, and not require any
other option to be valid. The library SHALL provide usage text naming every supported option and
the fixed file names; the caller is responsible for printing it.

#### Scenario: Help alone succeeds

- **WHEN** the only argument is `--help`
- **THEN** parsing succeeds with the help flag set and no diagnostics

#### Scenario: Help wins over invalid arguments

- **WHEN** `--help` is combined with an unknown option or an invalid directory
- **THEN** parsing still succeeds with the help flag set and no diagnostics

#### Scenario: Usage text lists the contract

- **WHEN** the caller requests the usage text
- **THEN** it names `-gs/--global-settings`, `-ls/--local-settings`, `-lgl/--log-level`,
  `-h/--help`, and the four fixed JSON file names

### Requirement: Unknown, missing, repeated, and positional arguments are handled

Unknown options, options missing a required value, and positional arguments MUST yield
`ErrorCode::InvalidArgument`. A repeated option SHALL keep the last supplied value. Every
diagnostic SHALL identify the offending option or argument.

#### Scenario: Unknown option is rejected

- **WHEN** `--colour` is supplied
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming `--colour`

#### Scenario: Missing value is rejected

- **WHEN** `--global-settings` is the last argument
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming `--global-settings`

#### Scenario: Repeated option keeps the last value

- **WHEN** `--log-level debug --log-level error` is supplied
- **THEN** parsing succeeds and records `error`

#### Scenario: Positional argument is rejected

- **WHEN** a bare `config` argument is supplied without `--`
- **THEN** parsing returns `InvalidArgument` with a diagnostic naming `config`

### Requirement: Command-line types are serializable

`Command_line_options`, `Command_line_diagnostic`, and `Command_line_error` SHALL provide member
and free `to_string()` and a `std::formatter` specialization. All three SHALL provide
non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. Non-object JSON and mistyped
fields SHALL leave the target unchanged.

#### Scenario: Every type renders through to_string and formatter

- **WHEN** `to_string()` and `std::format("{}", value)` are called on options, a diagnostic, and
  an error
- **THEN** each produces the documented cold-path representation

#### Scenario: Diagnostics and errors round-trip through JSON

- **WHEN** a diagnostic or error is written with `to_json` and read back with `from_json`
- **THEN** the recovered fields equal the originals

#### Scenario: Options round-trip through JSON

- **WHEN** parsed options are written with `to_json` and read back with `from_json`
- **THEN** the recovered directories, log level, and help flag equal the originals

#### Scenario: Non-object JSON is ignored

- **WHEN** `from_json` receives an array or a scalar for any command-line type
- **THEN** the target remains unchanged

### Requirement: Missing JSON fields use defaults

`from_json` SHALL start from defaults for an object document: empty directory strings,
`LogLevel::Info` for an absent log level, `false` for an absent help flag, empty diagnostic
strings, and `ErrorCode::InvalidArgument` with no diagnostics for an error. A present `null`
log level SHALL mean no CLI override.

#### Scenario: Missing fields use defaults

- **WHEN** `from_json` receives an object that omits fields
- **THEN** directory paths are empty, the log level is `Info`, help is `false`, diagnostic
  strings are empty, and an error defaults to `InvalidArgument` with no diagnostics

### Requirement: Parsing is cold path only

Parsing, validation, path mapping, and serialization SHALL execute before infrastructure
initialization and MUST NOT be called from a tick, producer, worker, or signal path. They may
allocate `std::string` and `std::vector`; steady-state execution SHALL retain zero allocation and
no locking.

#### Scenario: Tick path is unaffected

- **WHEN** an application parses its command line once, loads settings, then runs ticks
- **THEN** the tick loop performs no parsing, no path mapping, and no allocation

### Requirement: Command-line contract is documented

`README.md` SHALL document the four options, the fixed file names, the absolute/relative path
rules, the CLI log-level precedence, and a minimal parse → map → load → apply example.

#### Scenario: README shows the complete flow

- **WHEN** a reader opens the `nexigen::app` section
- **THEN** it shows how to parse arguments, build `App_settings_paths`, load `App_settings`, and
  apply the CLI log level
