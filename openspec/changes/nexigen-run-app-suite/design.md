# Design

## Context

See `proposal.md` for motivation. The existing `nexigen::runner` contract is deliberately small and directory-owned:

- `Runner::start` creates one instance layout, launches one detached per-instance supervisor, and returns after `resource.json` records the child PID.
- The per-instance supervisor owns `waitpid`, applies the optional idle timeout, and writes `exit.json`.
- `Runner::stop` and `Runner::status` address an instance only through its directory, so any process can operate it.

`runner/instance-lifecycle` and `runner/process-supervision` define the observable behavior. The suite feature must reuse that contract rather than replace it: one application still means one instance directory and one per-instance supervisor.

The `app` module already provides the settings pipeline this feature needs: `App_settings_paths` plus `app::load_app_settings` merge defaults → global → local → environment across the four fixed files (`logging.json`, `metrics.json`, `tracing.json`, `chrono.json`) and validate the canonical lowercase log levels. The suite reuses that pipeline instead of inventing a second settings format.

Project constraints: C++23, `-fno-exceptions`, `-fno-rtti`, `core::Status`/`Result` with `has_value()`, `JSON_NOEXCEPTION=1`, Linux-only syscalls, no new external dependencies, and no work on any real-time hot path.

## Goals / Non-Goals

**Goals:**

- One declarative manifest for a set of applications, parsed and validated before anything is forked.
- A detached suite supervisor that owns policy, while each application keeps its existing per-instance supervisor.
- Per-application settings resolution (suite/app global, manifest-relative local, effective log level) using the existing `app` pipeline.
- Deterministic suite metadata under `<run_dir>/.suite/<suite_id>/`, readable by any later process.
- A separate runtime artifact tree with one directory per attempt, so restarts never overwrite previous evidence.
- Per-application environment, CPU affinity, idle timeout, and restart policy.
- Whole-suite restart for `failure` applications, bounded by a suite-wide budget.
- Final, restart-suppressing suite stop, and aggregate text/JSON status.

**Non-Goals:**

- No dependency graph, readiness probes, or start ordering beyond manifest order.
- No cgroups, namespaces, memory limits, or IO limits; affinity is the only placement control.
- No dynamic manifest reload, no hot reconfiguration, no hub/UI/network transport.
- No log rotation; every restart creates a new attempt directory and operators manage disk.
- No recovery of suite restart counters if the suite supervisor itself dies.
- No change to the single-instance layout or to `resource.json`/`exit.json` schemas.

## Decisions

### D1. Manifest schema (v1)

One flat JSON object keeps the file easy to hand-edit and to validate:

```json
{
  "version": 1,
  "suite_id": "hil",
  "run_id": "run-42",
  "run_dir": "/var/run/nexigen",
  "global_settings_path": "/etc/nexigen",
  "artifacts_path": "/var/nexigen-artifacts",
  "log_level": "info",
  "suite_restart_attempts": 2,
  "suite_restart_delay_s": 10,
  "apps": [
    {
      "app": "plant",
      "instance": "1",
      "command": "/opt/plant",
      "args": ["--bus", "can0"],
      "env": {"PLANT_MODE": "fast"},
      "cpus": [0, 1],
      "idle_timeout_s": 0,
      "stop_grace_s": 5,
      "restart_attempts": 3,
      "restart_delay_s": 2,
      "impact_on_app_suite_operability": "degrade",
      "global_settings_path": "/etc/nexigen/plant",
      "local_settings_path": "config/plant",
      "log_level": "debug"
    }
  ]
}
```

Field rules:

