# Spec Delta

## ADDED Requirements

### Requirement: Command-line options initialize the infrastructure

`Infrastructure::initialize(const Command_line_options& options, App_settings& loaded)` SHALL
map the parsed directories to `App_settings_paths`, call `load_app_settings`, apply the CLI log
level to `loaded`, and, only when loading succeeds, call the existing
`initialize(const App_settings&)` overload. The existing overload SHALL remain unchanged.

#### Scenario: Valid options initialize from loaded settings

- **WHEN** parsed options name a readable global or local settings directory and initialization
  is called with them
- **THEN** `loaded` contains the four merged subsystem settings, the CLI level is applied, and
  the infrastructure initializes through the existing path

#### Scenario: Existing settings overload still works

- **WHEN** application code calls `initialize(const App_settings&)` directly
- **THEN** it behaves exactly as before, without parsing or loading files

### Requirement: Settings-loading failures are fail-fast

When `load_app_settings` returns a failure, the command-line initialization overload MUST return
that status without claiming signal ownership, creating components, or starting workers.
`loaded` MUST still contain the best-effort settings and diagnostics so the caller can inspect
them or explicitly call `initialize(loaded)` to proceed in a degraded mode.

#### Scenario: Loader failure skips initialization

- **WHEN** a supplied settings file exists but cannot be read, or contains an invalid value
- **THEN** the overload returns the loader's failure status and no infrastructure component is
  initialized

#### Scenario: Best-effort settings remain available

- **WHEN** the loader reports a diagnostic and returns a failure
- **THEN** `loaded` still carries the merged defaults and every subsystem diagnostic for the
  caller to print or act on

#### Scenario: Caller can opt into degraded startup

- **WHEN** the caller decides to continue after inspecting `loaded`
- **THEN** calling `initialize(loaded)` starts the infrastructure with those best-effort settings

### Requirement: Help is rejected as an initialization input

When `options.help` is true, the command-line initialization overload MUST return
`ErrorCode::InvalidArgument` without loading settings or initializing the infrastructure. Help
is a caller action (print usage and exit), not a startup request.

#### Scenario: Help options are not initialized

- **WHEN** `initialize(options, loaded)` receives options with `help = true`
- **THEN** it returns `InvalidArgument` and does not touch the settings files or any component

#### Scenario: Missing directories are rejected

- **WHEN** a manually constructed options value has both directory fields empty
- **THEN** the overload returns `InvalidArgument` through the settings-loading path and does not
  initialize
