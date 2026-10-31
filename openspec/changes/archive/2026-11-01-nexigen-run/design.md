# Design

## Context

`app/telemetry-layout` defined the *identity* and *path resolution* contract: an application can discover where to write logs, metrics, traces, and chrono snapshots, but nothing creates those paths, generates absolute-pathed settings, or launches the application under a supervision contract.

The nexigen-run change adds a `nexigen::runner` library and a `nexigen-run` binary that does exactly that:

- Creates the instance directory tree
- Writes `resource.json` (start-time metadata)
- Generates four settings files with absolute sink paths
- Launches a child process under a new session/process group
- Captures stdout and stderr to files
- Stops the child on demand (SIGTERM → SIGKILL)
- Writes `exit.json` on termination
- Reports `status`

No hot-path, no new external dependency, no network transport.

## Goals / Non-Goals

**Goals:**

- Implementation of a deterministic instance lifecycle per the `runner/instance-lifecycle` spec
- Implementation of process supervision per the `runner/process-supervision` spec
- A CLI binary that exposes `start`, `stop`, `status` sub-commands
- Generated settings that put `sink.path` onto absolute paths inside the instance directory
- Build and test infra for the new module (`build/debug/...`, `build/release/...`, `build/tsan/...`, `build/asan/...`)

**Non-Goals:**

- The nexigen-hub observer, its TUI/Web UI, or HTTP API (separate follow-up)
- Inotify-based instance discovery
- Retention, rollup, or Parquet conversion of the JSONL streams
- Network-based health checks or multi-host orchestration
- Windows/macOS cross-platform (Linux-only)

## Decisions

### D1. Module structure: `nexigen::runner` library + `nexigen-run` binary

Library code:

| Path | Role |
|---|---|
| `include/nexigen/runner/instance_lifecycle.hpp` | Resource_metadata / Exit_metadata structs, factory helpers, JSON serialisation trio |
| `include/nexigen/runner/runner.hpp` | `Runner` class: run/stop/status orchestration, dir creation, settings generation, process supervision |
| `src/runner/instance_lifecycle.cpp` | Implementation: metadata population, JSON I/O |
| `src/runner/runner.cpp` | Implementation: `Runner::start`, `Runner::stop`, `Runner::status` |

Binary:

| Path | Role |
|---|---|
| `tools/nexigen-run/main.cpp` | Thin CLI around `Runner` |

Tests:

| Path | Role |
|---|---|
| `tests/runner/runner_test.cpp` | Unit tests (fork-based, but local-scope) |

Rationale: the library may be used directly by managed applications that embed the lifecycle contract without the CLI wrapper.

### D2. `Runner::start` creates the directory first

Sequence:

1. Resolve instance dir from identity or CLI args (the CLI calls `app::load_run_identity()` and merges command-line overrides)
2. Create `<instance_dir>/settings/` (mkdir -p)
3. Generate and write `settings/logging.json`, `settings/metrics.json`, `settings/tracing.json`, `settings/chrono.json` with absolute paths
4. Truncate `<instance_dir>/stdout.log` and `<instance_dir>/stderr.log`
5. `fork` a detached supervisor, which `setsid`s, forks the application child, and waits for the child's setup handshake (`setsid` + `dup2` complete)
6. Write `resource.json` with the child PID and supervisor PID
7. Report the child PID back to `start` through a pipe; `start` returns control to the caller (child is async)
8. Register the optional idle-timeout timer in the supervisor

All I/O happens before the supervisor starts waiting; the application child only performs `setsid`, `dup2`, `setenv`, and `exec`.

### D2b. Detached supervisor

`start` must return while the application keeps running, and the application must be reaped so `exit.json` can be written. A direct child of `start` would be re-parented when `start` exits, so the runner interposes one supervisor process:

- The supervisor `setsid`s away from the CLI terminal and redirects its own stdio to `/dev/null`.
- It forks the application child, which calls `setsid` again to become a session/process-group leader.
- The child reports readiness through a pipe; only then is `resource.json` written. This removes the race where `stop` could signal a process group that does not exist yet.
- The supervisor owns `waitpid`, applies the idle timeout, and writes `exit.json` exactly once.
- `stop`/`status` remain stateless: they read `resource.json`, probe `/proc`, and read `exit.json`.

The supervisor is cold-path infrastructure for one instance; it adds no work to the supervised application.

### D3. Settings generation reuses `app/telemetry-layout` resolution

The runner calls `app::resolve_telemetry_paths` with the present identity. A resolved path is written verbatim only when it is absolute and lies inside the instance directory; otherwise the deterministic `<instance_dir>/<stream>.jsonl` path is written, because the outside path cannot be trusted to belong to this instance. For logging, `log_root_directory` is always the instance directory and `sink.path` is `<instance_dir>/app.jsonl`.