- `version` MUST be `1`; any other value is `InvalidArgument`.
- `suite_id`, `app`, `instance` follow the existing safe-identifier rules (1–128 chars, `A-Za-z0-9._-`, no `..`). `app` MUST NOT be `.suite` because that name is reserved for the suite metadata directory.
- `run_dir` and `artifacts_path` MUST be absolute and are required. `run_id` may be empty.
- `global_settings_path` is optional and MUST be absolute when present and non-empty. It names a directory with the four fixed files: `logging.json`, `metrics.json`, `tracing.json`, and `chrono.json`. An empty or absent value means "no global settings source".
- `log_level` is optional; when present it accepts only the canonical lowercase logging names (`verbose`, `debug`, `info`, `warning`, `error`, `fatal`, `off`). An absent level means "keep the merged settings level".
- `apps` MUST be non-empty and contain at most 256 entries.
- `suite_restart_attempts` defaults to `0`; `suite_restart_delay_s` defaults to `5`. Both are non-negative and capped (`<= 1000` attempts, `<= 86400` seconds).
- Per application, `app`, `instance`, `command`, and `impact_on_app_suite_operability` are required. `args` defaults to `[]`; `env` to `{}`; `cpus` to `[]`; `idle_timeout_s` to `0`; `stop_grace_s` to `5`; `restart_attempts` to `0`; `restart_delay_s` to `0`; `global_settings_path` to the suite value; `local_settings_path` to empty; `log_level` inherits the suite value, which may itself be absent.
- `command` is `argv[0]`; `args` is the rest of `argv`. No shell is involved.
- `cpus` is a duplicate-free list of indices in `[0, online_cpus)` and at most `CPU_SETSIZE` entries; an empty list means "no affinity restriction".
- `env` keys MUST be non-empty, contain no `=` or NUL, and are applied before `exec`.
- `global_settings_path` at application level replaces the suite value rather than layering on top of it. `local_settings_path` MUST be relative, contain no `..` segment, and is resolved against the absolute directory of the manifest file. `App_spec` keeps the manifest-relative text so serialization round-trips; the absolute directory is computed at launch.
- Settings precedence is defaults → suite global → application global (replacement) → application local → effective log level. Generated artifact sink paths override any sink named by a settings file.
- Unknown fields are ignored for forward compatibility; wrong types and wrong value domains are `InvalidArgument`.
- `impact_on_app_suite_operability` is exactly `failure`, `degrade`, or `not_impact`.

Validation is a pure function over the parsed manifest, so the CLI and the suite supervisor can both rely on it.

### D2. Directory layout and metadata

Control files and runtime artifacts are deliberately separated. Application control files keep the existing layout under `run_dir`; suite metadata goes to a reserved sibling; everything produced at runtime goes under `artifacts_path`:

```
<run_dir>/
├── .suite/
│   └── <suite_id>/
│       ├── suite.json          # resource: supervisor pid, start time, settings/artifacts paths, apps
│       ├── suite-state.json    # aggregate + per-app live state, attempt, artifact dir
│       ├── suite-exit.json     # final outcome, written once
│       └── stop.request        # empty marker; existence requests final stop
└── <app>/<instance>/
    ├── resource.json           # rewritten on every attempt
    ├── exit.json               # rewritten on every attempt
    └── settings/               # regenerated on every attempt
        ├── logging.json
        ├── metrics.json
        ├── tracing.json
        └── chrono.json

<artifacts_path>/
└── <suite_id>/<app>/<instance>/
    ├── attempt-0001/
    │   ├── stdout.log
    │   ├── stderr.log
    │   ├── app.jsonl
    │   ├── metrics.jsonl
    │   ├── traces.jsonl
    │   └── chrono.jsonl
    └── attempt-0002/...
```

`suite.json` is the suite resource record:

```json
{
  "suite_id": "hil",
  "run_id": "run-42",
  "run_dir": "/var/run/nexigen",
  "global_settings_path": "/etc/nexigen",
  "artifacts_path": "/var/nexigen-artifacts",
  "pid": 4242,
  "start_time_rfc3339": "2026-11-01T12:00:00.000000000Z",
  "start_time_mono_ns": 123456789,
  "manifest_path": "/etc/nexigen/hil-suite.json",
  "apps": [
    {"app": "plant", "instance": "1", "instance_dir": "/var/run/nexigen/plant/1"}
  ]
}
```

`suite-state.json` carries `state`, `suite_restarts_used`, and one entry per app with `state`, `restarts_used`, `attempt`, and `artifact_dir`. `suite-exit.json` carries the final `state`, a `reason` (`operator_stop`, `restart_exhausted`, or `forced_stop`), and `uptime_s`.

