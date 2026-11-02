# Proposal

## Why

`app_name` lives in `LoggerOptions` (a logging-only field) and feeds only the `"app"` field of log lines; metrics and tracing have no application identity at all, and the runner's own telemetry `"app"` field is hardcoded to `"nexigen-run"`. This hoists the application name into a single `app::Settings.app_name` sourced from the command line, fans it out to logging, metrics, and tracing, and gives the runner its own configurable name.

## What Changes

- Move `app_name` out of `LoggerOptions` and into `app::Settings`. **BREAKING** — logging settings no longer carry `app_name` (its JSON key and `NEXIGEN_LOG_APP_NAME` override are removed).
- Source `app_name` from a new `--app-name` CLI option (parsed into `CommandLineOptions`, applied into `app::Settings`). **BREAKING** — new option.
- Unify with `RunIdentity.app_name`: remove that field; `Runner::start` receives `app_name` as a separate parameter so the runner can still derive `instance_dir` and write `resource.json` metadata. **BREAKING** — `RunIdentity` and `Runner::start` signatures change.
- Add `app_name` to the `emplace_logger` / `emplace_metrics_sink` / `emplace_trace_sink` hook signatures so each policy can inject it into its sink. **BREAKING** — `InfrastructurePolicy` contract changes.
- Emit an `"app"` field in metric batch JSON and trace span JSON output (new observable behavior; chrono output is out of scope).
- The runner passes `--app-name` into the child argv and stops setting `NEXIGEN_APP_NAME`. **BREAKING** — the runner→app channel moves from env to argv.
- Add `runner_name` to `suite.json` and use it as the runner's own telemetry `"app"` field, replacing the hardcoded `"nexigen-run"`.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `logging/logger-options`: `LoggerOptions` drops the `app_name` field.
- `logging/logging-settings`: the local-only `app_name` setting is removed and `apply_to` no longer maps it into `FileLoggerConfig`.
- `app/settings`: `Settings` gains an `app_name` member.
- `app/command-line`: a new `--app-name` option is parsed and applied.
- `app/telemetry-layout`: `RunIdentity` drops `app_name`; `instance_dir` derivation uses the separately supplied `app_name`.
- `app/application`: `Infrastructure::initialize` and the policy hooks carry `app_name`.
- `metrics/metric-sinks`: metric JSONL batches carry an `"app"` field.
- `tracing/trace-sinks`: trace JSONL span lines carry an `"app"` field.
- `runner/app-suite`: `suite.json` gains a `runner_name` field.
- `runner/logging`: the runner's own `"app"` field is taken from `runner_name` (default `"nexigen-run"`).
- `runner/process-supervision`: the child is launched with `--app-name` in argv instead of `NEXIGEN_APP_NAME` in the environment.

## Impact

- **Code**: logging (`logger_options.hpp`, `logging_settings.cpp`, `logger_options_format.cpp`), app (`app_settings`, `command_line`, `telemetry_layout`, `infrastructure`), metrics/tracing file sinks (+ their settings), runner (`app_suite`, `runner_logging`, `runner.cpp`, `instance_lifecycle`), plus tests and examples.
- **Performance / latency / allocation**: none on hot paths. The `"app"` string is a fixed buffer copied once at sink initialization and referenced per record (the same copy-once pattern the file logger already uses); no per-record allocation or latency change.
- **Dependencies**: none new.
