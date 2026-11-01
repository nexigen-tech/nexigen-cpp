# Design

## Context

`nexigen-run` already provides the full suite-supervisor surface the demo needs:
manifest parsing/validation (`SuiteManifest`), impact policy
(`failure`/`degrade`/`not_impact`), per-attempt artifact directories, and a
`runner.jsonl` lifecycle log (see `docs/runner-suite.md` and specs
`runner/app-suite`, `runner/process-supervision`, `runner/instance-lifecycle`).
The example app side is already covered by `ex_app_cli_settings`, which shows
the exact `Infrastructure` + `initialize(options, loaded)` flow this demo
reuses. The gap is only the wiring: one configurable app binary plus a manifest
that drives three distinct instances through the three impact levels. No
library code or public API changes.

The one non-obvious constraint is settings discovery: the runner merges the
manifest's global/local settings and writes them to
`<run_dir>/<app>/<instance>/settings/`, then launches the child with only the
manifest's `args` and `NEXIGEN_*` env — it does not inject a settings argument.
The child must therefore be given `--global-settings <instance_dir>/settings`
explicitly in the manifest, or its sinks resolve to the instance directory
instead of the attempt directory.

## Goals / Non-Goals

**Goals:**

- One configurable `ex_suite_app` binary whose role and timing come from env,
  reused for all three instances, so app identity/name and directories are
  produced entirely by the runner.
- A manifest that drives the three impact levels and makes the merge precedence
  (suite global vs per-app local) observable.
- A manual, minutes-long scenario an operator can watch through
  `nexigen-run status --json`, `runner.jsonl`, `attempt-*`, and `exit.json`.

**Non-Goals:**

- No library changes; no new public enums/structs/classes, so no new
  serialization or `std::formatter` surface (the example reuses existing types).
- No new CTest that runs the full minutes-long scenario; only `ex_suite_app`'s
  bounded worker-mode smoke run is registered under CTest.
- No automated pass/fail assertion of the whole timeline; observation is the
  human operator's, with `runner.jsonl` as the record.

## Decisions

### D1 — One binary, role and timing via environment (not CLI tokens)

The app selects its role and intervals from `NEXIGEN_DEMO_ROLE`,
`NEXIGEN_DEMO_EXIT_AFTER_S` (degrade), `NEXIGEN_DEMO_ABORT_AFTER_S` (failure),
`NEXIGEN_DEMO_LOG_PERIOD_MS`, and `NEXIGEN_DEMO_RUN_FOREVER` (worker). The only
CLI argument is `--global-settings <instance_dir>/settings`.

- *Rationale:* the app's `parse_command_line` rejects unknown options and
  positional arguments, so adding `--exit-after` etc. would require the
  token-stripping dance `ex_app_cli_settings` already does for `--fail-demo`.
  Env is read once at startup (cold path) and also demonstrates the manifest
  `env` field.
- *Alternative considered:* strip demo tokens before `parse_command_line`
  (existing pattern). Rejected as more moving parts for no benefit here.

### D2 — Settings via `--global-settings <instance_dir>/settings`

Each manifest app's `args` carries `--global-settings <run_dir>/<app>/<instance>/settings`,
a deterministic absolute path, so the child loads the runner's merged settings
(with attempt-dir sink paths and the correct `app_name`).

- *Alternative considered:* point at the original global/local sources. Rejected:
  the child would merge them itself and its sinks would fall back to the
  instance directory, breaking the attempt-directory contract.

### D3 — Three-role manifest with concrete timings

`suite.json` launches `worker` (`not_impact`), `degrade` (`degrade`), and
`failure` (`failure`). Concrete values (recorded here, kept out of the spec):

- degrade: `NEXIGEN_DEMO_EXIT_AFTER_S=60`, `restart_attempts=3`,
  `restart_delay_s=2` → three observed restarts, then suite `degraded`.
- failure: `NEXIGEN_DEMO_ABORT_AFTER_S=300`, `suite_restart_attempts=1`,
  `suite_restart_delay_s=5` → one whole-suite relaunch, then suite `failed`
  (`restart_exhausted`) at the next failure crash.