All three files use the existing atomic writer (`temporary file + rename`) from `instance_lifecycle.cpp`. `stop.request` is an empty file whose existence is the signal; creating it is idempotent, so concurrent stop commands are harmless.

The attempt directory is created with `mkdir -p` before the child starts. Nothing from a previous attempt is truncated or rewritten: a restart only adds `attempt-<N+1>`, while the control files at the instance root are replaced per attempt.

### D3. Process model: suite supervisor over per-instance supervisors

**Chosen:** one detached suite supervisor per suite, which starts one existing per-instance supervisor per application.

Alternatives considered:

- *One supervisor owning all application children directly.* Rejected: it would replace the existing per-instance contract, require multiplexing `waitpid` for unrelated children, and make a single crash lose every child. The per-instance supervisor already provides `setsid`, stdio capture, idle timeout, and `exit.json`.
- *No long-lived suite process; CLI orchestrates.* Rejected: restart policy must outlive the CLI, exactly as child supervision already does.

The suite supervisor:

- calls the existing `Runner::start` once per application, recording each returned instance directory;
- is the parent of every per-instance supervisor, so it reaps them with `waitpid(-1, WNOHANG)` in its poll loop;
- reads instance `exit.json`/`resource.json` and consults `/proc` for liveness;
- performs restarts by calling `Runner::start` again for the affected instance with the next attempt directory, regenerated settings, and a new `artifact_dir`;
- never touches application hot paths and never shares memory with the applications.

If the suite supervisor dies, per-instance supervisors keep running their children. `status` and `stop` still work from the suite and instance directories; only automatic restarts are lost. This is an explicit resilience boundary, not a crash-recovery mechanism.

### D4. Start handshake

`Runner::start_suite` runs in the caller:

1. Parse and validate the manifest.
2. Derive `<run_dir>/.suite/<suite_id>`.
3. If `suite.json` exists and `suite-exit.json` does not and the recorded pid is alive, return `Conflict`. A stale suite record is superseded.
4. Create the suite directory and `stop.request` handling state.
5. `fork` the suite supervisor. A close-on-exec `Start_report` pipe carries `{pid, error}` back to the caller.
6. The supervisor `setsid`s, points its own stdio at `/dev/null`, writes `suite.json`, then for every application in manifest order resolves settings, creates `attempt-0001`, launches the first attempt, writes `suite-state.json`, reports success, and enters its poll loop.
7. The caller reads the report and returns. Success means "the suite supervisor owns the suite"; per-application launch failures are recorded in `suite-state.json` and handled by policy, not by the `start` exit status.

This mirrors the existing single-instance handshake, including `_exit` in the child to avoid running the caller's destructors.

### D5. Restart policy and state machine

The supervisor keeps one preallocated `App_runtime` per manifest entry:

```cpp
struct App_runtime {
    App_spec spec;
    std::string instance_dir;
    std::string artifact_dir;
    App_state state{App_state::Unknown};
    std::uint32_t attempt{0};
    std::uint32_t restarts_used{0};
    std::uint64_t next_restart_ns{0};
    std::int32_t supervisor_pid{0};
    bool exit_consumed{false};
};
```

A single-threaded 100 ms poll loop (`suite_poll_interval_ms`) performs:

1. Reap finished per-instance supervisors with `waitpid(-1, WNOHANG)`.
2. If `stop.request` exists, run the final stop path and exit.
3. For each app that is `running` or `restarting`, check for a consumed `exit.json`, a deliberate-stop marker, and process liveness.
4. Apply due delayed retries and due whole-suite relaunches.
5. Rewrite `suite-state.json` only when the aggregate or an app state changed.

Every launch, including the first, increments `attempt`, computes `<artifacts_path>/<suite_id>/<app>/<instance>/attempt-<attempt:04>`, regenerates the four settings files with that directory as the sink root, and lets the child create its six files there. Earlier attempt directories are never modified.

Classification of a finished attempt:

