# Tasks

## 1. Remove app_name from logging

- [x] 1.1 Remove `app_name` from `logging::LoggerOptions` (header + `logger_options_format.cpp` serialization + `to_string`). Verify: logging tests that reference `LoggerOptions.app_name` are updated and the logger-options suite passes.
- [x] 1.2 Remove the `app_name` key, the `NEXIGEN_LOG_APP_NAME` override, and the local-only app-name requirement from `logging_settings.{hpp,cpp}`. Verify: `load_logging_settings` no longer produces an app name, and the logging-settings suite passes.
- [x] 1.3 Stop mapping `app_name` in `logging::apply_to` (the `FileLoggerConfig.app_name` is no longer sourced from logging settings). Verify: `logging_settings_apply` tests pass with the app-name mapping removed.

## 2. App name in app settings and CLI

- [x] 2.1 Add `std::string app_name{}` to `app::Settings` and include it in `Settings::to_string()` / `to_json`. Verify: the app/settings serialization tests cover `app_name`.
- [x] 2.2 Add the `-an`/`--app-name` option to `command_line.{hpp,cpp}` (`CommandLineOptions.app_name`), its serialization, and the usage text; apply it into `Settings.app_name` in the CLI `apply_to`. Verify: command-line tests parse and round-trip `--app-name`.

## 3. RunIdentity and runner app_name threading

- [x] 3.1 Remove `app_name` from `app::RunIdentity` (header, `load_run_identity`, `validate_run_identity`, `to_json`/`from_json`, `resolve_telemetry_paths`) and take the application name as a parameter for instance-directory derivation. Verify: telemetry-layout tests pass with the app name supplied separately.
- [x] 3.2 Add `app_name` as a parameter to `Runner::start` and thread it through `resolve_start_layout` (instance_dir derivation) and `ResourceMetadata::from_identity` (resource.json). Verify: runner/app-suite and runner tests pass.

## 4. Hooks and sink "app" fields

- [x] 4.1 Add a `std::string_view app_name` parameter to `emplace_logger`, `emplace_metrics_sink`, and `emplace_trace_sink` (not chrono) in the `InfrastructurePolicy` concept, and pass `settings.app_name` from `Infrastructure::initialize`. Verify: `tests/app` policy fixtures compile and pass.
- [x] 4.2 Add the `"app"` field to `JsonFileMetricSink` output and an app-name field to `JsonFileMetricSinkConfig` (fixed buffer copied at construction). Verify: metric-sink tests assert the `"app"` field.
- [x] 4.3 Add the `"app"` field to `JsonFileTraceSink` span output and an app-name field to `JsonFileTraceSinkConfig` (fixed buffer copied at construction). Verify: trace-sink tests assert the `"app"` field.

## 5. Runner name in the manifest

- [x] 5.1 Add `runner_name` to `runner::SuiteManifest` (and record it in `SuiteMetadata` / `suite.json`), with an empty value meaning the default. Verify: app-suite serialization round-trips `runner_name`.
- [x] 5.2 Use `suite.runner_name` (default `"nexigen-run"`) as `FileLoggerConfig.app_name` in `runner_logging.cpp` instead of the hardcoded string. Verify: runner logging carries the configured runner name.

## 6. Runner passes --app-name via argv

- [x] 6.1 In `runner.cpp`, append `--app-name <name>` to the child argv and remove the `setenv("NEXIGEN_APP_NAME", ...)` call. Verify: process-supervision tests assert the child receives `--app-name` and no `NEXIGEN_APP_NAME`.

## 7. Tests, examples, and full verification

- [x] 7.1 Update the settings examples (`ex_app_cli_settings`, `ex_suite_app`, `ex_app_lifecycle`) and their policies for the new hook signatures and `--app-name`. Verify: examples build and run.
- [x] 7.2 Update all affected tests (`logging`, `app`, `runner`, `metrics`, `tracing`) for the removed/moved `app_name` and the new `"app"` fields, and fix any exact-string `to_string` assertions. Verify: all suites pass under Debug.
- [x] 7.3 Compile and pass 100% of unit tests under Debug, Release (`-O3 -march=native -flto`), ThreadSanitizer (`-DNEXIGEN_ENABLE_TSAN=ON`), and AddressSanitizer (`-DNEXIGEN_ENABLE_ASAN=ON`). Verify: every suite is green in all four configurations and no new warnings under `-Werror`.

## Workflow follow-up

- Archive the change after the project's review requirements are satisfied.
- Verify the archived specs landed correctly.
