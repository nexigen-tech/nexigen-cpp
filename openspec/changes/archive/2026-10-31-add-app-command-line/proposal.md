# Proposal

## Why

Applications currently have to hardcode or manually assemble the four global/local settings
path pairs (`logging`, `metrics`, `tracing`, `chrono`) and have no command-line way to select
the initial log level. Operators need a stable, boost::program_options-style CLI contract that
points the process at global and local configuration directories and selects a log level before
`Infrastructure::initialize` runs.

## What Changes

- Add a command-line parser in `namespace nexigen::app` with the boost::program_options syntax
  subset used by this project:
  - `-gs, --global-settings <absolute-directory>`
  - `-ls, --local-settings <relative-directory>`
  - `-lgl, --log-level <verbose|debug|info|warning|error|fatal|off>`
  - `-h, --help`
  - `--option=value`, `--option value`, `-o value`, `-ovalue`, `-o=value`, and the `--`
    terminator, including multi-character short names such as `-gs`/`-ls`/`-lgl`.
- Use fixed file names inside both directories: `logging.json`, `metrics.json`,
  `tracing.json`, and `chrono.json`.
- Provide a mapping helper that turns the parsed directories into an `App_settings_paths`
  value with the four global/local pairs; an absent directory leaves that source pair empty
  (disabled), matching the existing loader contract.
- Provide an application helper that applies the CLI log level after `load_app_settings`,
  making the CLI the highest-precedence source for `logging.options.initial_log_level`.
- Add `Infrastructure::initialize(const Command_line_options&, App_settings& loaded)`: it maps
  the directories, loads all four subsystems, applies the CLI level, and then calls the existing
  `initialize(loaded)`. A settings-loader failure is fail-fast (returned without initializing),
  while `loaded` stays populated best-effort; `help = true` is rejected as an initialization
  input. The existing `initialize(const App_settings&)` overload is unchanged.
- Validate strictly: the global directory MUST be absolute, the local directory MUST be
  relative, both MUST be non-empty when supplied, unknown options/missing values/positional
  arguments MUST produce `ErrorCode::InvalidArgument` diagnostics, and the log level MUST be
  one of the exact `nexigen::logging::LogLevel` spellings.
- Require at least one settings directory unless help was requested: when neither
  `--global-settings` nor `--local-settings` is supplied and `--help` is absent, parsing MUST
  fail with a diagnostic stating that one of them is required. The documented application flow
  MUST print that diagnostic to stderr and exit with a non-zero status instead of loading
  built-in defaults.
- Add serializable cold-path types (`Command_line_options`, `Command_line_diagnostic`,
  `Command_line_error`) with member/free `to_string`, `std::formatter`, and JSON where the
  project mandates it.
- Document the CLI in `README.md` and cover the parser, mapping, override, missing-directory
  termination, and error paths in `tests/app/app_test.cpp`.
- No **BREAKING** changes: existing settings loaders, `Infrastructure`, and public APIs keep
  their current behavior.

**Performance, latency, and memory impact:** the parser is cold-path only and runs once before
`Infrastructure::initialize`. It allocates `std::string`/`std::vector` storage during startup
(two directory strings, an optional level, and diagnostics only on failure). It introduces no
hot-path work, no atomics, no locks, and no steady-state allocation; tick/producer latency is
unchanged.

## Capabilities

### New Capabilities

- `app/command-line`: command-line grammar, global/local settings directory mapping to the
  four subsystem file pairs, log-level selection, help handling, and parse diagnostics.

### Modified Capabilities

- `app/application`: `Infrastructure` gains a command-line settings initialization overload
  with fail-fast loading semantics; the existing `initialize(const App_settings&)` contract is
  unchanged.

## Impact

- New public header `include/nexigen/app/command_line.hpp` and cold-path unit
  `src/app/command_line.cpp` (picked up by the existing `src/app/*.cpp` glob).
- `include/nexigen/app/infrastructure.hpp` gains the initialization overload and includes the
  command-line header.
- Tests extended in `tests/app/app_test.cpp`.
- README `nexigen::app` section gains a command-line usage subsection.
- Depends on `nexigen::logging::LogLevel`/`from_string` and the existing
  `App_settings_paths`/`App_settings` types; no new external dependencies.
- No changes to hot paths, concurrency primitives, or existing binary interfaces.