- Operator stop (suite `stop.request` during the suite stop phase) → `stopped`, never restart.
- `exit_code == 0` → `exited`, never restart.
- `exit_code != 0`, non-null `signal`, or a failed `Runner::start` → abnormal.

Policy application for an abnormal result:

| Impact | Action | Budget | Exhaustion |
|---|---|---|---|
| `failure` | Stop all apps, wait `suite_restart_delay_s`, relaunch all with the next attempt directory | `suite_restart_attempts` shared by suite | Stop all, state `failed`, reason `restart_exhausted` |
| `degrade` | Retry only that app after `restart_delay_s` with the next attempt directory | per-app `restart_attempts` | App `failed`, suite `degraded` |
| `not_impact` | Retry only that app after `restart_delay_s` with the next attempt directory | per-app `restart_attempts` | App `failed`, suite state unchanged |

Counter semantics:

- `restart_attempts` is the number of retries after the initial attempt. Zero means no retry.
- `suite_restart_attempts` is the number of whole-suite relaunches. Zero means a `failure` crash immediately fails the suite.
- Per-app counters reset on every whole-suite relaunch, because it is a new suite generation; the attempt number does not reset, so artifact directories stay unique.
- A `failure` app's own `restart_attempts`/`restart_delay_s` are ignored; only the suite budget applies.
- A `not_impact` app never changes the aggregate state, whether it is retrying or permanently failed. If the suite was already `degraded` for another reason, it stays `degraded`.

Aggregate precedence is `failed` > `degraded` > `stopped` > `running`. While a per-app retry or a whole-suite relaunch is pending, the app state is `restarting`; the aggregate remains `running` until a final failure/degrade decision is made.

### D6. Stop and status

`stop` is directory-driven:

1. Resolve the suite directory from `--config` or `--suite-dir`.
2. If `suite-exit.json` exists, return success without signaling (idempotent).
3. If `suite.json` is missing, return `InvalidArgument`.
4. Create `stop.request` atomically.
5. Wait up to `max(stop_grace_s among apps) + 5 s` for `suite-exit.json`.
6. If the supervisor is unresponsive, send SIGTERM then SIGKILL to the suite supervisor, then call the existing per-instance `Runner::stop` for every recorded instance, and write the final `suite-exit.json` with reason `forced_stop`.
7. Return success only when every recorded application group is gone.

`status` is also directory-driven:

- No readable `suite.json` → aggregate `unknown`.
- `suite-exit.json` present → its final state (`stopped`/`failed`).
- Otherwise use `suite-state.json`, then recompute each app from `resource.json`, `exit.json`, and `/proc` so a dead supervisor does not leave stale `running` entries.
- Text output is one aggregate line plus one line per app with its state, attempt number, and artifact directory. `--json` emits a `Suite_status` object with the same values.

Both `--config <manifest>` and `--suite-dir <dir>` resolve to the same suite directory and must produce identical states. `--config` is a convenience for operators; the suite directory is authoritative if the manifest was edited after start.

### D7. Extending the single-instance launch contract

`Runner::start` gains an options overload, and the existing signature delegates with defaults:

```cpp
struct Start_options {
    std::string artifact_dir{};        // empty = instance_dir (legacy layout)
    std::string global_settings_dir{}; // absolute directory or empty
    std::string local_settings_dir{};  // absolute directory or empty
    std::optional<logging::LogLevel> log_level{};
    std::vector<std::pair<std::string, std::string>> env{}; // applied before exec
    std::vector<std::uint32_t> cpus{};                      // empty = unrestricted
};

core::Status start(const app::Run_identity& identity,
                   const std::vector<std::string>& command,
                   std::uint64_t timeout_s,
                   const Start_options& options);
```

