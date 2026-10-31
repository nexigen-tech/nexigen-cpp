# Design

## Context

See `proposal.md` for motivation and `specs/app/command-line/spec.md` for the behavioral
contract. The existing `nexigen::app` settings pipeline already exposes `App_settings_paths`
(four global/local pairs), `load_app_settings`, and `App_settings`; the CLIs only need to
produce those inputs before `Infrastructure::initialize`. All work is cold path, so the strict
hot-path allocation/locking rules do not apply, but the public types must still follow the
project-wide `to_string`/formatter/JSON mandates and the `-fno-exceptions` error model.

## Goals / Non-Goals

**Goals:**

- A small, dependency-free parser in `nexigen::app` with a boost::program_options-style subset.
- A deterministic mapping from two directories to the eight existing settings paths.
- A documented precedence rule for the CLI log level.
- Structured diagnostics that survive `-fno-exceptions` and are serializable.

**Non-Goals:**

- Implicit configuration discovery, config-file inclusion, environment substitution, or
  subcommand/positional parsing.
- Changing `App_settings`, the loaders, `Infrastructure`, or any hot path.
- Filesystem existence checks during parsing; the loaders already treat missing files as an
  absent source.
- Shell-like quoting or tokenization; `argv` arrives already split.

## Decisions

### D1. Public surface and file layout

Add `include/nexigen/app/command_line.hpp` and `src/app/command_line.cpp`. The source is picked
up by the existing `src/app/*.cpp` glob, so no CMake source-list change is needed.

```cpp
namespace nexigen::app {

struct Command_line_diagnostic {
    std::string option{};   // offending option/argument, e.g. "--global-settings"
    std::string message{};  // human-readable explanation
    [[nodiscard]] std::string to_string() const;
};

struct Command_line_options {
    std::string global_settings_dir{};              // empty = source disabled
    std::string local_settings_dir{};               // empty = source disabled
    std::optional<logging::LogLevel> log_level{};   // empty = no CLI override
    bool help{false};
    [[nodiscard]] std::string to_string() const;
};

struct Command_line_error {
    core::ErrorCode code{core::ErrorCode::InvalidArgument};
    std::vector<Command_line_diagnostic> diagnostics{};
    [[nodiscard]] std::string to_string() const;
};

using Command_line_result = core::Result<Command_line_options, Command_line_error>;

[[nodiscard]] Command_line_result parse_command_line(
    int argc, const char* const argv[]) noexcept;
[[nodiscard]] Command_line_result parse_command_line(
    std::span<const std::string_view> args) noexcept;
[[nodiscard]] App_settings_paths make_app_settings_paths(
    const Command_line_options& options) noexcept;
void apply_to(const Command_line_options& options, App_settings& settings) noexcept;
[[nodiscard]] core::Status load_app_settings(
    const Command_line_options& options, App_settings& out) noexcept;
[[nodiscard]] std::string_view command_line_usage() noexcept;

} // namespace nexigen::app
```

`parse_command_line(argc, argv)` skips `argv[0]`; the span overload treats every element as an
argument and exists so tests do not need fake `argv` storage.

**Alternatives considered:** returning `Status` plus an out-parameter would lose per-option
diagnostics; a custom exception type is impossible under `-fno-exceptions`. `Result<T,
Command_line_error>` keeps the success value and the full diagnostic list in one value.

### D2. Grammar and parser algorithm

Supported tokens:

| Form | Meaning |
| --- | --- |
| `-gs DIR`, `-gsDIR`, `-gs=DIR` | global settings directory |
| `--global-settings DIR`, `--global-settings=DIR` | same |
| `-ls DIR`, `-lsDIR`, `-ls=DIR` | local settings directory |
| `--local-settings DIR`, `--local-settings=DIR` | same |
| `-lgl LEVEL`, `-lglLEVEL`, `-lgl=LEVEL` | log level |
| `--log-level LEVEL`, `--log-level=LEVEL` | same |
| `-h`, `--help` | help flag |
| `--` | end of option parsing |

Algorithm:

1. Pre-scan tokens up to the first standalone `--` for an exact `-h`/`--help`. If found,
   return success immediately with `help = true`; help must not depend on other arguments.
2. Iterate the remaining tokens left to right:
   - `--`: mark that option parsing is finished; every subsequent token is positional.
   - `--name` / `--name=value`: look up the known long name. An unknown name or a missing
     value records a diagnostic. Without `=`, consume the next token as the value only when it
     does not look like another option (length > 1 and starts with `-` means "missing value";
     use `--name=value` for such values).
   - `-name` / `-namevalue` / `-name=value`: match the token against the exact short names
     `-gs`, `-ls`, `-lgl`, and the flag `-h`. The registered short names are mutually
     non-prefixing, so declaration order is unambiguous. For value-taking names, use the next
     token when the token equals the name exactly, or the adjacent suffix when the name is a
     prefix (strip one leading `=`). A bare `-`, an unknown short name (including `-g`, `-l`,
     `-L`), or `-hfoo` records a diagnostic.
   - Anything else: positional argument diagnostic.