```cpp
const auto identity = nexigen::app::load_run_identity();
nexigen::app::Telemetry_paths resolved{};
resolve_telemetry_paths(identity, "", "", "", resolved);
// resolved.metrics = "<instance_dir>/metrics.jsonl" etc.
```

A stream without a resolved path gets the deterministic `<instance_dir>/<stream>.jsonl` path; no `sink.path` is left empty, so every stream has an absolute target inside the instance directory.

### D4. resource.json and exit.json are plain structs with the serialisation trio

Two new structs: `Resource_metadata` and `Exit_metadata`.

`Resource_metadata` fields:
- `run_id`, `app_name`, `instance_id`, `instance_dir` — mirrored from `Run_identity`
- `pid` — child PID after fork
- `start_time_rfc3339` — ISO-8601 string at mkdir time
- `start_time_mono_ns` — CLOCK_MONOTONIC_RAW at mkdir time
- `version` — optional string, set to the fixed tag `"nexigen-cpp"`; no git metadata is embedded, so builds stay reproducible

`Exit_metadata` fields:
- `exit_code` — `std::optional<std::int32_t>` (null when signalled)
- `signal` — `std::optional<std::int32_t>` (null when normal exit)
- `uptime_s` — wall clock seconds as a `std::uint64_t`
- `terminated_by` — `Terminated_by` enum (`self`, `runner`, `unknown`)

Both implement `to_string`, free `to_string`, `std::formatter`, ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`.

### D5. `Runner::stop` uses SIGTERM → grace → SIGKILL

```
Runner::stop(instance_dir, grace = 5s):
  if exit.json exists: return success          // idempotent
  pid = read child pid from resource.json
  kill(-pid, SIGTERM)                          // process group
  wait up to grace seconds for exit.json + group death
  if still alive or no exit.json: kill(-pid, SIGKILL)
  wait up to 5 s for the supervisor's exit.json
  if still missing: write a fallback exit.json from resource.json
  if the stop sent a signal: patch terminated_by to "runner"
  return success
```

`stop` is idempotent: a second call sees `exit.json`, sends no signal, and returns success only if the process group is already gone (otherwise it reports `SystemError`). The supervisor writes the normal `exit.json`; `stop` only patches the attribution so a graceful SIGTERM (where the child exits with a code rather than dying from the signal) still reports `"terminated_by": "runner"` and `status` reports `stopped`.

### D6. `Runner::status` checks /proc

```
Runner::status(instance_dir):
  no resource.json                → "unknown"
  child PID alive (/proc, not Z)  → "running"
  exit.json terminated_by=runner  → "stopped"
  otherwise                       → "exited"
```

No global state map; every status read is derived from the instance directory on disk. This makes the runner stateless across restarts. A PID that has been reaped is treated as dead even while its zombie is still visible, so a stale group leader cannot be reported as running.

### D7. Cold-path and concurrency impact

Everything in `nexigen::runner` is cold-path: `fork`, `exec`, `setsid`, `mkdir`, file I/O, JSON generation. The runner process is separate from the managed process. No lock, no atomic, no allocation is added to a producer/tick path in the managed application.

### D8. CLI interface

```
nexigen-run start [--run-id <id>] [--app <name>] [--instance <id>]
                   [--run-dir <path>] [--instance-dir <path>]
                   [--timeout <secs>] [-- <command>...]

nexigen-run stop <instance-dir> [--grace <secs>]

nexigen-run status <instance-dir>
```

Environment variables `NEXIGEN_*` serve as defaults for the start command.

## Risks / Trade-offs

- **Separate binary doubles process count** → Only the runner is a new process; the managed application sees no overhead. The runner is idle-waitpid most of the time.
- **exit.json race with SIGKILL** → SIGKILL cannot be caught; the runner writes exit.json after it confirms the child is reaped. If the runner itself is killed before writing, the next `status` sees `resource.json + no child` and reports `"terminated_by": "unknown"`.
- **Timeout polling uses a blocking thread** → The runner is a single-control-thread tool; it blocks on `sigsleep` or {poll + waitpid} during the grace period. Fine for a supervisor.
- **Settings format drift from `app/settings`** → Generated settings intentionally use the same JSON keys as `load_app_settings` would read. A mismatch between the runner's generation and the loader's parser will be caught by the integration test.

## Follow-ups

- `nexigen-hub` remains a separate change: inotify discovery, JSONL tailing, run/app/instance tree, and live UI/API.
- The runner's `stop` + `status` may become a REST API in the hub change; the binary subcommands remain as the local interface.