- `artifact_dir` is created with `mkdir -p` before launch. When it is non-empty, `stdout.log`, `stderr.log`, and the four JSONL streams are created there and the instance directory holds only control files. When it is empty, the legacy instance-directory layout is used unchanged.
- Settings are built as `App_settings_paths` from `<global_settings_dir>/logging.json` and the other three fixed names, plus the same four names under `local_settings_dir`, and merged with the existing `app::load_app_settings` pipeline. Missing files keep defaults; a loader error fails the launch before `fork`.
- The runner serializes each merged subsystem into `<instance_dir>/settings/{logging,metrics,tracing,chrono}.json`, sets `sink.path` to `<artifact_dir>/<stream>.jsonl`, sets `log_root_directory` to `artifact_dir`, and sets `initial_log_level` to the effective level. Source sink paths are always overridden. `generate_settings` therefore takes the stream root (instance directory or attempt directory) instead of assuming the instance directory.
- The effective level is the application `log_level`, otherwise the suite `log_level`. When either is present it is exported as `NEXIGEN_LOG_LEVEL` after the manifest `env` block so it cannot be overridden accidentally; when neither is present the merged settings level is kept.
- CPU indices are validated in the parent against `sysconf(_SC_NPROCESSORS_ONLN)` and `CPU_SETSIZE` before `fork`, so an invalid request cannot create a child.
- In the child, manifest `env` entries are applied first, then the `NEXIGEN_*` identity variables, then `NEXIGEN_LOG_LEVEL`, so runner-owned values always win.
- `sched_setaffinity(0, ...)` runs before `exec`; a kernel rejection is reported through the setup pipe and surfaces as a launch failure.
- The setup pipe reports an `int32_t` status instead of one ready byte so `setsid`, `dup2`, affinity, and exec preparation failures are attributable.

The suite supervisor passes a new `artifact_dir` for every launch, so a restart never touches the files of an earlier attempt.

### D8. Public types and serialization

New header `include/nexigen/runner/app_suite.hpp`:

- `enum class Impact_on_app_suite_operability { Failure, Degrade, Not_impact };`
- `enum class Suite_state { Running, Degraded, Failed, Stopped, Unknown };`
- `enum class App_state { Running, Restarting, Exited, Stopped, Failed, Unknown };`
- `App_spec`: `app`, `instance`, `command`, `args`, `env`, `cpus`, `idle_timeout_s`, `stop_grace_s`, `restart_attempts`, `restart_delay_s`, `impact_on_app_suite_operability`, `global_settings_path`, `local_settings_path`, `log_level`.
- `Suite_manifest`: `version`, `suite_id`, `run_id`, `run_dir`, `global_settings_path`, `artifacts_path`, `log_level`, `suite_restart_attempts`, `suite_restart_delay_s`, `manifest_path`, `apps`.
- `Suite_metadata`: the suite resource record with `suite_id`, `run_id`, `run_dir`, `global_settings_path`, `artifacts_path`, supervisor `pid`, start time, `manifest_path`, and per-app instance directories.
- `Suite_app_status`: `app`, `instance`, `instance_dir`, `artifact_dir`, `attempt`, `state`, `restarts_used`.
- `Suite_status`: `state`, `suite_id`, `run_dir`, `artifacts_path`, `suite_restarts_used`, `apps`.

Every enum and structure follows the project contract: `to_string` member and free function, `std::formatter`, and non-throwing ADL `to_json`/`from_json` under `JSON_NOEXCEPTION=1`. `from_json` uses the same field-by-field, type-checked helpers as `instance_lifecycle.cpp`; a wrong type leaves the previous value unchanged.

Public functions:

```cpp
core::Status load_suite_manifest(std::string_view path, Suite_manifest& out) noexcept;
core::Status validate_suite_manifest(const Suite_manifest& manifest) noexcept;
core::Status start_suite(const Suite_manifest& manifest, std::string& suite_dir_out);
core::Status stop_suite(std::string_view suite_dir, std::optional<std::uint64_t> grace_override) noexcept;
core::Status suite_status(std::string_view suite_dir, Suite_status& out) noexcept;
std::string suite_dir_path(std::string_view run_dir, std::string_view suite_id);
```

`Runner` exposes `start_suite` as a member so the CLI has one entry point; the free functions remain available for tests.

### D9. CLI

