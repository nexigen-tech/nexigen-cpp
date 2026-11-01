# Proposal

## Why

`nexigen-run` launches exactly one application per invocation. A HIL complex needs a coordinated set of applications, each with its own command-line arguments, environment, CPU affinity, and failure policy, started, stopped, and inspected as one unit. Today operators must script many independent `start` calls, and nothing decides whether one application's crash should restart only that application, mark the complex degraded, or restart the whole suite.

## What Changes

- Add a JSON **suite manifest** and `nexigen-run start|stop|status --config <file>`:
  - per application: `app`, `instance`, `command`, `args`, `env`, allowed `cpus`, `idle_timeout_s`, `stop_grace_s`, `restart_attempts`, `restart_delay_s`, `impact_on_app_suite_operability`, `global_settings_path`, `local_settings_path`, and `log_level`;
  - per suite: `suite_id`, `run_id`, `run_dir`, `global_settings_path`, `artifacts_path`, `log_level`, `suite_restart_attempts`, `suite_restart_delay_s`.
- Add a detached **suite supervisor** that starts one existing per-instance supervisor per application, watches every instance, applies restart policy, and records suite metadata under `<run_dir>/.suite/<suite_id>/` (`suite.json`, `suite-state.json`, `suite-exit.json`).
- Implement the three impact levels:
  - `failure`: any abnormal termination of that application stops the suite and restarts all applications, consuming the suite-wide restart budget;
  - `degrade`: only the failed application is retried with its own budget; if it stays down the suite continues as `degraded`;
  - `not_impact`: only the failed application is retried; the suite state never changes and the application is reported failed.
- Resolve settings per application: the suite `global_settings_path` is the base, an application-level `global_settings_path` replaces it, `local_settings_path` is resolved relative to the manifest directory, and, when either specifies `log_level`, the application value or the suite value wins over the settings files. The runner merges these sources and writes the resulting four settings files with artifact sink paths.
- Separate runtime artifacts from control files: `artifacts_path` holds logs, metrics, traces, and timestamps under `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-<NNNN>/`, while `resource.json`, `exit.json`, and generated settings stay in the instance directory.
- Restart only on abnormal termination (non-zero exit code or death by signal). Normal exit code 0 and operator-issued `stop` are final and never restart.
- Extend launch options: per-application `env` and `cpus` are applied in the child before `exec`; every launch attempt writes to a new artifact directory with new `stdout.log`, `stderr.log`, and JSONL streams, leaving earlier attempts untouched.
- Add suite-level `stop` (no restarts, final state `stopped`) and suite-level `status` with per-application plus aggregate state, in text and `--json` form.
- No **BREAKING** changes: single-instance `start`, `stop`, and `status` keep their current behavior and layout.

## Capabilities

### New Capabilities

- `runner/app-suite`: manifest schema and validation, settings resolution, per-attempt artifact layout, detached suite supervision, impact levels, per-application and whole-suite restart budgets, aggregate `stop`/`status`, and suite metadata files.

### Modified Capabilities

- `runner/instance-lifecycle`: the deterministic layout keeps control files in the instance directory while allowing an attempt artifact root for runtime streams, and generated settings point at that stream root.
- `runner/process-supervision`: the launch contract gains configurable child environment and CPU affinity, and per-attempt stdout/stderr stream directories for restarted attempts.

## Impact

- **Code**: new `include/nexigen/runner/app_suite.hpp` and `src/runner/app_suite.cpp`; `Runner` gains a suite entry point and an options-based start overload in `include/nexigen/runner/runner.hpp` / `src/runner/runner.cpp`; `generate_settings` gains a stream-root parameter in `src/runner/instance_lifecycle.cpp`; `tools/nexigen-run/main.cpp` gains `--config` operations; new tests in `tests/runner/`.
- **APIs**: additive `nexigen::runner` types and functions; existing single-instance API stays source-compatible.
- **Dependencies**: none new. Uses `nlohmann::json` (already a dependency), the existing `app::load_app_settings` merge pipeline, and Linux `sched_setaffinity` / `execvp`.
- **Performance**: cold path only. Nothing runs in a tick, producer, snapshot, or sink callback. The suite supervisor polls at 100 ms and performs JSON I/O only on state transitions, so there is no effect on supervised-application latency. Memory is O(number of applications), with all buffers allocated during suite startup; each attempt creates one new artifact directory, and previous attempts are never rewritten.
- **Storage**: runtime artifacts move to `artifacts_path` while control files stay under `run_dir`; every restart consumes a new set of six files per application, so disk usage grows with attempts.
- **Operational**: adds one detached suite supervisor process per suite and one existing supervisor per application; `stop` remains directory-driven and idempotent.