- worker: `NEXIGEN_DEMO_RUN_FOREVER=1`, `idle_timeout_s=0`.

The manifest sets `global_settings_path` to a committed `settings/` directory
(with `metrics.json`/`chrono.json` `collect_interval_ns = 1000000000`) and a
per-app `local_settings_path` on one instance (e.g. `config/degrade`) to make
the merge precedence visible.

### D4 — App structure mirrors `ex_app_cli_settings`

A stateless `SuitePolicy` (FileLogger + the three JSON-file sinks, `Rotation None`),
`make_infrastructure`, `initialize(options, loaded)`, six instruments created
through one `meter`, a paced tick loop, then `shutdown()`. Role/timers are read
before initialization. The six instruments are all `MultiThreaded` +
`ResetOnRead` (a background harvester runs each `collect_interval`), mapped as:

| Kind | Instrument | Updated each tick |
|---|---|---|
| Counter | `ticks` | increment |
| Gauge | `loop_counter` | set to tick index |
| DescriptiveStatistics | `tick_latency` | record tick interval ns |
| ExplicitHistogram | `latency_explicit` | record tick interval ns |
| ExponentialHistogram | `latency_exp` | record tick interval ns |
| StateGauge | `role` | set once to the role string |

`ResetOnRead` plus the 1 s collection interval produces the "reset once per
second" behavior across all six kinds.

### D5 — Exit path per role

The tick callback is wall-clock aware and requests a stop when the role's
interval elapses (or, for worker bounded mode, when its short duration ends).
After `run()` returns:

- worker (bounded smoke): `shutdown()`, exit 0.
- worker (forever): `run()` blocks until a signal or `request_stop`; `shutdown()`, exit 0.
- degrade: `shutdown()` (flush streams), then `return 1` → `exit.json` `exit_code: 1`.
- failure: `std::abort()` without shutdown → `exit.json` `signal: 6`, `terminated_by: "self"`.

### D6 — Script-generated manifest (no machine-specific absolute paths)

`run_suite_demo.sh` derives an absolute demo root (default
`/tmp/opencode/nexigen-suite-demo`, overridable by env), writes `suite.json`
there with `run_dir`/`artifacts_path` under that root, then runs
`nexigen-run start --config`. `clean_suite_demo.sh` removes the demo root
idempotently. This keeps the manifest reproducible on any machine.

- *Alternative considered:* commit `suite.json` with hardcoded `/tmp/...`
  paths. Rejected as non-portable and harder to review.

### D7 — No new concurrent/atomic component

The example introduces no new concurrent structure: the tick loop is a single
producer over pre-allocated instruments, and background harvest/flush
concurrency (SPSC shard pool, `alignas(64)` indices, explicit memory orders)
lives in the existing `Infrastructure`, already specified by the logging and
metrics specs. This change therefore adds no memory-layout or memory-order
decisions of its own.

## Risks / Trade-offs

- [degrade retries vs whole-suite restart interaction] When the failure app
  aborts, the whole suite restarts and the degrade app's interval and retry
  counter reset — the observed timeline is richer than "one app restarts". →
  Mitigation: the README/script section documents the expected sequence so the
  operator knows a reset is correct, not a bug.
- [Long wall-clock run] Full scenario spans ~10+ minutes. → Mitigation: it is a
  manual scenario, gated behind a dedicated script, and never a CTest test; the
  CTest smoke only runs the bounded worker mode.
- [failure abort leaves streams unflushed] The crash simulation intentionally
  drops the tail of the failure app's telemetry. → Mitigation: documented as
  expected for a genuine crash; the attempt's other streams and `runner.jsonl`
  still record the event.
- [Portability of `/tmp/opencode` default] → Mitigation: the demo root is
  overridable via env; the committed files contain no absolute paths.

## Migration Plan

Not applicable — this adds a new, optional example and touches no existing
runtime behavior. Rollback is removing the `examples/suite/` directory and the
`examples` spec delta at archive time.
