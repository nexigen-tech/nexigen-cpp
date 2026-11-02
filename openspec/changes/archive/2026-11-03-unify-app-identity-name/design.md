# Design

## Context

See proposal.md — Why. Relevant current state:

- `app_name` lives in `logging::LoggerOptions` (default `"unknown_app"`) and feeds only the file logger's `"app"` JSON field via `logging::apply_to` → `FileLoggerConfig.app_name` → `FileSink::Settings.app[64]` (a fixed buffer copied once at init, referenced per line).
- Metrics and tracing have no application identity: `JsonFileMetricSink` writes `{"timestamp", "records"}`, `JsonFileTraceSink` writes one object per span with no `"app"` field.
- `app::RunIdentity` carries `app_name`, used by both the app (telemetry path resolution) and the runner (`resolve_start_layout` derives `instance_dir = run_dir/app_name/instance_id`; `ResourceMetadata` writes `resource.json`).
- The runner's own telemetry `"app"` field is hardcoded (`runner_logging.cpp` `config.app_name = "nexigen-run"`), and it passes identity to children via `setenv("NEXIGEN_APP_NAME", ...)`.

## Goals / Non-Goals

**Goals:**

- One application name (`app::Settings.app_name`), sourced from the command line, fanned out to the logger, metrics sink, and trace sink.
- A configurable runner name (`runner_name` in `suite.json`) for the runner's own telemetry.
- The runner delivers the application name to children via `--app-name` argv instead of `NEXIGEN_APP_NAME` env.

**Non-Goals:**

- Chrono output gains no `"app"` field (out of scope).
- The hub's discovery contract is unchanged: `resource.json` still carries `app_name` (the runner keeps writing it from `AppSpec.app` / the tool's `--app-name`).
- No unification of the remaining `NEXIGEN_*` identity variables — only `app_name` moves to argv.

## Decisions

### D1 — The application name lives in `app::Settings`, sourced from the CLI

`LoggerOptions.app_name` is removed. `app::Settings` gains `std::string app_name{}`; a new `-an`/`--app-name` CLI option is parsed into `CommandLineOptions.app_name` and applied into `Settings.app_name` by `apply_to`. The logging settings `app_name` JSON key and `NEXIGEN_LOG_APP_NAME` override are removed, and `logging::apply_to` no longer maps an app name into `FileLoggerConfig`.

- **Why `app::Settings`**: it is the aggregate `Infrastructure::initialize` already consumes, so the name is available exactly where the fan-out happens.

### D2 — `RunIdentity` drops `app_name`; the runner receives it separately

`RunIdentity` becomes `{run_id, run_dir, instance_id, instance_dir}`. `Runner::start` gains a `std::string_view app_name` parameter (kept alongside the `RunIdentity`), sourced from `AppSpec.app` (suite flow) or the tool's `--app-name` (single-instance flow). `resolve_start_layout` derives `instance_dir` from that parameter; `ResourceMetadata::from_identity` writes it into `resource.json`.

- **Why a separate parameter rather than keeping `app_name` in `RunIdentity`**: the runner and the child app resolve the name from different channels (suite manifest / tool CLI vs the child's own `--app-name`), so it no longer belongs in the shared identity type.

### D3 — Emplacement hooks receive the application name

`emplace_logger`, `emplace_metrics_sink`, and `emplace_trace_sink` each gain a `std::string_view app_name` parameter; `emplace_chrono_sink` does not. `Infrastructure::initialize` reads `settings.app_name` and passes it to the three hooks. Each policy sets `FileLoggerConfig.app_name` (logger) or the sink's app-name field (metrics/tracing) from it.

### D4 — `"app"` is a fixed buffer, copied once at construction

Each sink stores the application name in a fixed buffer (`char app[64]`, ≤ 63 bytes) copied once at construction and referenced per record — exactly the file logger's existing pattern. No per-record or per-batch allocation; no hot-path change.

### D5 — `runner_name` in `suite.json`

`SuiteManifest` gains an optional `runner_name` (empty = default `"nexigen-run"`). `runner_logging.cpp` uses `config.app_name = suite.runner_name` instead of the hardcoded string. `SuiteMetadata` (written `suite.json`) records it so `--suite-dir` can reproduce the runner name.

### D6 — The runner passes `--app-name` via argv

`runner.cpp` appends `--app-name <name>` to the child argv and stops calling `setenv("NEXIGEN_APP_NAME", ...)`. The remaining `NEXIGEN_*` variables are unchanged.

### D7 — Validation: one rule for the shared name

Because the name is both a folder component and a `"app"` JSON field, it SHALL be at most 63 bytes and limited to `A-Z a-z 0-9 . _ -` (no `/`, no `..`, no NUL). This tightens the previous 128-byte identity bound down to the log buffer's 63-byte limit.

## Risks / Trade-offs

- **Breaking surface**: `LoggerOptions`, `RunIdentity`, `Runner::start`, and the `InfrastructurePolicy` hook signatures all change. → source library with no external ABI guarantee; callers (tests, examples, `nexigen-run`) are updated in the same change.
- **Validation tightening (63 B)** may reject previously accepted 64–128-byte app names. → accepted trade-off; the log buffer is 64 bytes, so the tighter bound is the honest one.
- **Runner argv change** ripples through `app_suite` and `process-supervision` and their specs. → confined to the runner module; no hot-path or discovery impact.
- **No concurrency or atomic component** is introduced or altered, so the `alignas(64)` / `std::memory_order` design rules do not apply; this is a cold-path-only change with zero latency or allocation impact.

## Migration Plan

This is a source-library refactor with no runtime deployment or data migration. The change is atomic within the repo:

1. Remove `app_name` from `LoggerOptions` and its logging-settings loader; stop mapping it in `apply_to`.
2. Add `app_name` to `app::Settings`, `CommandLineOptions` (`--app-name`), and the `apply_to` CLI bridge.
3. Remove `app_name` from `RunIdentity`; thread `app_name` through `Runner::start` / `resolve_start_layout` / `ResourceMetadata`.
4. Add the `app_name` parameter to the three emplacement hooks and fan it out in `Infrastructure::initialize`; add the `"app"` field to the metrics and tracing JSON sinks.
5. Add `runner_name` to `SuiteManifest` / `SuiteMetadata` and use it in `runner_logging.cpp`.
6. Switch the runner to `--app-name` argv (drop `NEXIGEN_APP_NAME`).
7. Update tests and examples; run the full suite (Debug, Release, TSan, ASan).