3. Assign values as they are parsed; a repeated option overwrites the previous value (last
   wins), matching boost::program_options.
4. Validate the collected values:
   - At least one of the global or local directory must be present; if both are empty, record
     a diagnostic whose message states that one settings directory is required.
   - A supplied global directory must be non-empty and start with `/`.
   - A supplied local directory must be non-empty and not start with `/`.
   - A supplied log level must satisfy `logging::from_string` exactly (lowercase canonical
     names). The diagnostic lists all accepted names.
5. Return `unexpected(Command_line_error{ErrorCode::InvalidArgument, diagnostics})` if any
   diagnostic exists; otherwise return the options.

Empty values supplied explicitly (`--global-settings=`) are rejected, not treated as
"disabled". Omitting the option is the only way to disable a source. Positional arguments are
never accepted; the program has no positional interface.

**Alternatives considered:** `getopt_long` would add a C API and make diagnostics/`Result`
integration awkward. A full boost::program_options dependency is disproportionate for four
options and would add an external dependency to a core library.

### D3. Directory-to-path mapping

Fixed names, shared by both directories:

```
logging.json
metrics.json
tracing.json
chrono.json
```

`make_app_settings_paths` fills `App_settings_paths` as follows:

- `logging.global_file` / `logging.local_file`
- `metrics.global_file` / `metrics.local_file`
- `tracing.global_file` / `tracing.local_file`
- `chrono.global_file` / `chrono.local_file`

Joining is a pure string operation: start from the directory; append `/` only when the
directory does not already end with `/`; append the fixed name. An empty directory yields an
empty path (source disabled). No filesystem call, no existence check, and no path
canonicalization happen here; the existing loaders keep their `ENOENT`-as-absent behavior.

**Alternatives considered:** `std::filesystem::path` would add lexical normalization and
possible exception surface for no benefit; the contract is POSIX/Linux and the directories are
already validated as absolute/relative.

### D4. Log-level precedence

`apply_to(options, settings)` only touches
`settings.logging.options.initial_log_level` when `options.log_level` is engaged. It is called
after `load_app_settings`, so the CLI value wins over the loader's global → local →
environment chain without mutating process environment variables. If the option is absent, the
loaded value is preserved.

The convenience overload `load_app_settings(options, out)` builds the paths, calls the existing
`load_app_settings(paths, out)`, applies the CLI level even when the loader reports a
diagnostic, and returns the loader's status. This keeps the "best effort" contract: a bad file
does not discard an explicitly requested CLI level.

**Alternatives considered:** setting `NEXIGEN_LOG_LEVEL` before loading would mutate global
process state, complicate tests, and make the precedence implicit. Applying the level after the
load is deterministic and local.

### D5. Missing-directory behavior

If both directory fields are empty after the help short-circuit, the parser appends a diagnostic
whose option is `--global-settings/--local-settings` and whose message states that one settings
directory is required, then returns `InvalidArgument`. The parser never prints or exits: it is a
library, and printing belongs to the host application.

The documented application flow treats this diagnostic as fatal: it writes the diagnostic and
`command_line_usage()` to stderr and returns a non-zero status before calling
`load_app_settings`. The README uses exit code `2` for usage errors; the library does not
prescribe a specific code, only "non-zero". This keeps the behavior testable and lets hosts map
the failure to their own exit-code convention.

The convenience `load_app_settings(options, out)` also returns `InvalidArgument` when both
directory fields are empty, so a manually constructed options value cannot silently load
built-in defaults. The parser remains the primary source of the user-facing diagnostic.

**Alternatives considered:** printing from the parser would make a reusable library write to
the process's stderr and call `std::exit`, which is hostile to tests, embedding, and hosts with
their own logging. Returning structured diagnostics preserves that choice.

### D6. Infrastructure integration

Add one overload to `Infrastructure` in `include/nexigen/app/infrastructure.hpp`:

```cpp
[[nodiscard]] core::Status initialize(const Command_line_options& options,
                                      App_settings& loaded) noexcept;
```

Implementation order:

1. If `options.help` is true, return `InvalidArgument`; help is a caller action.
2. Default-construct a local `App_settings settings{}` and call
   `load_app_settings(options, settings)` (the convenience overload from D4).
3. Move the best-effort result into the caller's `loaded` parameter before inspecting the
   status, so diagnostics survive even on failure.
4. If the load status is a failure, return it **without** claiming signal ownership, creating
   components, or starting workers (fail fast).
5. Otherwise call the existing `initialize(loaded)` and return its status.

