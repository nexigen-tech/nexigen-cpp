# nexigen-run application suites

`nexigen-run` can supervise a coordinated set of applications from one JSON
manifest. Each application still runs as an independent instance with its own
per-instance supervisor; a detached **suite supervisor** owns restart and
impact policy for the set.

Single-instance forms remain unchanged:

```bash
nexigen-run start -- /opt/app --flag
nexigen-run status /var/run/nexigen/app/1
nexigen-run stop /var/run/nexigen/app/1
```

## Suite manifest

```json
{
  "version": 1,
  "suite_id": "hil",
  "run_id": "run-42",
  "run_dir": "/var/run/nexigen",
  "global_settings_path": "/etc/nexigen",
  "artifacts_path": "/var/log/nexigen",
  "log_level": "info",
  "suite_restart_attempts": 2,
  "suite_restart_delay_s": 10,
  "apps": [
    {
      "app": "plant",
      "instance": "1",
      "command": "/opt/plant",
      "args": ["--bus", "can0", "--rate", "100"],
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

### Suite fields

| Field | Required | Meaning |
|---|---|---|
| `version` | yes | Must be `1`. |
| `suite_id` | yes | Safe identifier; also the suite metadata directory name. |
| `run_id` | no | Copied into each application's identity. |
| `run_dir` | yes | Absolute control-file root. Instances live at `<run_dir>/<app>/<instance>/`. |
| `global_settings_path` | no | Absolute directory with `logging.json`, `metrics.json`, `tracing.json`, `chrono.json`. |
| `artifacts_path` | yes | Absolute root for logs, metrics, traces, and timestamps. |
| `log_level` | no | Suite-wide `verbose`, `debug`, `info`, `warning`, `error`, `fatal`, or `off`. |
| `suite_restart_attempts` | no | Whole-suite relaunches for `failure` applications. Default `0`. |
| `suite_restart_delay_s` | no | Pause before a whole-suite relaunch. Default `5`. |
| `apps` | yes | 1–256 application entries. |

### Application fields

| Field | Required | Meaning |
|---|---|---|
| `app` | yes | Safe identifier; must not be `.suite`. |
| `instance` | yes | Safe identifier; `(app, instance)` must be unique. |
| `command` | yes | `argv[0]`, resolved through the child's `PATH` when relative. |
| `args` | no | Arguments passed verbatim; no shell is involved. |
| `env` | no | Extra child-only environment entries. |
| `cpus` | no | Allowed CPU indices; empty means no affinity restriction. |
| `idle_timeout_s` | no | Terminate the application after this many seconds; `0` disables. |
| `stop_grace_s` | no | SIGTERM grace before SIGKILL. Default `5`. |
| `restart_attempts` | no | Per-application retries for `degrade`/`not_impact`. Default `0`. |
| `restart_delay_s` | no | Pause before a per-application retry. Default `0`. |
| `impact_on_app_suite_operability` | yes | `failure`, `degrade`, or `not_impact`. |
| `global_settings_path` | no | Absolute directory replacing the suite-level global path. |
| `local_settings_path` | no | Relative directory resolved against the manifest directory. |
| `log_level` | no | Per-application level overriding the suite level. |

## Settings resolution

For each application the runner merges, in order:

1. built-in defaults;
2. the suite `global_settings_path`;
3. the application `global_settings_path`, which **replaces** the suite value;
4. the application `local_settings_path`, resolved against the manifest file's
   directory;
5. an explicit suite or application `log_level`.

The runner writes the four merged files into
`<run_dir>/<app>/<instance>/settings/`. Every sink path is replaced with the
current attempt artifact directory, and `log_root_directory` is that directory.
When neither the suite nor the application names a `log_level`, the merged
settings value is kept and `NEXIGEN_LOG_LEVEL` is not forced.

Identity variables (`NEXIGEN_*`) always win over manifest `env` values.

## Runtime artifacts

Every launch attempt writes a fresh directory:

```
<artifacts_path>/<suite_id>/<app>/<instance>/attempt-0001/
├── stdout.log
├── stderr.log
├── app.jsonl
├── metrics.jsonl
├── traces.jsonl
└── chrono.jsonl
```

A restart increments the attempt number and creates `attempt-0002`, and so on.
Earlier attempts are never truncated or overwritten. Control files
(`resource.json`, `exit.json`, `settings/`) stay in the instance directory and
are rewritten for the current attempt.

## Restart and impact policy

Restarts happen only on **abnormal termination**: a non-zero exit code or death
by a signal. Exit code `0` and an operator-issued `stop` are final.

| Impact | On abnormal termination | Budget | On exhaustion |
|---|---|---|---|
| `failure` | Stop the whole suite, wait `suite_restart_delay_s`, relaunch every application with new attempt directories | `suite_restart_attempts` (shared) | Suite `failed`, reason `restart_exhausted` |
| `degrade` | Retry only that application after `restart_delay_s` | Its `restart_attempts` | Application `failed`, suite `degraded` |
| `not_impact` | Retry only that application after `restart_delay_s` | Its `restart_attempts` | Application `failed`, suite state unchanged |

A `failure` application ignores its own `restart_attempts`/`restart_delay_s`:
the suite-wide budget applies. Per-application counters reset on a whole-suite
relaunch; attempt numbers keep increasing so artifact directories stay unique.

## Commands

```bash
# Start the detached suite supervisor.
nexigen-run start --config /etc/nexigen/hil.json

# Aggregate + per-application state, text or JSON.
nexigen-run status --config /etc/nexigen/hil.json
nexigen-run status --suite-dir /var/run/nexigen/.suite/hil --json

# Final stop: SIGTERM, grace, SIGKILL; no restarts.
nexigen-run stop --config /etc/nexigen/hil.json --grace 10
nexigen-run stop --suite-dir /var/run/nexigen/.suite/hil
```

`--config` resolves the suite directory from the manifest; `--suite-dir`
addresses it directly. Both report the same state. `status` returns `0` for any
known state; the state itself is the payload.

Suite metadata lives in `<run_dir>/.suite/<suite_id>/`:

- `suite.json` — suite resource record;
- `suite-state.json` — aggregate and per-application state, attempt, artifact
  directory, and restart counters;
- `suite-exit.json` — final `stopped`/`failed` outcome and reason;
- `stop.request` — empty marker used by `stop`.

## Limitations

- No dependency ordering or readiness probes; applications launch in manifest
  order.
- No dynamic manifest reload.
- No log rotation; every attempt keeps its own files, so disk usage grows with
  attempts.
- If the suite supervisor dies, per-instance supervisors keep the applications
  running and `status`/`stop` still work, but automatic restarts stop.
- Stopping an individual suite member directly is not supported; use the suite
  stop command so the supervisor does not treat the stop as a failure.
