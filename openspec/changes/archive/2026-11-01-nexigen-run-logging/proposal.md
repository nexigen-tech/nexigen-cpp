# Proposal

## Why

`nexigen-run` currently reports its own diagnostics only through stderr, exit codes, and the suite state JSON files. Once the CLI exits, the detached supervisor's decisions — application exits, retries, whole-suite restarts, policy failures, forced stops — leave no durable, timestamped trace. Operators need a log next to the runtime artifacts to reconstruct what the runner did.

## What Changes

- Add a runner-owned `nexigen::logging::FileLogger` instance (a small, cold-path instantiation) in both `nexigen-run` CLI commands and the detached suite supervisor.
- Write runner logs to `<artifacts_path>/<suite_id>/runner.jsonl` for a valid suite manifest, with scheduled `RotationConfig::None`.
- Fall back to `<cwd>/.nexigen-run/runner.jsonl` when the manifest is absent or invalid. For `--suite-dir`, derive the artifact path from `suite.json`; the level then comes from the recorded global settings, otherwise `info`.
- Select runner logging settings from two layers:
  - suite-level `log_level` overrides `initial_log_level` from `<global_settings_path>/logging.json`, which overrides `info`;
  - `runner_logger_cpus` overrides `flush_cpu_affinity` from that file for the logger worker;
  - the file also supplies `max_file_size_bytes` and throttle settings when present. Path, file name, and scheduled rotation stay runner-specific.
- Add suite-manifest fields `runner_control_cpus` and `runner_logger_cpus`. Pin the CLI/suite-supervisor control thread and the logger worker respectively; leave application processes and per-instance supervisors unconstrained, and reset the child to all online CPUs when an application has no explicit `cpus`.
- Validate affinity softly: malformed entries or unavailable cores produce a warning, disable that pinning, and never fail `start`, `stop`, or `status`.
- Log CLI command outcomes (`start`, `stop`, `status`) and suite supervisor lifecycle events (suite start/stop/fail, application launch/exit, retry scheduling, suite restart, forced stop).
- Make logging lifecycle fork-safe: flush and join the logger worker before every `fork` performed after initialization, then reinitialize in the parent; flush before `_exit`.
- Keep logging best-effort: initialization or write failure MUST NOT change a command's exit code or supervision policy; the runner falls back to the other log location, then to stderr.
- No change to application logging, generated settings, suite layout, or the `nexigen-run` CLI syntax.

## Capabilities

### New Capabilities

- `runner/logging`: how `nexigen-run` records its own CLI and supervisor diagnostics, including level/path selection, fallback behavior, fork-safe logger lifecycle, and the events written to `runner.jsonl`.

### Modified Capabilities

None. Application logging and suite policy remain defined by the existing `logging/*` and `runner/*` capabilities.

## Impact

- **Code**: new `include/nexigen/runner/runner_logging.hpp` and `src/runner/runner_logging.cpp`; integration in `tools/nexigen-run/main.cpp`, `src/runner/app_suite.cpp`, and the child-affinity reset in `src/runner/runner.cpp`; tests in `tests/runner/`.
- **APIs**: additive `nexigen::runner` logging helpers and two additive suite-manifest fields; the existing `Runner` and CLI contracts are unchanged.
- **Dependencies**: uses the existing `nexigen::logging::FileLogger` and logging-settings loader; no new external dependency.
- **Performance**: cold path only. Each CLI process and the suite supervisor may start one background logger thread; producer calls are on control paths only, never on an application tick, producer, snapshot, or sink path. The wrapper uses a bounded `FileLogger<64, 256, 4>` instantiation, so memory is a fixed few tens of KiB per runner process. Affinity is applied once per process; the application child resets to all online CPUs unless it has explicit `cpus`. No application latency is affected.
- **Storage**: one append-only `runner.jsonl` per suite (or per working directory fallback). Scheduled rotation is disabled; size segmentation and throttling follow the global logging settings when supplied. Operators own retention.