The out-parameter is necessary because the settings diagnostics live in `App_settings`
(`logging`/`metrics`/`tracing`/`chrono` members), not in `core::Status`; returning only a status
would discard the information the caller needs to print. The overload is additive and leaves the
existing `initialize(const App_settings&)` path untouched.

Fail-fast is deliberate: an unreadable or invalid settings file should not silently start
real-time workers with defaults. The caller can still inspect `loaded` and explicitly call
`initialize(loaded)` to opt into degraded startup. `Infrastructure` remains free of printing,
`argc/argv`, and `std::exit`; command-line parsing stays a separate cold-path step.

`command_line.hpp` includes `app_settings.hpp` and `logging/log_level.hpp`; `infrastructure.hpp`
includes `command_line.hpp`. There is no cycle because `command_line.hpp` does not include
`infrastructure.hpp`.

**Alternatives considered:** an end-to-end `make_infrastructure_from_command_line<Policy>(argc,
argv)` would hide the parse/print/exit policy inside the library and complicate the return type;
a raw `initialize(argc, argv)` overload would make `Infrastructure` depend on process-global
argument conventions. The out-parameter overload composes with the existing lifecycle and keeps
the failure policy explicit.

### D7. Serialization and diagnostics

Implement the project mandates on the three new structs:

- Member `to_string()` plus a free `to_string(const T&)` and `std::formatter<T>` for
  `Command_line_options`, `Command_line_diagnostic`, and `Command_line_error`.
- `to_json`/`from_json` for all three types. `from_json` starts from defaults so an object that
  omits fields is deterministic: empty directory strings, `LogLevel::Info` for an absent log
  level, `false` for an absent help flag, empty diagnostic strings, and
  `ErrorCode::InvalidArgument` with no diagnostics for an error. A present `null` log level
  means no CLI override.
- `from_json` is non-throwing under `JSON_NOEXCEPTION=1`: non-object input and mistyped fields
  return immediately without modifying the target; string fields are validated with
  `is_string()` and read with `get<std::string>()`; a `log_level` string is parsed with
  `logging::from_string`.

Representations:

```
Command_line_diagnostic(option="--log-level", message="...")
Command_line_options(global_settings_dir="/etc/nexigen", local_settings_dir="./hil-config",
                     log_level=warning, help=false)
Command_line_error(code=InvalidArgument, diagnostics=[...])
```

`command_line_usage()` returns a static `std::string_view` naming every option, the accepted
level spellings, and the four fixed file names. It allocates nothing and never fails.

### D8. Memory and concurrency

There is no shared state, no atomic, no lock, and no worker: parsing runs on the startup thread
before `Infrastructure::initialize`. Consequently there is no `alignas(64)` requirement and no
`std::memory_order` choice to make. `Command_line_options` holds two `std::string`s, an
`std::optional<LogLevel>`, and a `bool`; `Command_line_diagnostic` holds two `std::string`s; the
error holds a `core::ErrorCode` and a `std::vector`. All allocations are startup-only
(`std::string`, vector growth on diagnostics). The success path performs no JSON work and no
filesystem work.

### D9. Testing strategy

Extend `tests/app/app_test.cpp` with a `command_line` suite covering:

- long, short, `=`, and adjacent-value forms; `argv[0]` skipping;
- help alone and help combined with invalid input; `--` handling;
- unknown, missing, empty, repeated, and positional arguments;
- absolute/relative validation for both directories, plus neither/global-only/local-only cases
  and the missing-directory diagnostic;
- all seven log-level names, invalid and mixed-case spellings;
- eight-path mapping, trailing-slash normalization, and absent directories;
- CLI override/preserve behavior and the combined loader, including its both-empty rejection;
- `Infrastructure::initialize(options, loaded)` success, fail-fast on a loader error, help
  rejection, and explicit degraded startup via `initialize(loaded)`;
- `to_string`/formatter/JSON round-trips and non-object JSON rejection.

The suite runs in the existing `nexigen_app_tests` target; no new executable or CMake source
list is needed.

## Risks / Trade-offs

- **Shape validation does not prove existence** → The loaders deliberately treat missing files
  as an absent source; the README states this and tests cover a non-existent directory.
- **A value starting with `-` looks like an option** → Use `--name=value`, `-nvalue`, or
  `-n=value`; documented in the usage text and README.
- **Help pre-scan changes normal precedence** → Intentional: `--help` must always be printable
  even when other arguments are wrong.
- **CLI level is applied even when settings loading failed** → Intentional best effort; the
  caller still receives the loader's failure status and diagnostics.
- **Fixed file names are a contract** → Any rename is a breaking CLI change; the four names are
  pinned in tests and the usage text.

## Migration Plan

Additive only. No existing type, signature, loader, or behavior changes. Rollback is removing
the new header/source, the test suite, and the README subsection; no data migration is needed.

## Open Questions

None.
