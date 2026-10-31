# Tasks

## 1. Command-line types and parser core

- [x] 1.1 Add `include/nexigen/app/command_line.hpp` with `Command_line_diagnostic`, `Command_line_options`, `Command_line_error`, `Command_line_result`, the parser/path/apply/usage declarations, and the required includes; verify the header compiles by building `nexigen_app_tests` after adding it to the test TU.
- [x] 1.2 Implement long-option parsing (`--name value`, `--name=value`) and multi-character short-option parsing (`-gs value`, `-gsvalue`, `-gs=value`, and the same forms for `-ls` and `-lgl`) in `src/app/command_line.cpp`, assigning values with last-wins semantics; verify with new tests for each accepted form and for repeated options.
- [x] 1.3 Implement the help pre-scan (`-h`/`--help` wins over other arguments) and the `--` terminator with positional rejection; verify tests for help alone, help plus invalid input, a bare `--`, and arguments after `--`.
- [x] 1.4 Implement diagnostics for unknown options, missing values, empty values, and positional arguments, returning `Command_line_error{ErrorCode::InvalidArgument, ...}`; verify each case produces a diagnostic naming the offending token.

## 2. Directory and log-level validation

- [x] 2.1 Enforce that a supplied global directory is non-empty and absolute and a supplied local directory is non-empty and relative; verify tests for a valid pair, relative global, absolute local, and empty values.
- [x] 2.2 Parse `--log-level` and `-lgl` with `nexigen::logging::from_string`, rejecting unknown or mixed-case spellings and listing the accepted lowercase names in the diagnostic; verify one test per canonical level plus invalid/mixed-case cases.
- [x] 2.3 Implement `command_line_usage()` as a static `std::string_view` naming all four options, the seven level names, and the four fixed JSON file names; verify a test checks every required token.
- [x] 2.4 Reject the case where neither directory is supplied (with help absent) with `InvalidArgument` and a diagnostic stating that one settings directory is required; verify tests for neither, global-only, local-only, and help-only inputs.

## 3. Path mapping and application helpers

- [x] 3.1 Implement `make_app_settings_paths` with the fixed names `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json`, joining directories without double slashes and leaving absent directories empty; verify tests for eight populated paths, trailing-slash normalization, and a single supplied directory.
- [x] 3.2 Implement `apply_to(const Command_line_options&, App_settings&)` to override `logging.options.initial_log_level` only when the CLI level is present; verify tests for CLI-wins and absent-preserves behavior.
- [x] 3.3 Implement the `load_app_settings(const Command_line_options&, App_settings&)` convenience overload that maps paths, calls the existing loader, applies the CLI level even on a loader failure, and returns `InvalidArgument` when both directories are empty; verify tests for OK, invalid-file status, the applied override, and the both-empty rejection.
- [x] 3.4 Add `Infrastructure::initialize(const Command_line_options&, App_settings& loaded)` to `include/nexigen/app/infrastructure.hpp`, delegating to the existing initialization path after a successful load; verify a test initializes from valid options and confirms the CLI level and loaded settings, and that the existing `initialize(settings)` overload still works.
- [x] 3.5 Implement fail-fast behavior in the new overload: return the loader status without claiming signal ownership or creating components, keep `loaded` populated best-effort, reject `help = true`, and allow a later explicit `initialize(loaded)`; verify tests for loader failure, help rejection, and degraded startup.

## 4. Serialization and formatting

- [x] 4.1 Implement member/free `to_string()` and `std::formatter` specializations for `Command_line_options`, `Command_line_diagnostic`, and `Command_line_error`; verify tests compare `std::format("{}", value)` with `to_string(value)` for all three.
- [x] 4.2 Implement `to_json` for all three types and non-throwing `from_json` for all three under `JSON_NOEXCEPTION=1`; default absent fields (empty paths, `LogLevel::Info`, `help=false`, empty diagnostics, `InvalidArgument`), keep `null` as no CLI override, and verify round-trip tests plus non-object/mistyped input leaving targets unchanged.

## 5. Documentation

- [x] 5.1 Add a command-line settings subsection to the `nexigen::app` section of `README.md` covering the four options, fixed file names, absolute/relative rules, the requirement that at least one settings directory is supplied, CLI log-level precedence, and a minimal parse → `Infrastructure::initialize(options, loaded)` example (with the manual map/load/apply sequence as an alternative) that prints diagnostics plus usage to stderr and returns a non-zero status when parsing fails; verify the example compiles against the public headers and the documented file names match the implementation.

## 6. Build and sanitizer matrix

- [x] 6.1 Build and run the full unit-test suite in Debug (`cmake --build build/debug` and `ctest --test-dir build/debug --output-on-failure`) and verify 100% of tests pass.
- [x] 6.2 Build and run the full unit-test suite in Release with the project's latency gates (`-O3 -march=native -flto`) and verify 100% of tests pass.
- [x] 6.3 Build and run the full unit-test suite under ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`) and verify 100% of tests pass without sanitizer diagnostics.
- [x] 6.4 Build and run the full unit-test suite under AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`) and verify 100% of tests pass without sanitizer diagnostics.

## Workflow follow-up

- Archive the change after implementation and review are complete.
- Sync the `app/command-line` delta into the main specs during archive.
