# Design

## Context

See `proposal.md` for motivation and the two delta specs for the behavioral contract. The repository already has `examples/app/ex_app_lifecycle.cpp` (console sinks, default `App_settings`, no settings files), the command-line parser in `nexigen::app`, the `Infrastructure::initialize(options, loaded)` overload, and file-backed sinks for logging, metrics, tracing, and chrono. `examples/CMakeLists.txt` provides `nexigen_add_example`, which builds an executable, links it to a module target, and registers a CTest smoke test.

The example must be runnable directly from the repository root and must leave inspectable artifacts. Relative paths therefore resolve against the current working directory, and the helper scripts pin that directory to the repository root.

## Goals / Non-Goals

**Goals:**

- A self-contained example that prepares its own local settings and output directories, uses the real command-line parser, initializes through the existing command-line overload, and writes all four artifact kinds to files.
- Deterministic, bounded execution suitable for CTest and manual inspection.
- Simple runner and cleanup scripts with no dependency on a test harness.

**Non-Goals:**

- No library API, settings-schema, or sink changes.
- No new configuration discovery, daemon lifecycle, or signal-driven production flow.
- No change to `ex_app_lifecycle`; the new example is additive.
- No attempt to make the example a general-purpose launcher for arbitrary settings trees.

## Decisions

### D1. File layout and fixed names

Add:

- `examples/app/ex_app_cli_settings.cpp`
- `examples/app/run_ex_app_cli_settings.sh`
- `examples/app/clean_ex_app_cli_settings.sh`
- one `nexigen_add_example(ex_app_cli_settings ex_app_cli_settings.cpp nexigen::app)` line in `examples/app/CMakeLists.txt`
- root `.gitignore` entries for the two generated directories, so manual runs do not dirty `git status`

Fixed paths, relative to the current working directory:

| Path | Purpose |
|---|---|
| `./nexigen-example-settings/` | generated local settings |
| `./nexigen-example-settings/logging.json` | logging settings |
| `./nexigen-example-settings/metrics.json` | metrics settings |
| `./nexigen-example-settings/tracing.json` | tracing settings |
| `./nexigen-example-settings/chrono.json` | chrono settings |
| `./nexigen-example-output/` | generated artifacts |
| `./nexigen-example-output/app.jsonl` | log records |
| `./nexigen-example-output/metrics.jsonl` | metric batches |
| `./nexigen-example-output/traces.jsonl` | span records |
| `./nexigen-example-output/chrono.jsonl` | chrono snapshots |

Rationale: one output directory matches the request, keeps cleanup to two paths, and makes the artifacts easy to inspect. Alternative considered: one directory per subsystem (`logs/`, `metrics/`, `traces/`, `chrono/`). Rejected because the request describes a single directory and one cleanup target.

### D2. Preparation order and cleanup safety

`main` performs setup before touching the command line:

1. `std::filesystem::remove_all(kOutputDir, ec)` to clear stale artifacts, then `create_directories(kOutputDir, ec)`.
2. `create_directories(kLocalSettingsDir, ec)`.
3. Write the four settings files with `std::ofstream` (truncating).
4. Parse the command line and apply the options.

All filesystem calls use the `std::error_code` overloads so the example remains valid under `-fno-exceptions`. A failure at any step prints the failing path and returns non-zero. `remove_all` removes a symlink itself rather than following it; the fixed distinctive directory names keep accidental deletion risk low.

Alternative considered: let the shell scripts clean directories before launching. Rejected because the spec requires the program to clear an existing output directory itself, so direct execution behaves the same as the scripted run.

### D3. Generated settings contents

The generated files stay minimal and valid for the existing loaders:

- `logging.json`: `log_root_directory` = `./nexigen-example-output`, `app_name` = `nexigen-example`, `initial_log_level` = `info`, `spsc_ring_buffer_capacity` = 128, `message_capacity` = 128, `max_producer_threads` = 32. The capacities fit `FileLogger<>` defaults so the logger's explicit capacity check passes.
- `metrics.json`: `collect_interval_ns` = 0; the example calls `collect_now()` explicitly.
- `tracing.json`: `max_tracers` = 4.
- `chrono.json`: `collect_interval_ns` = 0; the example calls `snapshot_now()` explicitly.

Zero collection intervals keep the example deterministic and avoid extra background workers under sanitizer runs. The logging settings still drive the file logger's base directory through the existing `logging::apply_to` mapping; the other three sink paths are fixed constants in the policy because those settings structures carry provider options, not sink paths.

### D4. Command-line composition

The example preserves real `argv` handling and remains runnable without arguments:

1. Copy `argv[1..]` into a `std::vector<std::string_view>`.
2. Scan for any token that starts with `-gs`, `--global-settings`, `-ls`, or `--local-settings` (including `=` and attached-value forms). If neither directory option is present, append `--local-settings` and `./nexigen-example-settings`.
3. Parse with `parse_command_line(args)`.
4. On a parse error, print every diagnostic plus the usage text to stderr and return `2`; on `--help`, print usage to stdout and return `0`.
5. Load and initialize with `infra->initialize(options, loaded)`.
6. On a loader or initialization failure, print the diagnostics from `loaded` and return `1`.