```
nexigen-run start  --config <manifest>
nexigen-run stop   --config <manifest> [--grace <s>]
nexigen-run stop   --suite-dir <dir> [--grace <s>]
nexigen-run status --config <manifest> [--json]
nexigen-run status --suite-dir <dir> [--json]
```

Existing forms (`start ... -- <command>`, `stop <instance-dir>`, `status <instance-dir>`) are unchanged. `--config` and `--suite-dir` are mutually exclusive with each other and with a positional instance directory or a trailing command. Exit codes stay `0` success, `1` operational failure, `2` usage. `status` returns `0` for every known state; the state itself is the payload.

### D10. Concurrency and memory model

No concurrent or atomic component is introduced. The suite supervisor is single-threaded; all coordination with the CLI and with per-instance supervisors is through files, signals, and process lifetimes. Therefore `alignas(64)`, shard sizing, and `std::memory_order` choices do not apply; there are no shared atomics or locks to specify.

- All `App_runtime` storage is reserved before the poll loop, so steady-state polling performs no container growth. Building the next attempt path and re-serializing settings allocate only on a restart, which is a cold path.
- `access`/`/proc` checks and `waitpid` are the only per-tick operations. JSON is parsed only when a new `exit.json` appears and serialized only on state transitions; settings are merged only when an attempt is launched.
- Atomic rename gives readers a consistent document; cross-file reads can observe adjacent generations, which `status` resolves by preferring `suite-exit.json` and recomputing app liveness.
- A forced stop writes the final suite record only after the suite supervisor has been killed, so the last writer is the CLI.

### D11. Migration and rollback

- Additive only: existing library signatures keep working through a delegating overload; existing instance directories and files are untouched.
- A suite keeps application control files in the existing instance layout and adds a separate artifact tree under `artifacts_path`, so existing `status`/`stop` continue to work on individual applications.
- Rollback: stop any suites, then remove the new library/CLI paths. Existing instances remain operable with the previous runner; artifact directories are inert.

## Risks / Trade-offs

- **Suite supervisor is a single point for restart policy** → Per-instance supervisors still own children; `status`/`stop` work from directories; document that only automatic restarts stop if the suite supervisor dies.
- **100 ms polling bounds failure detection and restart latency** → Acceptable for HIL cold-path policy; keeping a single constant makes the trade-off explicit. `pidfd`/`inotify` were rejected as unnecessary complexity.
- **File-based coordination can interleave with a forced stop** → Atomic rename, idempotent `stop.request`, kill-then-write ordering, and ESRCH tolerance keep it convergent.
- **Every restart creates six new files per application** → Disk usage grows linearly with attempts. Log rotation is out of scope; operators own retention for `artifacts_path`. Keeping earlier attempts is deliberate, because truncating would lose failure evidence.
- **Artifacts may live on a different filesystem from control files** → The runner uses absolute paths and creates the attempt tree with `mkdir -p`; a permission or space failure is a launch failure and follows the application's impact policy.
- **A settings file may name an external sink** → Generated settings always override sink paths with the attempt directory, so artifacts stay under `artifacts_path` regardless of source configuration.
- **Stopping one suite member directly is not supported** → Use `stop --config` or `stop --suite-dir`. A direct per-instance stop looks like an abnormal exit to the suite and can trigger its restart policy; the design keeps the suite command as the only restart-suppressing stop.
- **A crashing `failure` app can cause repeated whole-suite restarts** → The shared suite budget bounds the churn; exhaustion produces a final `failed` state.
- **CPU affinity is advisory** → The child inherits the requested set, but the application's own threads may change it; the runner does not enforce it after `exec`.
- **PID reuse can briefly confuse liveness checks** → `/proc/<pid>/stat` filters zombies and `suite-exit.json` is authoritative for final states; the window is the same as the existing single-instance runner.
- **`execvp` resolves `command` through the child's `PATH`** → A manifest `env.PATH` changes resolution; this is intentional and matches shell expectations.

## Open Questions

None. Deferred items (dependency ordering, readiness probes, log rotation, cgroup limits, manifest reload) are listed as non-goals and do not change the specs or task breakdown.