The global directory stays empty by default. A caller-supplied `--global-settings` is honored as an explicit override; the generated local settings directory then remains unused. Alternative considered: synthesize a fixed argument list and ignore `argv`. Rejected because the example must demonstrate the actual command-line API.

### D5. Workload and bounded runtime

After initialization:

- Create a counter and a gauge through `meter("cli-example", "1.0")`.
- Create a tracer through `tracer_for("cli-example.loop")` and open one span around the loop.
- Run a tick loop with a `std::chrono::steady_clock` deadline of 2 seconds and a hard tick cap:
  - increment the counter and set the gauge each tick;
  - log an info record every 100 ticks;
  - call `metrics().collect_now()` and `chrono().snapshot_now()` every 100 ticks;
  - request `Infrastructure_stop_reason::Requested` when the deadline or cap is reached.
- End the span, then call `shutdown()` and verify its status.
- Verify that all four artifact files exist and are non-empty; return non-zero otherwise.

The 2-second internal deadline is far below the one-minute requirement. The runner adds a 60-second `timeout` as an outer guard. Alternative considered: run until SIGINT. Rejected because CTest and scripted runs must terminate on their own.

### D6. Stateless file policy

`Cli_file_policy` mirrors the existing example's policy shape and holds no runtime state:

- `Logger = logging::FileLogger<>`; `emplace_logger` runs `logging::check_capacity`, maps the merged settings with `logging::apply_to`, pins `rotation.kind` to `Rotation_schedule::None` (so `app.jsonl` stays at the output root instead of the config default's hourly subdirectory), then calls `out->initialize(config)`.
- `Metrics_sink = metrics::JsonFileMetricSink`; constructed with `{kMetricsPath, false}`.
- `Trace_sink = tracing::JsonFileTraceSink`; constructed with `{kTracesPath, RotationPolicy::None, 0, 0}`.
- `Chrono_sink = chrono::JsonFileChronoSink`; constructed with `{kChronoPath, false}`.

All sink paths are compile-time constants. The sinks are constructed during `Infrastructure::initialize` and outlive their providers through the existing member ordering. `append=false` is safe because the output directory was cleared first and prevents stale batches from a previous process.

Alternative considered: reuse console sinks and redirect stdout. Rejected because the request explicitly asks for files per artifact kind and file sinks already exist.

### D7. Runner and cleanup scripts

Both scripts resolve the repository root from their own location (`dirname "${BASH_SOURCE[0]}"/../..`), so they work from any current directory.

`run_ex_app_cli_settings.sh`:

- uses `BUILD_DIR` when set, otherwise `<repo>/build`;
- looks for `examples/app/ex_app_cli_settings` under the build root, then under `debug/` and `release/`;
- builds the target with `cmake --build "$BUILD_DIR" --target ex_app_cli_settings` when the binary is absent;
- `cd`s to the repository root so the generated directories land there;
- runs the binary through `timeout 60` when `timeout` is available, otherwise directly (the example still self-bounds);
- forwards extra arguments and returns the example's exit status.

`clean_ex_app_cli_settings.sh` removes exactly `<repo>/nexigen-example-settings` and `<repo>/nexigen-example-output` with `rm -rf`, which is idempotent when they are absent. Both scripts use `set -euo pipefail` and are committed executable.

Alternative considered: a CMake custom target for running/cleanup. Rejected because the request asks for bash scripts and they are easier to inspect and run manually.

### D8. Verification strategy

- `nexigen_add_example` registers the CTest smoke test; the example itself verifies that the four artifact files are non-empty.
- The run script provides a manual end-to-end check from the repository root.
- The cleanup script provides the reset step between manual runs.
- The example runs in the build tree under CTest, so its generated directories are covered by the existing `build/` ignore rule; the cleanup script targets the repository-root copies produced by the runner.

### D9. Memory layout and concurrency

This change adds no atomic or concurrent component. `Cli_file_policy` is stateless and has no fields, so there is no `alignas(64)` layout, per-thread shard sizing, or `std::memory_order` choice to specify. All concurrency remains inside the existing `FileLogger`, `MetricProvider`, `TracerProvider`, and `ChronoProvider`; the example only calls their public cold-path control operations (`collect_now`, `snapshot_now`, `flush`, `shutdown`) from `main`. No library allocation or locking is added to any hot path.

## Risks / Trade-offs

- **Working-directory dependence** → The generated paths are relative by design. The scripts pin the repository root, the README documents the behavior, and the fixed distinctive names make the cleanup target unambiguous.
- **Deleting an existing output directory** → The program intentionally clears `./nexigen-example-output`; a user running it from an unexpected directory could lose files in a directory with that exact name. The name is example-specific, the behavior is documented, and the cleanup script touches only the same two paths.
- **FileLogger capacity mismatch** → The generated `logging.json` pins capacities within `FileLogger<>` defaults; a user-supplied local settings file with larger capacities fails initialization and is reported as an error rather than silently ignored.
- **`timeout` availability** → The runner checks for `timeout` and falls back to direct execution; the example's internal 2-second deadline still bounds the run.
- **CTest artifacts in the build tree** → The generated directories appear under the CTest working directory and are ignored by the existing `build/` rule; the cleanup script is documented for repository-root runs.

## Migration Plan

Additive only. No migration or rollback steps are required; removing the example source, scripts, CMake line, README entry, and spec delta would restore the previous state.

## Open Questions

None.
